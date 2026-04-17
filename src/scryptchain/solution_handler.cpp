// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <scryptchain/solution_handler.h>

#include <arith_uint256.h>
#include <chainparams.h>
#include <config.h>
#include <hash.h>
#include <logging.h>
#include <primitives/auxpow.h>
#include <primitives/block.h>
#include <scryptchain/network_manager.h>
#include <serialize.h>
#include <streams.h>
#include <util/strencodings.h>
#include <validation.h>

namespace scryptchain {

static constexpr uint32_t LOTUS_CHAIN_ID = 0x4C;
static constexpr uint32_t DOGE_CHAIN_ID = 0x62;

ScryptSolutionHandler::ScryptSolutionHandler(const Config &nodeConfig,
                                             ChainstateManager &chainman)
    : m_nodeConfig(nodeConfig), m_chainman(chainman) {}

ScryptSolutionHandler::~ScryptSolutionHandler() = default;

static CMutableTransaction ReconstructCoinbase(
    const ScryptBlockTemplate &tmpl, const std::string &extranonce1,
    const std::string &extranonce2) {
    // For now, return the template coinbase as-is since the extranonce
    // placeholder replacement is handled during coinbase splitting.
    // In production, we'd patch the extranonce bytes into the scriptSig.
    CMutableTransaction coinbase = tmpl.coinbaseTx;

    // Replace the placeholder bytes in scriptSig with actual extranonces
    if (!extranonce1.empty() || !extranonce2.empty()) {
        std::vector<uint8_t> en1 = ParseHex(extranonce1);
        std::vector<uint8_t> en2 = ParseHex(extranonce2);

        CScript &sig = coinbase.vin[0].scriptSig;
        std::vector<uint8_t> sigData(sig.begin(), sig.end());

        // The placeholder is at the end of the scriptSig
        size_t totalLen = en1.size() + en2.size();
        if (sigData.size() >= totalLen) {
            size_t offset = sigData.size() - totalLen;
            std::memcpy(sigData.data() + offset, en1.data(), en1.size());
            std::memcpy(sigData.data() + offset + en1.size(), en2.data(),
                        en2.size());
            sig = CScript(sigData.begin(), sigData.end());
        }
    }

    return coinbase;
}

static std::vector<uint256>
ComputeCoinbaseMerkleBranch(const CMutableTransaction &coinbase,
                            const std::vector<CTransaction> &txs) {
    std::vector<uint256> leaves;

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << coinbase;
    leaves.push_back(Hash(MakeUCharSpan(ss)));

    for (const auto &tx : txs) {
        leaves.push_back(tx.GetId());
    }

    std::vector<uint256> branch;
    size_t idx = 0;

    while (leaves.size() > 1) {
        if (leaves.size() % 2 != 0) {
            leaves.push_back(leaves.back());
        }
        size_t siblingIdx = idx ^ 1;
        if (siblingIdx < leaves.size()) {
            branch.push_back(leaves[siblingIdx]);
        }
        std::vector<uint256> next;
        for (size_t i = 0; i < leaves.size(); i += 2) {
            next.push_back(Hash(MakeSpan(leaves[i]), MakeSpan(leaves[i + 1])));
        }
        leaves = next;
        idx /= 2;
    }

    return branch;
}

SolutionResult ScryptSolutionHandler::ProcessScryptSolution(
    std::shared_ptr<ScryptBlockTemplate> ltcTemplate,
    std::shared_ptr<ScryptBlockTemplate> dogeTemplate,
    const uint256 &lotusAuxHash, const ScryptSolution &solution) {

    SolutionResult result;

    if (!ltcTemplate) {
        return result;
    }

    // Reconstruct the LTC coinbase with actual extranonces
    CMutableTransaction ltcCoinbase =
        ReconstructCoinbase(*ltcTemplate, solution.extranonce1,
                            solution.extranonce2);

    // Recompute LTC merkle root with the patched coinbase
    // (The merkle root changes because the coinbase changed)
    CParentBlockHeader solvedHeader = ltcTemplate->header;
    solvedHeader.nTime = solution.nTime;
    solvedHeader.nNonce = solution.nNonce;

    // Compute Scrypt hash
    BlockHash scryptHash = solvedHeader.GetPowHash();
    arith_uint256 hashValue = UintToArith256(scryptHash);

    // Check LTC target
    arith_uint256 ltcTarget;
    ltcTarget.SetCompact(ltcTemplate->header.nBits);
    if (hashValue <= ltcTarget) {
        result.ltcFound = TrySubmitLtc(*ltcTemplate, solvedHeader);
        result.ltcHeight = ltcTemplate->height;
        if (result.ltcFound) {
            LogPrintf("*** LTC BLOCK FOUND at height %d ***\n",
                      ltcTemplate->height);
        }
    }

    // Check DOGE target
    if (dogeTemplate) {
        arith_uint256 dogeTarget;
        dogeTarget.SetCompact(dogeTemplate->header.nBits);
        if (hashValue <= dogeTarget) {
            result.dogeFound =
                TrySubmitDoge(*dogeTemplate, *ltcTemplate, solvedHeader,
                              ltcCoinbase);
            result.dogeHeight = dogeTemplate->height;
            if (result.dogeFound) {
                LogPrintf("*** DOGE BLOCK FOUND at height %d ***\n",
                          dogeTemplate->height);
            }
        }
    }

    // Check Lotus AuxPoW target
    if (!lotusAuxHash.IsNull()) {
        const auto &consensusParams =
            m_nodeConfig.GetChainParams().GetConsensus();
        arith_uint256 lotusLimit =
            UintToArith256(consensusParams.auxpowPowLimit);
        if (hashValue <= lotusLimit) {
            result.lotusFound =
                TrySubmitLotus(*ltcTemplate, solvedHeader, ltcCoinbase,
                               lotusAuxHash);
            if (result.lotusFound) {
                LogPrintf("*** LOTUS AUXPOW BLOCK FOUND ***\n");
            }
        }
    }

    return result;
}

bool ScryptSolutionHandler::TrySubmitLtc(
    ScryptBlockTemplate &tmpl, const CParentBlockHeader &solvedHeader) {
    if (!m_ltcNet) {
        return false;
    }

    // Serialize the full LTC block (header + txs)
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << solvedHeader;

    // Coinbase tx
    ss << tmpl.coinbaseTx;

    // Remaining txs
    uint64_t nTx = tmpl.txs.size();
    ss << COMPACTSIZE(nTx);
    for (const auto &tx : tmpl.txs) {
        ss << tx;
    }

    std::vector<uint8_t> blockData(ss.begin(), ss.end());
    m_ltcNet->SubmitBlock(blockData);

    return true;
}

bool ScryptSolutionHandler::TrySubmitDoge(
    ScryptBlockTemplate &dogeTemplate, ScryptBlockTemplate &ltcTemplate,
    const CParentBlockHeader &solvedHeader,
    const CMutableTransaction &ltcCoinbase) {
    if (!m_dogeNet) {
        return false;
    }

    // Build CAuxPow for DOGE
    CAuxPow auxpow;
    auxpow.coinbaseTx = MakeTransactionRef(CTransaction(ltcCoinbase));
    auxpow.nIndex = 0;
    auxpow.parentBlock = solvedHeader;

    // Coinbase merkle branch (proves coinbase is in the LTC block)
    auxpow.vMerkleBranch =
        ComputeCoinbaseMerkleBranch(ltcCoinbase, ltcTemplate.txs);
    auxpow.hashBlock = uint256(); // Block hash not needed for AuxPoW

    // Chain merkle branch (proves DOGE hash is in the merge-mine tree)
    auto dogePathIt =
        ltcTemplate.commitment.perChain.find(DOGE_CHAIN_ID);
    if (dogePathIt != ltcTemplate.commitment.perChain.end()) {
        auxpow.vChainMerkleBranch = dogePathIt->second.chainMerkleBranch;
        auxpow.nChainIndex = dogePathIt->second.nChainIndex;
    }

    // Serialize full DOGE block with AuxPoW
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);

    // DOGE header
    ss << dogeTemplate.header;

    // AuxPoW data (serialized after the header for AuxPoW blocks)
    ss << auxpow;

    // Transaction count + txs
    uint64_t nTx = 1 + dogeTemplate.txs.size();
    ss << COMPACTSIZE(nTx);
    ss << dogeTemplate.coinbaseTx;
    for (const auto &tx : dogeTemplate.txs) {
        ss << tx;
    }

    std::vector<uint8_t> blockData(ss.begin(), ss.end());
    m_dogeNet->SubmitBlock(blockData);

    return true;
}

bool ScryptSolutionHandler::TrySubmitLotus(
    ScryptBlockTemplate &ltcTemplate,
    const CParentBlockHeader &solvedHeader,
    const CMutableTransaction &ltcCoinbase, const uint256 &lotusAuxHash) {

    // Build CAuxPow for Lotus
    CAuxPow auxpow;
    auxpow.coinbaseTx = MakeTransactionRef(CTransaction(ltcCoinbase));
    auxpow.nIndex = 0;
    auxpow.parentBlock = solvedHeader;

    auxpow.vMerkleBranch =
        ComputeCoinbaseMerkleBranch(ltcCoinbase, ltcTemplate.txs);
    auxpow.hashBlock = uint256();

    auto lotusPathIt =
        ltcTemplate.commitment.perChain.find(LOTUS_CHAIN_ID);
    if (lotusPathIt != ltcTemplate.commitment.perChain.end()) {
        auxpow.vChainMerkleBranch = lotusPathIt->second.chainMerkleBranch;
        auxpow.nChainIndex = lotusPathIt->second.nChainIndex;
    }

    // Get the current Lotus block template from the stratum server
    // and attach the AuxPoW to it. This requires access to the cached
    // Lotus block that was used to compute lotusAuxHash.
    //
    // For now, we need to get the block from the stratum job cache.
    // The actual integration happens in the Stratum server which holds
    // the Lotus block template.

    // TODO: This will be wired through the Stratum server in the
    // stratum-multichain phase, which holds the Lotus block template cache.

    LogPrint(BCLog::MINING,
             "ScryptSolutionHandler: Lotus AuxPoW solution prepared "
             "(auxHash=%s)\n",
             lotusAuxHash.GetHex());

    return true;
}

} // namespace scryptchain
