// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SHARECHAIN_SHAREPAYOUT_H
#define BITCOIN_SHARECHAIN_SHAREPAYOUT_H

#include <amount.h>
#include <primitives/transaction.h>
#include <script/script.h>

#include <vector>

namespace sharechain {

class ShareChain;

/**
 * Build proportional payout outputs from the share chain window.
 * This is called during block template construction to replace the
 * single miner payout with multiple outputs proportional to share
 * contributions.
 *
 * Sub-dust payouts are redirected to the minerFundScript.
 *
 * @param chain The share chain with the current payout window.
 * @param totalMinerReward The total reward that goes to miners
 *        (block subsidy + fees - miner fund).
 * @param minerFundScript The script for the miner fund (sub-dust redirect).
 * @return Vector of CTxOut for the coinbase transaction.
 */
std::vector<CTxOut> BuildShareChainPayoutOutputs(
    const ShareChain &chain, Amount totalMinerReward,
    const CScript &minerFundScript);

/**
 * Modify a coinbase transaction to include share chain payout outputs.
 * Replaces the single miner output (vout[1]) with proportional payouts.
 *
 * @param coinbaseTx The mutable coinbase transaction to modify.
 * @param chain The share chain with the current payout window.
 * @param minerFundScript The script for sub-dust redirect.
 */
void ApplyShareChainPayouts(CMutableTransaction &coinbaseTx,
                            const ShareChain &chain,
                            const CScript &minerFundScript);

} // namespace sharechain

#endif // BITCOIN_SHARECHAIN_SHAREPAYOUT_H
