// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRYPTCHAIN_TEMPLATE_BUILDER_H
#define BITCOIN_SCRYPTCHAIN_TEMPLATE_BUILDER_H

#include <auxmining/auxmining.h>
#include <primitives/parentheader.h>
#include <primitives/transaction.h>
#include <scryptchain/chain_params.h>
#include <script/script.h>
#include <uint256.h>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace scryptchain {

class ScryptHeaderChain;
class ScryptMemPool;

struct ScryptBlockTemplate {
    CParentBlockHeader header;
    CMutableTransaction coinbaseTx;
    std::vector<CTransaction> txs;
    std::vector<uint256> txMerkleBranches;

    std::string coinbase1;
    std::string coinbase2;

    MultiChainCommitment commitment;

    int height{0};

    /**
     * The block hash of this chain's template (used for the merge-mine
     * commitment when this is a child chain like DOGE).
     */
    uint256 GetBlockHash() const { return uint256(header.GetHash()); }
};

class ScryptTemplateBuilder {
public:
    ScryptTemplateBuilder();
    ~ScryptTemplateBuilder();

    /**
     * Create a DOGE block template (child chain).
     * Returns the dogeBlockHash for use in the merge-mine commitment.
     */
    std::shared_ptr<ScryptBlockTemplate>
    CreateDogeTemplate(ScryptHeaderChain &dogeChain,
                       ScryptMemPool *dogeMempool,
                       const CScript &dogeCoinbaseScript);

    /**
     * Create an LTC block template (parent chain) with merge-mine commitment.
     * @param ltcChain The LTC header chain
     * @param ltcMempool Transaction pool (may be null for coinbase-only)
     * @param lotusAuxHash Lotus block's pre-AuxPoW hash
     * @param dogeBlockHash DOGE block hash from CreateDogeTemplate
     * @param ltcCoinbaseScript Script paying LTC block reward
     * @param extranoncePlaceholderLen Bytes reserved for extranonce
     */
    std::shared_ptr<ScryptBlockTemplate>
    CreateLtcTemplate(ScryptHeaderChain &ltcChain, ScryptMemPool *ltcMempool,
                      const uint256 &lotusAuxHash,
                      const uint256 &dogeBlockHash,
                      const CScript &ltcCoinbaseScript,
                      size_t extranoncePlaceholderLen = 8);

private:
    static int64_t GetBlockSubsidy(const ScryptChainParams &params,
                                   int height);
    static uint256 ComputeMerkleRoot(const CMutableTransaction &coinbase,
                                     const std::vector<CTransaction> &txs);
    static void SplitCoinbase(const CMutableTransaction &coinbase,
                              size_t extranoncePlaceholderLen,
                              std::string &coinbase1,
                              std::string &coinbase2);
};

} // namespace scryptchain

#endif // BITCOIN_SCRYPTCHAIN_TEMPLATE_BUILDER_H
