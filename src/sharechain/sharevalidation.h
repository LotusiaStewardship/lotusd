// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SHARECHAIN_SHAREVALIDATION_H
#define BITCOIN_SHARECHAIN_SHAREVALIDATION_H

#include <sharechain/share.h>
#include <sharechain/sharechain.h>

#include <string>

class CChainState;
class CChainParams;
class CBlockIndex;

namespace sharechain {

enum class ShareValidationResult {
    VALID,
    INVALID_POW,
    UNKNOWN_PREV_SHARE,
    INVALID_MAIN_CHAIN_REF,
    DUPLICATE,
    INVALID_PAYOUT_ROOT,
    INVALID_HEIGHT,
};

/**
 * Validate a share received from the P2P network.
 * Checks:
 * - PoW meets share target (native triple-SHA-256 or AuxPoW)
 * - hashPrevShare points to a known share
 * - lotusHeader references a valid main chain tip or recent ancestor
 * - hashPayoutRoot matches expected payout window
 * - Share is not a duplicate
 */
ShareValidationResult ValidateShareForP2P(const CShare &share,
                                           const ShareChain &chain,
                                           const CChainState &chainstate,
                                           const CChainParams &chainParams,
                                           std::string &error);

/**
 * Returns the misbehavior penalty score for a given validation failure.
 */
int GetShareMisbehaviorScore(ShareValidationResult result);

} // namespace sharechain

#endif // BITCOIN_SHARECHAIN_SHAREVALIDATION_H
