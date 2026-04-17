// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <scryptchain/header_chain.h>

#include <crypto/scrypt.h>
#include <hash.h>
#include <logging.h>
#include <sqlite/sqlite_wrapper.h>
#include <util/strencodings.h>

#include <sqlite3.h>

#include <algorithm>
#include <cassert>

namespace scryptchain {

// ---------------------------------------------------------------------------
// ScryptHeaderDB
// ---------------------------------------------------------------------------

ScryptHeaderDB::ScryptHeaderDB(const std::string &chainName,
                               const fs::path &dataDir) {
    fs::path chainDir = dataDir / chainName;
    fs::create_directories(chainDir);
    fs::path dbPath = chainDir / "headers.sqlite";
    m_db = std::make_unique<CSqliteWrapper>(dbPath);
    m_tableName = "scrypt_headers";
    m_metaTable = "scrypt_meta";

    std::string createHeaders =
        "CREATE TABLE IF NOT EXISTS " + m_tableName +
        " (hash BLOB PRIMARY KEY, prev_hash BLOB, height INTEGER, "
        "n_bits INTEGER, n_time INTEGER, n_nonce INTEGER, n_version INTEGER, "
        "merkle_root BLOB, chain_work BLOB) WITHOUT ROWID";
    m_db->ExecSQL(createHeaders);

    std::string createMeta =
        "CREATE TABLE IF NOT EXISTS " + m_metaTable +
        " (key TEXT PRIMARY KEY, value BLOB) WITHOUT ROWID";
    m_db->ExecSQL(createMeta);

    m_db->ExecSQL("CREATE INDEX IF NOT EXISTS idx_" + m_tableName +
                  "_height ON " + m_tableName + " (height)");
}

ScryptHeaderDB::~ScryptHeaderDB() = default;

bool ScryptHeaderDB::LoadAll(
    std::unordered_map<BlockHash, std::unique_ptr<ScryptHeaderIndex>,
                       BlockHasher> &headers,
    BlockHash &tipHash, std::string &error) {
    std::string sql = "SELECT hash, prev_hash, height, n_bits, n_time, "
                      "n_nonce, n_version, merkle_root, chain_work FROM " +
                      m_tableName;
    sqlite3_stmt *stmt = m_db->Prepare(sql);
    if (!stmt) {
        error = "Failed to prepare LoadAll statement";
        return false;
    }

    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        auto idx = std::make_unique<ScryptHeaderIndex>();

        const void *hashData = sqlite3_column_blob(stmt, 0);
        if (hashData && sqlite3_column_bytes(stmt, 0) == 32) {
            std::memcpy(idx->hash.begin(), hashData, 32);
        }

        const void *prevData = sqlite3_column_blob(stmt, 1);
        if (prevData && sqlite3_column_bytes(stmt, 1) == 32) {
            std::memcpy(idx->prevHash.begin(), prevData, 32);
        }

        idx->height = sqlite3_column_int(stmt, 2);
        idx->nBits = static_cast<uint32_t>(sqlite3_column_int64(stmt, 3));
        idx->nTime = static_cast<uint32_t>(sqlite3_column_int64(stmt, 4));
        idx->nNonce = static_cast<uint32_t>(sqlite3_column_int64(stmt, 5));
        idx->nVersion = static_cast<int32_t>(sqlite3_column_int(stmt, 6));

        const void *merkleData = sqlite3_column_blob(stmt, 7);
        if (merkleData && sqlite3_column_bytes(stmt, 7) == 32) {
            std::memcpy(idx->hashMerkleRoot.begin(), merkleData, 32);
        }

        const void *workData = sqlite3_column_blob(stmt, 8);
        int workBytes = sqlite3_column_bytes(stmt, 8);
        if (workData && workBytes == 32) {
            std::vector<uint8_t> workVec(static_cast<const uint8_t *>(workData),
                                         static_cast<const uint8_t *>(workData) +
                                             workBytes);
            idx->nChainWork = UintToArith256(uint256(workVec));
        }

        BlockHash key = idx->hash;
        headers[key] = std::move(idx);
    }

    sqlite3_reset(stmt);

    std::string metaSql =
        "SELECT value FROM " + m_metaTable + " WHERE key = 'tip_hash'";
    sqlite3_stmt *metaStmt = m_db->Prepare(metaSql);
    if (metaStmt) {
        if (sqlite3_step(metaStmt) == SQLITE_ROW) {
            const void *tipData = sqlite3_column_blob(metaStmt, 0);
            if (tipData && sqlite3_column_bytes(metaStmt, 0) == 32) {
                std::memcpy(tipHash.begin(), tipData, 32);
            }
        }
        sqlite3_reset(metaStmt);
    }

    LogPrintf("ScryptHeaderDB: loaded %d headers\n", headers.size());
    return true;
}

bool ScryptHeaderDB::WriteHeader(const ScryptHeaderIndex &hdr,
                                 std::string &error) {
    std::string sql =
        "INSERT OR REPLACE INTO " + m_tableName +
        " (hash, prev_hash, height, n_bits, n_time, n_nonce, n_version, "
        "merkle_root, chain_work) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
    sqlite3_stmt *stmt = m_db->Prepare(sql);
    if (!stmt) {
        error = "Failed to prepare WriteHeader statement";
        return false;
    }

    uint256 chainWork = ArithToUint256(hdr.nChainWork);

    sqlite3_bind_blob(stmt, 1, hdr.hash.begin(), 32, SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 2, hdr.prevHash.begin(), 32, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 3, hdr.height);
    sqlite3_bind_int64(stmt, 4, hdr.nBits);
    sqlite3_bind_int64(stmt, 5, hdr.nTime);
    sqlite3_bind_int64(stmt, 6, hdr.nNonce);
    sqlite3_bind_int(stmt, 7, hdr.nVersion);
    sqlite3_bind_blob(stmt, 8, hdr.hashMerkleRoot.begin(), 32, SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 9, chainWork.begin(), 32, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_reset(stmt);
    if (rc != SQLITE_DONE) {
        error = "Failed to write header: " +
                std::string(sqlite3_errmsg(m_db->GetHandle()));
        return false;
    }
    return true;
}

bool ScryptHeaderDB::WriteBatch(const std::vector<ScryptHeaderIndex> &headers,
                                std::string &error) {
    CSqliteTransaction txn(*m_db);
    for (const auto &hdr : headers) {
        if (!WriteHeader(hdr, error)) {
            txn.Abort();
            return false;
        }
    }
    if (!headers.empty()) {
        WriteTip(headers.back().hash, error);
    }
    txn.Commit();
    return true;
}

bool ScryptHeaderDB::WriteTip(const BlockHash &hash, std::string &error) {
    std::string sql = "INSERT OR REPLACE INTO " + m_metaTable +
                      " (key, value) VALUES ('tip_hash', ?)";
    sqlite3_stmt *stmt = m_db->Prepare(sql);
    if (!stmt) {
        error = "Failed to prepare WriteTip statement";
        return false;
    }
    sqlite3_bind_blob(stmt, 1, hash.begin(), 32, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_reset(stmt);
    if (rc != SQLITE_DONE) {
        error = "Failed to write tip";
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// ScryptHeaderValidator
// ---------------------------------------------------------------------------

bool ScryptHeaderValidator::ValidateHeader(const CParentBlockHeader &hdr,
                                           const ScryptHeaderIndex *prev,
                                           const ScryptChainParams &params,
                                           int checkpointHeight,
                                           std::string &error) {
    if (!prev) {
        error = "Previous header not found";
        return false;
    }

    int newHeight = prev->height + 1;

    // Skip PoW check before the highest checkpoint for fast sync
    if (newHeight <= checkpointHeight) {
        return true;
    }

    // For DOGE AuxPoW blocks: we can't validate Scrypt PoW in headers-only
    // mode because the AuxPoW proof is not present. Rely on checkpoints and
    // cumulative work.
    if (params.isAuxPow && params.auxpowActivationHeight > 0 &&
        newHeight >= params.auxpowActivationHeight) {
        bool isAuxPowBlock = (hdr.nVersion >> 8) & 1;
        if (isAuxPowBlock) {
            return true;
        }
    }

    // Validate Scrypt PoW
    BlockHash powHash = hdr.GetPowHash();
    arith_uint256 hashTarget;
    hashTarget.SetCompact(hdr.nBits);

    if (UintToArith256(powHash) > hashTarget) {
        error = "Scrypt PoW check failed at height " +
                std::to_string(newHeight);
        return false;
    }

    // Validate nBits
    uint32_t expectedBits = GetNextWorkRequired(prev, params);
    if (hdr.nBits != expectedBits) {
        error = "Incorrect nBits at height " + std::to_string(newHeight) +
                ": got " + std::to_string(hdr.nBits) + " expected " +
                std::to_string(expectedBits);
        return false;
    }

    return true;
}

uint32_t
ScryptHeaderValidator::GetNextWorkRequired(const ScryptHeaderIndex *tip,
                                           const ScryptChainParams &params) {
    if (!tip) {
        return params.genesisNBits;
    }
    if (params.digishield && tip->height >= params.digishieldHeight) {
        return GetNextWorkRequiredDigiShield(tip, params);
    }
    return GetNextWorkRequiredBitcoin(tip, params);
}

uint32_t ScryptHeaderValidator::GetNextWorkRequiredBitcoin(
    const ScryptHeaderIndex *tip, const ScryptChainParams &params) {
    int nextHeight = tip->height + 1;

    // Only retarget every difficultyAdjustInterval blocks
    if (nextHeight % params.difficultyAdjustInterval != 0) {
        return tip->nBits;
    }

    // Walk back difficultyAdjustInterval - 1 blocks
    const ScryptHeaderIndex *first = tip;
    for (int i = 0;
         i < params.difficultyAdjustInterval - 1 && first->pprev; ++i) {
        first = first->pprev;
    }

    int64_t targetTimespan =
        params.targetSpacing * params.difficultyAdjustInterval;
    int64_t actualTimespan =
        static_cast<int64_t>(tip->nTime) - static_cast<int64_t>(first->nTime);

    // Clamp to 1/4 .. 4x
    if (actualTimespan < targetTimespan / 4) {
        actualTimespan = targetTimespan / 4;
    }
    if (actualTimespan > targetTimespan * 4) {
        actualTimespan = targetTimespan * 4;
    }

    arith_uint256 newTarget;
    newTarget.SetCompact(tip->nBits);
    newTarget *= actualTimespan;
    newTarget /= targetTimespan;

    arith_uint256 limit;
    limit.SetCompact(params.genesisNBits);
    if (newTarget > limit) {
        newTarget = limit;
    }

    return newTarget.GetCompact();
}

uint32_t ScryptHeaderValidator::GetNextWorkRequiredDigiShield(
    const ScryptHeaderIndex *tip, const ScryptChainParams &params) {
    if (!tip->pprev) {
        return tip->nBits;
    }

    int64_t targetTimespan = params.targetSpacing;
    int64_t actualTimespan =
        static_cast<int64_t>(tip->nTime) -
        static_cast<int64_t>(tip->pprev->nTime);

    // DigiShield smoothing: newTimespan = target + (actual - target) / 8
    int64_t newTimespan =
        targetTimespan + (actualTimespan - targetTimespan) / 8;

    // Clamp to [target*3/4, target*3/2]
    if (newTimespan < targetTimespan * 3 / 4) {
        newTimespan = targetTimespan * 3 / 4;
    }
    if (newTimespan > targetTimespan * 3 / 2) {
        newTimespan = targetTimespan * 3 / 2;
    }

    arith_uint256 newTarget;
    newTarget.SetCompact(tip->nBits);
    newTarget *= newTimespan;
    newTarget /= targetTimespan;

    arith_uint256 limit;
    limit.SetCompact(params.genesisNBits);
    if (newTarget > limit) {
        newTarget = limit;
    }

    return newTarget.GetCompact();
}

// ---------------------------------------------------------------------------
// ScryptHeaderChain
// ---------------------------------------------------------------------------

ScryptHeaderChain::ScryptHeaderChain(const ScryptChainParams &params,
                                     const fs::path &dataDir)
    : m_params(params) {
    m_db = std::make_unique<ScryptHeaderDB>(params.name, dataDir);

    for (const auto &[h, _] : params.checkpoints) {
        if (h > m_highestCheckpoint) {
            m_highestCheckpoint = h;
        }
    }
}

ScryptHeaderChain::~ScryptHeaderChain() = default;

bool ScryptHeaderChain::Initialize(std::string &error) {
    std::lock_guard<std::mutex> lock(m_cs);

    BlockHash tipHash;
    if (!m_db->LoadAll(m_headers, tipHash, error)) {
        return false;
    }

    // If we have no genesis, insert it
    if (m_headers.empty()) {
        auto genesis = std::make_unique<ScryptHeaderIndex>();
        genesis->hash = m_params.genesisHash;
        genesis->prevHash = BlockHash();
        genesis->height = 0;
        genesis->nBits = m_params.genesisNBits;
        genesis->nTime = m_params.genesisNTime;
        genesis->nNonce = m_params.genesisNNonce;
        genesis->nVersion = m_params.genesisNVersion;
        genesis->nChainWork = GetBlockProof(m_params.genesisNBits);

        m_tip = genesis.get();
        m_headers[m_params.genesisHash] = std::move(genesis);

        std::string err;
        m_db->WriteHeader(*m_tip, err);
        m_db->WriteTip(m_tip->hash, err);
    } else {
        LinkHeaders();

        if (!tipHash.IsNull()) {
            auto it = m_headers.find(tipHash);
            if (it != m_headers.end()) {
                m_tip = it->second.get();
            }
        }

        // Fallback: find the header with the most chain work
        if (!m_tip) {
            for (auto &[_, hdr] : m_headers) {
                if (!m_tip || hdr->nChainWork > m_tip->nChainWork) {
                    m_tip = hdr.get();
                }
            }
        }
    }

    LogPrintf("ScryptHeaderChain(%s): initialized at height %d\n",
              m_params.name, m_tip ? m_tip->height : -1);
    return true;
}

void ScryptHeaderChain::LinkHeaders() {
    for (auto &[_, hdr] : m_headers) {
        if (!hdr->prevHash.IsNull()) {
            auto prevIt = m_headers.find(hdr->prevHash);
            if (prevIt != m_headers.end()) {
                hdr->pprev = prevIt->second.get();
            }
        }
    }
}

arith_uint256 ScryptHeaderChain::GetBlockProof(uint32_t nBits) const {
    arith_uint256 target;
    bool fNegative, fOverflow;
    target.SetCompact(nBits, &fNegative, &fOverflow);
    if (fNegative || fOverflow || target == 0) {
        return arith_uint256(0);
    }
    // proof = ~target / (target + 1) + 1
    return (~target / (target + 1)) + 1;
}

bool ScryptHeaderChain::AcceptHeader(const CParentBlockHeader &hdr,
                                     std::string &error) {
    std::lock_guard<std::mutex> lock(m_cs);

    BlockHash hash = hdr.GetHash();

    // Already known
    if (m_headers.count(hash)) {
        return true;
    }

    // Find previous
    BlockHash prevHash = hdr.hashPrevBlock;
    auto prevIt = m_headers.find(prevHash);
    if (prevIt == m_headers.end()) {
        error = "Previous block not found: " + prevHash.GetHex();
        return false;
    }
    ScryptHeaderIndex *prev = prevIt->second.get();

    // Validate
    if (!ScryptHeaderValidator::ValidateHeader(hdr, prev, m_params,
                                               m_highestCheckpoint, error)) {
        return false;
    }

    // Check against checkpoints
    int newHeight = prev->height + 1;
    auto cpIt = m_params.checkpoints.find(newHeight);
    if (cpIt != m_params.checkpoints.end() && cpIt->second != hash) {
        error = "Checkpoint mismatch at height " + std::to_string(newHeight);
        return false;
    }

    auto idx = std::make_unique<ScryptHeaderIndex>();
    idx->hash = hash;
    idx->prevHash = prevHash;
    idx->height = newHeight;
    idx->nBits = hdr.nBits;
    idx->nTime = hdr.nTime;
    idx->nNonce = hdr.nNonce;
    idx->nVersion = hdr.nVersion;
    idx->hashMerkleRoot = hdr.hashMerkleRoot;
    idx->pprev = prev;
    idx->nChainWork = prev->nChainWork + GetBlockProof(hdr.nBits);

    ScryptHeaderIndex *newIdx = idx.get();
    m_headers[hash] = std::move(idx);

    // Update tip if more work
    if (!m_tip || newIdx->nChainWork > m_tip->nChainWork) {
        m_tip = newIdx;
    }

    return true;
}

bool ScryptHeaderChain::AcceptHeaders(
    const std::vector<CParentBlockHeader> &headers, std::string &error) {
    std::vector<ScryptHeaderIndex> toWrite;
    toWrite.reserve(headers.size());

    {
        std::lock_guard<std::mutex> lock(m_cs);

        for (const auto &hdr : headers) {
            BlockHash hash = hdr.GetHash();

            if (m_headers.count(hash)) {
                continue;
            }

            BlockHash prevHash = hdr.hashPrevBlock;
            auto prevIt = m_headers.find(prevHash);
            if (prevIt == m_headers.end()) {
                error = "Previous block not found: " + prevHash.GetHex();
                return false;
            }
            ScryptHeaderIndex *prev = prevIt->second.get();

            if (!ScryptHeaderValidator::ValidateHeader(
                    hdr, prev, m_params, m_highestCheckpoint, error)) {
                return false;
            }

            int newHeight = prev->height + 1;
            auto cpIt = m_params.checkpoints.find(newHeight);
            if (cpIt != m_params.checkpoints.end() && cpIt->second != hash) {
                error = "Checkpoint mismatch at height " +
                        std::to_string(newHeight);
                return false;
            }

            auto idx = std::make_unique<ScryptHeaderIndex>();
            idx->hash = hash;
            idx->prevHash = prevHash;
            idx->height = newHeight;
            idx->nBits = hdr.nBits;
            idx->nTime = hdr.nTime;
            idx->nNonce = hdr.nNonce;
            idx->nVersion = hdr.nVersion;
            idx->hashMerkleRoot = hdr.hashMerkleRoot;
            idx->pprev = prev;
            idx->nChainWork = prev->nChainWork + GetBlockProof(hdr.nBits);

            toWrite.push_back(*idx);

            ScryptHeaderIndex *newIdx = idx.get();
            m_headers[hash] = std::move(idx);

            if (!m_tip || newIdx->nChainWork > m_tip->nChainWork) {
                m_tip = newIdx;
            }
        }
    }

    if (!toWrite.empty()) {
        return m_db->WriteBatch(toWrite, error);
    }
    return true;
}

const ScryptHeaderIndex *ScryptHeaderChain::GetTip() const {
    std::lock_guard<std::mutex> lock(m_cs);
    return m_tip;
}

int ScryptHeaderChain::GetHeight() const {
    std::lock_guard<std::mutex> lock(m_cs);
    return m_tip ? m_tip->height : -1;
}

bool ScryptHeaderChain::IsSynced() const {
    std::lock_guard<std::mutex> lock(m_cs);
    if (!m_tip) {
        return false;
    }
    int64_t now = GetTime();
    return (now - static_cast<int64_t>(m_tip->nTime)) < 3600;
}

const ScryptHeaderIndex *
ScryptHeaderChain::GetIndex(const BlockHash &hash) const {
    std::lock_guard<std::mutex> lock(m_cs);
    auto it = m_headers.find(hash);
    if (it != m_headers.end()) {
        return it->second.get();
    }
    return nullptr;
}

std::vector<BlockHash> ScryptHeaderChain::GetBlockLocator() const {
    std::lock_guard<std::mutex> lock(m_cs);

    std::vector<BlockHash> locator;
    if (!m_tip) {
        locator.push_back(m_params.genesisHash);
        return locator;
    }

    const ScryptHeaderIndex *cur = m_tip;
    int step = 1;
    while (cur) {
        locator.push_back(cur->hash);
        if (cur->height == 0) {
            break;
        }
        int targetHeight = cur->height - step;
        if (targetHeight < 0) {
            targetHeight = 0;
        }
        while (cur && cur->height > targetHeight) {
            cur = cur->pprev;
        }
        if (locator.size() > 10) {
            step *= 2;
        }
    }
    return locator;
}

int ScryptHeaderChain::GetHighestCheckpoint() const {
    return m_highestCheckpoint;
}

} // namespace scryptchain
