// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stratum/stratumjob.h>

#include <chainparams.h>
#include <config.h>
#include <consensus/merkle.h>
#include <hash.h>
#include <miner.h>
#include <streams.h>
#include <util/strencodings.h>
#include <validation.h>

namespace stratum {

StratumJobManager::StratumJobManager(const Config &config,
                                     CChainState &chainstate,
                                     const CTxMemPool *mempool,
                                     const CChainParams &params,
                                     const CScript &coinbaseScript,
                                     size_t extranonce1Size,
                                     size_t extranonce2Size)
    : m_nodeConfig(config), m_chainstate(chainstate), m_mempool(mempool),
      m_params(params), m_coinbaseScript(coinbaseScript),
      m_extranonce1Size(extranonce1Size),
      m_extranonce2Size(extranonce2Size) {}

uint256
StratumJobManager::ComputeLayer3Hash(const CBlockHeader &header) const {
    // Mirrors the inner layer of CBlockHeader::GetHash():
    // Layer 3: SHA256(nHeaderVersion || vSize || nHeight ||
    //                 hashEpochBlock || hashMerkleRoot ||
    //                 hashExtendedMetadata)
    CHashWriter layer3(SER_GETHASH, 0);
    layer3 << header.nHeaderVersion;
    layer3 << header.vSize;
    layer3 << header.nHeight;
    layer3 << header.hashEpochBlock;
    layer3 << header.hashMerkleRoot;
    layer3 << header.hashExtendedMetadata;
    return layer3.GetSHA256();
}

std::pair<std::string, std::string>
StratumJobManager::SplitCoinbase(const CTransaction &coinbaseTx) const {
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << coinbaseTx;
    std::string fullHex = HexStr(ss);
    std::vector<uint8_t> raw = ParseHex(fullHex);

    size_t pos = 0;
    pos += 4;  // version
    pos += 1;  // vin count (always 1 for coinbase)
    pos += 36; // prevout (32 hash + 4 index)

    size_t scriptSigLenPos = pos;

    uint64_t origScriptSigLen = 0;
    int varintBytes = 0;
    if (raw[pos] < 0xfd) {
        origScriptSigLen = raw[pos];
        varintBytes = 1;
    } else if (raw[pos] == 0xfd) {
        origScriptSigLen = raw[pos + 1] | (raw[pos + 2] << 8);
        varintBytes = 3;
    } else {
        origScriptSigLen = raw[pos + 1] | (raw[pos + 2] << 8) |
                           (raw[pos + 3] << 16) |
                           ((uint64_t)raw[pos + 4] << 24);
        varintBytes = 5;
    }
    pos += varintBytes;

    size_t scriptSigStart = pos;
    size_t scriptSigEnd = pos + origScriptSigLen;

    uint64_t extranonceSpace = m_extranonce1Size + m_extranonce2Size;
    uint64_t newScriptSigLen = origScriptSigLen + extranonceSpace;

    std::vector<uint8_t> newVarint;
    if (newScriptSigLen < 0xfd) {
        newVarint.push_back((uint8_t)newScriptSigLen);
    } else if (newScriptSigLen <= 0xffff) {
        newVarint.push_back(0xfd);
        newVarint.push_back(newScriptSigLen & 0xff);
        newVarint.push_back((newScriptSigLen >> 8) & 0xff);
    } else {
        newVarint.push_back(0xfe);
        newVarint.push_back(newScriptSigLen & 0xff);
        newVarint.push_back((newScriptSigLen >> 8) & 0xff);
        newVarint.push_back((newScriptSigLen >> 16) & 0xff);
        newVarint.push_back((newScriptSigLen >> 24) & 0xff);
    }

    std::vector<uint8_t> cb1Data;
    cb1Data.insert(cb1Data.end(), raw.begin(),
                   raw.begin() + scriptSigLenPos);
    cb1Data.insert(cb1Data.end(), newVarint.begin(), newVarint.end());
    cb1Data.insert(cb1Data.end(), raw.begin() + scriptSigStart,
                   raw.begin() + scriptSigEnd);

    std::vector<uint8_t> cb2Data(raw.begin() + scriptSigEnd, raw.end());

    return {HexStr(cb1Data), HexStr(cb2Data)};
}

std::vector<uint256>
StratumJobManager::ComputeMerkleBranches(const CBlock &block) const {
    std::vector<uint256> branches;
    std::vector<uint256> leaves;
    for (const auto &tx : block.vtx) {
        leaves.push_back(tx->GetHash());
    }

    if (leaves.size() <= 1) {
        return branches;
    }

    std::vector<uint256> level = leaves;
    size_t index = 0;
    while (level.size() > 1) {
        size_t siblingIdx = index ^ 1;
        if (siblingIdx < level.size()) {
            branches.push_back(level[siblingIdx]);
        } else {
            branches.push_back(level[index]);
        }

        std::vector<uint256> nextLevel;
        for (size_t i = 0; i < level.size(); i += 2) {
            uint256 left = level[i];
            uint256 right = (i + 1 < level.size()) ? level[i + 1] : left;
            nextLevel.push_back(Hash(left, right));
        }
        level = nextLevel;
        index /= 2;
    }

    return branches;
}

bool StratumJobManager::CreateJob(bool cleanJobs, StratumJob &jobOut,
                                  std::string &error) {
    LOCK(cs_main);

    const CBlockIndex *pindexPrev = m_chainstate.m_chain.Tip();
    if (!pindexPrev) {
        error = "No chain tip available";
        return false;
    }

    BlockAssembler assembler(m_nodeConfig, *m_mempool);
    auto pblocktemplate = assembler.CreateNewBlock(m_coinbaseScript);
    if (!pblocktemplate) {
        error = "Failed to create block template";
        return false;
    }

    CBlock &block = pblocktemplate->block;

    StratumJob job;
    job.jobId = m_nextJobId++;
    job.block = std::make_shared<CBlock>(block);
    job.cleanJobs = cleanJobs;
    job.nBitsRaw = block.nBits;
    job.height = pindexPrev->nHeight + 1;

    auto [cb1, cb2] = SplitCoinbase(*block.vtx[0]);
    job.coinbase1 = cb1;
    job.coinbase2 = cb2;

    auto branches = ComputeMerkleBranches(block);
    for (const auto &b : branches) {
        job.merkleBranches.push_back(b.GetHex());
    }

    job.prevHash = HashToStratumHex(block.hashPrevBlock);
    job.nbits = Uint32ToStratumHex(block.nBits);
    job.ntime = BytesToStratumHex(block.vTime.data(), block.vTime.size());
    job.reserved = Uint32ToStratumHex(block.nReserved);

    // Precompute layer3 hash for efficient GPU mining
    uint256 l3 = ComputeLayer3Hash(block);
    job.layer3Hash = l3.GetHex();

    // AuxPoW fields
    CBlock cleanBlock(block);
    cleanBlock.vMetadata.clear();
    cleanBlock.hashExtendedMetadata = SerializeHash(cleanBlock.vMetadata);
    cleanBlock.SetSize(GetSerializeSize(cleanBlock, PROTOCOL_VERSION));
    job.auxBlockHash = cleanBlock.GetHash().GetHex();

    m_lastTipHash = pindexPrev->GetBlockHash();

    uint64_t id = job.jobId;
    m_jobs[id] = job;
    jobOut = job;

    return true;
}

UniValue StratumJobManager::FormatNotifyParams(const StratumJob &job) const {
    UniValue params(UniValue::VARR);
    params.push_back(strprintf("%x", job.jobId));
    params.push_back(job.prevHash);
    params.push_back(job.coinbase1);
    params.push_back(job.coinbase2);

    UniValue branches(UniValue::VARR);
    for (const auto &b : job.merkleBranches) {
        branches.push_back(b);
    }
    params.push_back(branches);

    params.push_back(job.layer3Hash);
    params.push_back(job.nbits);
    params.push_back(job.ntime);
    params.push_back(job.reserved);
    params.push_back(job.cleanJobs);

    return params;
}

UniValue
StratumJobManager::FormatAuxNotifyParams(const StratumJob &job) const {
    UniValue params(UniValue::VARR);
    params.push_back(strprintf("%x", job.jobId));
    params.push_back(job.auxBlockHash);
    params.push_back(job.auxChainId);
    params.push_back(Uint32ToStratumHex(job.auxBits));
    params.push_back(job.auxTarget);

    params.push_back(job.coinbase1);
    params.push_back(job.coinbase2);

    UniValue branches(UniValue::VARR);
    for (const auto &b : job.merkleBranches) {
        branches.push_back(b);
    }
    params.push_back(branches);

    params.push_back(job.cleanJobs);

    return params;
}

const StratumJob *StratumJobManager::GetJob(uint64_t jobId) const {
    auto it = m_jobs.find(jobId);
    if (it == m_jobs.end()) {
        return nullptr;
    }
    return &it->second;
}

void StratumJobManager::PruneJobs(size_t keepCount) {
    while (m_jobs.size() > keepCount) {
        m_jobs.erase(m_jobs.begin());
    }
}

bool StratumJobManager::HasNewTip() const {
    LOCK(cs_main);
    const CBlockIndex *tip = m_chainstate.m_chain.Tip();
    if (!tip) {
        return false;
    }
    return tip->GetBlockHash() != m_lastTipHash;
}

size_t StratumJobManager::JobCount() const {
    return m_jobs.size();
}

std::string HashToStratumHex(const uint256 &hash) {
    const uint8_t *data = hash.begin();
    std::string result;
    result.reserve(64);
    for (int i = 0; i < 32; i += 4) {
        for (int j = 3; j >= 0; j--) {
            result += strprintf("%02x", data[i + j]);
        }
    }
    return result;
}

std::string Uint32ToStratumHex(uint32_t val) {
    uint8_t buf[4];
    buf[0] = val & 0xff;
    buf[1] = (val >> 8) & 0xff;
    buf[2] = (val >> 16) & 0xff;
    buf[3] = (val >> 24) & 0xff;
    return HexStr(Span<const uint8_t>(buf, 4));
}

std::string BytesToStratumHex(const uint8_t *data, size_t len) {
    return HexStr(Span<const uint8_t>(data, len));
}

} // namespace stratum
