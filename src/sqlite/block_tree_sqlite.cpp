// Copyright (c) 2024 The Logos Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sqlite/block_tree_sqlite.h>
#include <sqlite/schema.h>

#include <blockindex.h>
#include <blockstatus.h>
#include <blockvalidity.h>
#include <chain.h>
#include <clientversion.h>
#include <logging.h>
#include <mockblockgen.h>
#include <pow/pow.h>
#include <primitives/blockhash.h>
#include <shutdown.h>
#include <sqlite3.h>
#include <streams.h>
#include <version.h>

#include <cassert>

static void BindHash(sqlite3_stmt *stmt, int idx, const uint256 &hash) {
    sqlite3_bind_blob(stmt, idx, hash.begin(), 32, SQLITE_STATIC);
}

static uint256 ColHash(sqlite3_stmt *stmt, int col) {
    const void *d = sqlite3_column_blob(stmt, col);
    int len = sqlite3_column_bytes(stmt, col);
    uint256 out;
    if (d && len == 32) {
        memcpy(out.begin(), d, 32);
    }
    return out;
}

CBlockTreeSqlite::CBlockTreeSqlite(const fs::path &db_path, size_t,
                                   bool fMemory, bool fWipe) {
    m_db = std::make_unique<CSqliteWrapper>(db_path, fMemory, fWipe);
    InitSchema();
}

void CBlockTreeSqlite::InitSchema() {
    sqlite_schema::CreateAllTables(m_db->GetHandle());
    sqlite_schema::CreateAllIndexes(m_db->GetHandle());
}

bool CBlockTreeSqlite::WriteBatchSync(
    const std::vector<std::pair<int, const CBlockFileInfo *>> &fileInfo,
    int nLastFile,
    const std::vector<const CBlockIndex *> &blockinfo) {

    if (!m_db->BeginTransaction()) {
        return false;
    }

    // Write block file info as serialized blobs
    sqlite3_stmt *fi_stmt = m_db->Prepare(
        "INSERT OR REPLACE INTO block_file_info(file_num, data) "
        "VALUES(?1, ?2)");
    for (const auto &[fileNum, pInfo] : fileInfo) {
        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << *pInfo;
        sqlite3_bind_int(fi_stmt, 1, fileNum);
        sqlite3_bind_blob(fi_stmt, 2, ss.data(), ss.size(), SQLITE_TRANSIENT);
        sqlite3_step(fi_stmt);
        sqlite3_reset(fi_stmt);
    }

    // Write last block file
    sqlite3_stmt *meta_stmt = m_db->Prepare(
        "INSERT OR REPLACE INTO meta(key, value) VALUES(?1, ?2)");
    {
        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << nLastFile;
        sqlite3_bind_text(meta_stmt, 1, "last_block_file", -1, SQLITE_STATIC);
        sqlite3_bind_blob(meta_stmt, 2, ss.data(), ss.size(), SQLITE_TRANSIENT);
        sqlite3_step(meta_stmt);
        sqlite3_reset(meta_stmt);
    }

    // Write block index entries — store nStatus as serialized blob
    sqlite3_stmt *bi_stmt = m_db->Prepare(
        "INSERT OR REPLACE INTO block_index("
        "hash, prev_hash, height, n_file, data_pos, undo_pos, status, "
        "n_tx, n_size, n_bits, n_time, n_reserved, n_nonce, n_header_ver, "
        "n_height, hash_epoch, hash_merkle, hash_extmeta"
        ") VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,"
        "?16,?17,?18)");

    for (const CBlockIndex *pindex : blockinfo) {
        BlockHash hash = pindex->GetBlockHash();
        BlockHash prevHash =
            pindex->pprev ? pindex->pprev->GetBlockHash() : BlockHash();

        BindHash(bi_stmt, 1, hash);
        BindHash(bi_stmt, 2, prevHash);
        sqlite3_bind_int(bi_stmt, 3, pindex->nHeight);
        sqlite3_bind_int(bi_stmt, 4, pindex->nFile);
        sqlite3_bind_int(bi_stmt, 5, pindex->nDataPos);
        sqlite3_bind_int(bi_stmt, 6, pindex->nUndoPos);

        // Serialize BlockStatus to blob (preserves full flags)
        {
            CDataStream ss(SER_DISK, CLIENT_VERSION);
            ss << pindex->nStatus;
            sqlite3_bind_blob(bi_stmt, 7, ss.data(), ss.size(),
                              SQLITE_TRANSIENT);
        }

        sqlite3_bind_int(bi_stmt, 8, pindex->nTx);
        sqlite3_bind_int64(bi_stmt, 9, pindex->nSize);
        sqlite3_bind_int(bi_stmt, 10, pindex->nBits);
        sqlite3_bind_int64(bi_stmt, 11, pindex->nTime);
        sqlite3_bind_int(bi_stmt, 12, pindex->nReserved);
        sqlite3_bind_int64(bi_stmt, 13, pindex->nNonce);
        sqlite3_bind_int(bi_stmt, 14, pindex->nHeaderVersion);
        sqlite3_bind_int(bi_stmt, 15, pindex->nHeight);
        BindHash(bi_stmt, 16, pindex->hashEpochBlock);
        BindHash(bi_stmt, 17, pindex->hashMerkleRoot);
        BindHash(bi_stmt, 18, pindex->hashExtendedMetadata);

        sqlite3_step(bi_stmt);
        sqlite3_reset(bi_stmt);
    }

    // Write version
    {
        CDataStream ss(SER_DISK, CLIENT_VERSION);
        ss << static_cast<uint64_t>(CLIENT_VERSION);
        sqlite3_bind_text(meta_stmt, 1, "version", -1, SQLITE_STATIC);
        sqlite3_bind_blob(meta_stmt, 2, ss.data(), ss.size(), SQLITE_TRANSIENT);
        sqlite3_step(meta_stmt);
        sqlite3_reset(meta_stmt);
    }

    return m_db->CommitTransaction();
}

bool CBlockTreeSqlite::ReadBlockFileInfo(int nFile, CBlockFileInfo &info) {
    sqlite3_stmt *stmt = m_db->Prepare(
        "SELECT data FROM block_file_info WHERE file_num = ?1");
    sqlite3_bind_int(stmt, 1, nFile);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_reset(stmt);
        return false;
    }

    const void *data = sqlite3_column_blob(stmt, 0);
    int len = sqlite3_column_bytes(stmt, 0);
    CDataStream ss(static_cast<const char *>(data),
                   static_cast<const char *>(data) + len, SER_DISK,
                   CLIENT_VERSION);
    ss >> info;

    sqlite3_reset(stmt);
    return true;
}

bool CBlockTreeSqlite::ReadLastBlockFile(int &nFile) {
    sqlite3_stmt *stmt = m_db->Prepare(
        "SELECT value FROM meta WHERE key = 'last_block_file'");

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_reset(stmt);
        return false;
    }

    const void *data = sqlite3_column_blob(stmt, 0);
    int len = sqlite3_column_bytes(stmt, 0);
    CDataStream ss(static_cast<const char *>(data),
                   static_cast<const char *>(data) + len, SER_DISK,
                   CLIENT_VERSION);
    ss >> nFile;

    sqlite3_reset(stmt);
    return true;
}

bool CBlockTreeSqlite::WriteReindexing(bool fReindexing) {
    if (fReindexing) {
        sqlite3_stmt *stmt = m_db->Prepare(
            "INSERT OR REPLACE INTO meta(key, value) "
            "VALUES('reindex', X'01')");
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
        return true;
    }
    sqlite3_stmt *stmt =
        m_db->Prepare("DELETE FROM meta WHERE key = 'reindex'");
    sqlite3_step(stmt);
    sqlite3_reset(stmt);
    return true;
}

bool CBlockTreeSqlite::IsReindexing() const {
    sqlite3_stmt *stmt =
        const_cast<CSqliteWrapper *>(m_db.get())
            ->Prepare("SELECT 1 FROM meta WHERE key = 'reindex'");
    int rc = sqlite3_step(stmt);
    sqlite3_reset(stmt);
    return rc == SQLITE_ROW;
}

bool CBlockTreeSqlite::WriteFlag(const std::string &name, bool fValue) {
    std::string key = "flag_" + name;
    if (fValue) {
        sqlite3_stmt *stmt = m_db->Prepare(
            "INSERT OR REPLACE INTO meta(key, value) VALUES(?1, X'01')");
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    } else {
        sqlite3_stmt *stmt =
            m_db->Prepare("DELETE FROM meta WHERE key = ?1");
        sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_reset(stmt);
    }
    return true;
}

bool CBlockTreeSqlite::ReadFlag(const std::string &name, bool &fValue) {
    std::string key = "flag_" + name;
    sqlite3_stmt *stmt =
        m_db->Prepare("SELECT value FROM meta WHERE key = ?1");
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_reset(stmt);
        fValue = false;
        return false;
    }

    const void *data = sqlite3_column_blob(stmt, 0);
    int len = sqlite3_column_bytes(stmt, 0);
    fValue =
        (len > 0 && static_cast<const uint8_t *>(data)[0] != 0);

    sqlite3_reset(stmt);
    return true;
}

bool CBlockTreeSqlite::LoadBlockIndexGuts(
    const Consensus::Params &params,
    std::function<CBlockIndex *(const BlockHash &)> insertBlockIndex) {

    // Check version
    sqlite3_stmt *ver_stmt =
        m_db->Prepare("SELECT value FROM meta WHERE key = 'version'");
    int rc = sqlite3_step(ver_stmt);
    if (rc == SQLITE_ROW) {
        const void *data = sqlite3_column_blob(ver_stmt, 0);
        int len = sqlite3_column_bytes(ver_stmt, 0);
        CDataStream ss(static_cast<const char *>(data),
                       static_cast<const char *>(data) + len, SER_DISK,
                       CLIENT_VERSION);
        uint64_t version = 0;
        ss >> version;
        if (version != CLIENT_VERSION) {
            sqlite3_reset(ver_stmt);
            LogPrintf("%s: Invalid block index database version: %llu\n",
                      __func__, version);
            return false;
        }
    }
    sqlite3_reset(ver_stmt);

    // Load all block index entries — new stmt (not cached) for iteration
    sqlite3_stmt *stmt = nullptr;
    rc = sqlite3_prepare_v2(
        m_db->GetHandle(),
        "SELECT hash, prev_hash, n_file, data_pos, undo_pos, status, "
        "n_tx, n_size, n_bits, n_time, n_reserved, n_nonce, n_header_ver, "
        "n_height, hash_epoch, hash_merkle, hash_extmeta "
        "FROM block_index",
        -1, &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return false;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (ShutdownRequested()) {
            sqlite3_finalize(stmt);
            return false;
        }

        BlockHash blockHash(ColHash(stmt, 0));
        BlockHash prevHash(ColHash(stmt, 1));

        CBlockIndex *pindexNew = insertBlockIndex(blockHash);
        pindexNew->pprev = insertBlockIndex(prevHash);
        pindexNew->nFile = sqlite3_column_int(stmt, 2);
        pindexNew->nDataPos = sqlite3_column_int(stmt, 3);
        pindexNew->nUndoPos = sqlite3_column_int(stmt, 4);

        // Deserialize BlockStatus from blob
        {
            const void *statusData = sqlite3_column_blob(stmt, 5);
            int statusLen = sqlite3_column_bytes(stmt, 5);
            if (statusData && statusLen > 0) {
                CDataStream ss(static_cast<const char *>(statusData),
                               static_cast<const char *>(statusData) +
                                   statusLen,
                               SER_DISK, CLIENT_VERSION);
                ss >> pindexNew->nStatus;
            }
        }

        pindexNew->nTx = sqlite3_column_int(stmt, 6);
        pindexNew->nSize = sqlite3_column_int64(stmt, 7);
        pindexNew->nBits = sqlite3_column_int(stmt, 8);
        pindexNew->nTime = sqlite3_column_int64(stmt, 9);
        pindexNew->nReserved = sqlite3_column_int(stmt, 10);
        pindexNew->nNonce = sqlite3_column_int64(stmt, 11);
        pindexNew->nHeaderVersion = sqlite3_column_int(stmt, 12);
        pindexNew->nHeight = sqlite3_column_int(stmt, 13);
        pindexNew->hashEpochBlock = ColHash(stmt, 14);
        pindexNew->hashMerkleRoot = ColHash(stmt, 15);
        pindexNew->hashExtendedMetadata = ColHash(stmt, 16);

        const bool skipPoW = IsMockBlockMode();
        if (!skipPoW &&
            !CheckProofOfWork(pindexNew->GetBlockHash(), pindexNew->nBits,
                              params)) {
            sqlite3_finalize(stmt);
            LogPrintf("%s: CheckProofOfWork failed: %s\n", __func__,
                      pindexNew->ToString());
            return false;
        }
    }

    sqlite3_finalize(stmt);
    return true;
}

bool CBlockTreeSqlite::Upgrade(const Consensus::Params &) {
    return true;
}

bool CBlockTreeSqlite::IsEmpty() {
    sqlite3_stmt *stmt =
        m_db->Prepare("SELECT 1 FROM block_index LIMIT 1");
    int rc = sqlite3_step(stmt);
    sqlite3_reset(stmt);
    return rc != SQLITE_ROW;
}
