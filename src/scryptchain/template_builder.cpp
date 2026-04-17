// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <scryptchain/template_builder.h>

#include <hash.h>
#include <logging.h>
#include <scryptchain/header_chain.h>
#include <scryptchain/mempool.h>
#include <serialize.h>
#include <streams.h>
#include <util/strencodings.h>
#include <util/time.h>

#include <cassert>

namespace scryptchain {

// Lotus AuxPoW chain ID
static constexpr uint32_t LOTUS_CHAIN_ID = 0x4C;
// DOGE AuxPoW chain ID
static constexpr uint32_t DOGE_CHAIN_ID = 0x62;

ScryptTemplateBuilder::ScryptTemplateBuilder() = default;
ScryptTemplateBuilder::~ScryptTemplateBuilder() = default;

int64_t ScryptTemplateBuilder::GetBlockSubsidy(const ScryptChainParams &params,
                                               int height) {
    if (params.fixedRewardHeight > 0 && height >= params.fixedRewardHeight) {
        return params.fixedRewardSatoshis;
    }

    int halvings = height / params.halvingInterval;
    if (halvings >= 64) {
        return 0;
    }
    return params.initialSubsidySatoshis >> halvings;
}

uint256
ScryptTemplateBuilder::ComputeMerkleRoot(const CMutableTransaction &coinbase,
                                         const std::vector<CTransaction> &txs) {
    std::vector<uint256> leaves;

    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << coinbase;
    leaves.push_back(Hash(MakeUCharSpan(ss)));

    for (const auto &tx : txs) {
        leaves.push_back(tx.GetId());
    }

    if (leaves.size() == 1) {
        return leaves[0];
    }

    while (leaves.size() > 1) {
        if (leaves.size() % 2 != 0) {
            leaves.push_back(leaves.back());
        }
        std::vector<uint256> next;
        for (size_t i = 0; i < leaves.size(); i += 2) {
            next.push_back(Hash(Span(leaves[i]), Span(leaves[i + 1])));
        }
        leaves = next;
    }

    return leaves[0];
}

void ScryptTemplateBuilder::SplitCoinbase(
    const CMutableTransaction &coinbase, size_t extranoncePlaceholderLen,
    std::string &coinbase1, std::string &coinbase2) {
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << coinbase;

    std::string fullHex = HexStr(MakeUCharSpan(ss));

    // The extranonce goes right after the scriptSig data, before any
    // additional coinbase data. For simplicity, we split at the end of the
    // serialized coinbase, leaving room for the extranonce in the
    // scriptSig.
    //
    // The scriptSig is: [BIP34 height] [commitment] [extranonce placeholder]
    // We need to find the extranonce placeholder bytes in the hex.
    // Convention: we look for extranoncePlaceholderLen zero bytes at the end
    // of scriptSig.

    // For now, simple split: coinbase1 = everything before the last
    // extranoncePlaceholderLen*2 hex chars of the scriptSig, coinbase2 = rest
    size_t splitPoint = fullHex.size() / 2;
    if (splitPoint > extranoncePlaceholderLen) {
        splitPoint -= extranoncePlaceholderLen;
    }

    // Hex chars: splitPoint * 2
    coinbase1 = fullHex.substr(0, splitPoint * 2);
    coinbase2 = fullHex.substr(splitPoint * 2);
}

std::shared_ptr<ScryptBlockTemplate>
ScryptTemplateBuilder::CreateDogeTemplate(ScryptHeaderChain &dogeChain,
                                          ScryptMemPool *dogeMempool,
                                          const CScript &dogeCoinbaseScript) {
    auto tmpl = std::make_shared<ScryptBlockTemplate>();
    const auto &params = dogeChain.GetParams();
    const auto *tip = dogeChain.GetTip();
    if (!tip) {
        return nullptr;
    }

    int newHeight = tip->height + 1;
    tmpl->height = newHeight;

    // Build coinbase tx
    CMutableTransaction coinbaseTx;
    coinbaseTx.nVersion = 1;
    coinbaseTx.nLockTime = 0;

    // Coinbase input
    CTxIn coinbaseInput;
    coinbaseInput.prevout = COutPoint();
    coinbaseInput.nSequence = CTxIn::SEQUENCE_FINAL;

    // BIP34: encode height in scriptSig
    CScript scriptSig;
    scriptSig << newHeight;
    coinbaseInput.scriptSig = scriptSig;

    coinbaseTx.vin.push_back(coinbaseInput);

    // Output: block reward
    int64_t reward = GetBlockSubsidy(params, newHeight);
    CTxOut rewardOutput(reward * SATOSHI, dogeCoinbaseScript);
    coinbaseTx.vout.push_back(rewardOutput);

    tmpl->coinbaseTx = coinbaseTx;

    // Add mempool txs (only peer-confirmed ones)
    if (dogeMempool) {
        tmpl->txs = dogeMempool->GetForTemplate(params.maxBlockWeight);
    }

    // Build header
    // DOGE AuxPoW version: (chainId << 16) | (1 << 8) | 2
    tmpl->header.nVersion =
        (static_cast<int32_t>(params.auxpowChainId) << 16) | (1 << 8) | 0x02;
    tmpl->header.hashPrevBlock = tip->hash;
    tmpl->header.hashMerkleRoot =
        ComputeMerkleRoot(tmpl->coinbaseTx, tmpl->txs);
    tmpl->header.nTime = static_cast<uint32_t>(GetTime());
    tmpl->header.nBits =
        ScryptHeaderValidator::GetNextWorkRequired(tip, params);
    tmpl->header.nNonce = 0;

    LogPrint(BCLog::MINING,
             "CreateDogeTemplate: height=%d reward=%lld nBits=%08x\n",
             newHeight, reward, tmpl->header.nBits);

    return tmpl;
}

std::shared_ptr<ScryptBlockTemplate>
ScryptTemplateBuilder::CreateLtcTemplate(ScryptHeaderChain &ltcChain,
                                         ScryptMemPool *ltcMempool,
                                         const uint256 &lotusAuxHash,
                                         const uint256 &dogeBlockHash,
                                         const CScript &ltcCoinbaseScript,
                                         size_t extranoncePlaceholderLen) {
    auto tmpl = std::make_shared<ScryptBlockTemplate>();
    const auto &params = ltcChain.GetParams();
    const auto *tip = ltcChain.GetTip();
    if (!tip) {
        return nullptr;
    }

    int newHeight = tip->height + 1;
    tmpl->height = newHeight;

    // Build multi-chain merge-mine commitment
    std::vector<AuxChainEntry> children;
    children.push_back({lotusAuxHash, LOTUS_CHAIN_ID});
    if (!dogeBlockHash.IsNull()) {
        children.push_back({dogeBlockHash, DOGE_CHAIN_ID});
    }
    tmpl->commitment = BuildMultiChainCommitment(children);

    // Build coinbase tx
    CMutableTransaction coinbaseTx;
    coinbaseTx.nVersion = 1;
    coinbaseTx.nLockTime = 0;

    CTxIn coinbaseInput;
    coinbaseInput.prevout = COutPoint();
    coinbaseInput.nSequence = CTxIn::SEQUENCE_FINAL;

    // scriptSig: BIP34 height + merge-mine commitment + extranonce placeholder
    CScript scriptSig;
    scriptSig << newHeight;

    // Append the merge-mine commitment payload
    for (uint8_t b : tmpl->commitment.coinbasePayload) {
        scriptSig << std::vector<uint8_t>{b};
    }

    // Add extranonce placeholder (zeroes)
    std::vector<uint8_t> placeholder(extranoncePlaceholderLen, 0);
    scriptSig << placeholder;

    coinbaseInput.scriptSig = scriptSig;
    coinbaseTx.vin.push_back(coinbaseInput);

    // Output: block reward
    int64_t reward = GetBlockSubsidy(params, newHeight);
    CTxOut rewardOutput(reward * SATOSHI, ltcCoinbaseScript);
    coinbaseTx.vout.push_back(rewardOutput);

    // SegWit commitment output (if LTC has SegWit)
    if (params.hasSegWit) {
        // Witness commitment: OP_RETURN + witness reserved value
        // For a coinbase-only block, the commitment is just the witness
        // reserved hash
        uint256 witnessRoot;
        uint256 witnessReserved;
        uint256 witnessCommitment =
            Hash(Span(witnessRoot), Span(witnessReserved));

        CScript commitScript;
        commitScript << OP_RETURN;
        std::vector<uint8_t> commitData(36);
        // aa21a9ed prefix
        commitData[0] = 0xaa;
        commitData[1] = 0x21;
        commitData[2] = 0xa9;
        commitData[3] = 0xed;
        std::memcpy(commitData.data() + 4, witnessCommitment.begin(), 32);
        commitScript << commitData;

        CTxOut witnessOut(0 * SATOSHI, commitScript);
        coinbaseTx.vout.push_back(witnessOut);
    }

    tmpl->coinbaseTx = coinbaseTx;

    // Add mempool txs (only peer-confirmed ones)
    if (ltcMempool) {
        tmpl->txs = ltcMempool->GetForTemplate(params.maxBlockWeight);
    }

    // Build header
    tmpl->header.nVersion = tip->nVersion;
    tmpl->header.hashPrevBlock = tip->hash;
    tmpl->header.hashMerkleRoot =
        ComputeMerkleRoot(tmpl->coinbaseTx, tmpl->txs);
    tmpl->header.nTime = static_cast<uint32_t>(GetTime());
    tmpl->header.nBits =
        ScryptHeaderValidator::GetNextWorkRequired(tip, params);
    tmpl->header.nNonce = 0;

    SplitCoinbase(tmpl->coinbaseTx, extranoncePlaceholderLen,
                  tmpl->coinbase1, tmpl->coinbase2);

    LogPrint(BCLog::MINING,
             "CreateLtcTemplate: height=%d reward=%lld nBits=%08x "
             "commitment=%d children\n",
             newHeight, reward, tmpl->header.nBits, children.size());

    return tmpl;
}

} // namespace scryptchain
