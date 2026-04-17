// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_STRATUM_STRATUMJOB_H
#define BITCOIN_STRATUM_STRATUMJOB_H

#include <primitives/block.h>
#include <script/script.h>
#include <univalue.h>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class CChainParams;
class CChainState;
class CTxMemPool;
class Config;
struct CBlockTemplate;

namespace scryptchain {
struct ScryptBlockTemplate;
class ScryptSolutionHandler;
class ScryptTemplateBuilder;
class ScryptHeaderChain;
class ScryptMemPool;
} // namespace scryptchain

namespace stratum {

struct StratumJob {
    uint64_t jobId;
    std::shared_ptr<CBlock> block;
    std::string coinbase1;
    std::string coinbase2;
    std::vector<std::string> merkleBranches;
    std::string prevHash;
    std::string nbits;
    std::string ntime;
    std::string reserved;
    std::string layer3Hash;
    bool cleanJobs = false;
    uint32_t nBitsRaw = 0;
    int height = 0;

    // AuxPoW fields (populated when share chain active)
    std::string auxBlockHash;
    int auxChainId = 0;
    uint32_t auxBits = 0;
    std::string auxTarget;

    // Multi-chain Scrypt fields (LTC as parent, DOGE+Lotus as children)
    std::shared_ptr<scryptchain::ScryptBlockTemplate> ltcTemplate;
    std::shared_ptr<scryptchain::ScryptBlockTemplate> dogeTemplate;
    uint256 lotusAuxHash;

    std::string ltcCoinbase1, ltcCoinbase2;
    std::vector<std::string> ltcMerkleBranches;
    std::string ltcPrevHash, ltcNbits, ltcNtime;
    int ltcHeight{0};
};

class StratumJobManager {
public:
    StratumJobManager(const Config &config, CChainState &chainstate,
                      const CTxMemPool *mempool,
                      const CChainParams &params,
                      const CScript &coinbaseScript,
                      size_t extranonce1Size, size_t extranonce2Size);

    bool CreateJob(bool cleanJobs, StratumJob &jobOut, std::string &error);

    UniValue FormatNotifyParams(const StratumJob &job) const;
    UniValue FormatAuxNotifyParams(const StratumJob &job) const;

    const StratumJob *GetJob(uint64_t jobId) const;
    void PruneJobs(size_t keepCount);
    bool HasNewTip() const;
    size_t JobCount() const;

private:
    const Config &m_nodeConfig;
    CChainState &m_chainstate;
    const CTxMemPool *m_mempool;
    const CChainParams &m_params;
    CScript m_coinbaseScript;
    size_t m_extranonce1Size;
    size_t m_extranonce2Size;
    uint64_t m_nextJobId = 1;
    std::map<uint64_t, StratumJob> m_jobs;
    BlockHash m_lastTipHash;

    std::pair<std::string, std::string>
    SplitCoinbase(const CTransaction &coinbaseTx) const;

    std::vector<uint256> ComputeMerkleBranches(const CBlock &block) const;

    /**
     * Precompute the inner layer (layer 3) of the Lotus triple-SHA-256 hash.
     * This hash is constant for a given job and can be sent to miners so
     * they only need to compute layers 2 and 1 per nonce attempt.
     */
    uint256 ComputeLayer3Hash(const CBlockHeader &header) const;
};

std::string HashToStratumHex(const uint256 &hash);
std::string Uint32ToStratumHex(uint32_t val);
std::string BytesToStratumHex(const uint8_t *data, size_t len);

} // namespace stratum

#endif // BITCOIN_STRATUM_STRATUMJOB_H
