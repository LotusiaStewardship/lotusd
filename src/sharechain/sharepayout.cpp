// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sharechain/sharepayout.h>
#include <sharechain/sharechain.h>

#include <logging.h>

namespace sharechain {

std::vector<CTxOut> BuildShareChainPayoutOutputs(
    const ShareChain &chain, Amount totalMinerReward,
    const CScript &minerFundScript) {
    return chain.BuildPayoutOutputs(totalMinerReward, minerFundScript);
}

void ApplyShareChainPayouts(CMutableTransaction &coinbaseTx,
                            const ShareChain &chain,
                            const CScript &minerFundScript) {
    // vout[0] = OP_RETURN height (untouched)
    // vout[1] = miner payout (replaced with proportional payouts)
    // vout[2+] = miner fund outputs (untouched)

    if (coinbaseTx.vout.size() < 2) {
        return;
    }

    Amount minerReward = coinbaseTx.vout[1].nValue;
    CScript origMinerScript = coinbaseTx.vout[1].scriptPubKey;

    auto payoutOutputs =
        BuildShareChainPayoutOutputs(chain, minerReward, minerFundScript);

    if (payoutOutputs.empty()) {
        // No shares in window -- keep original single payout
        return;
    }

    // Verify total payout matches miner reward (rounding is handled
    // by redirecting remainder to first output)
    Amount totalPayout = Amount::zero();
    for (const auto &out : payoutOutputs) {
        totalPayout += out.nValue;
    }

    Amount remainder = minerReward - totalPayout;
    if (remainder > Amount::zero() && !payoutOutputs.empty()) {
        payoutOutputs[0].nValue += remainder;
    }

    // Replace vout[1] with the first payout, append the rest
    coinbaseTx.vout[1] = payoutOutputs[0];

    // Insert remaining payout outputs after vout[1], before miner fund
    for (size_t i = 1; i < payoutOutputs.size(); ++i) {
        coinbaseTx.vout.insert(coinbaseTx.vout.begin() + 2 + (i - 1),
                               payoutOutputs[i]);
    }

    LogPrint(BCLog::MINING,
             "ShareChain: applied %d payout outputs, total=%d\n",
             payoutOutputs.size(), minerReward / SATOSHI);
}

} // namespace sharechain
