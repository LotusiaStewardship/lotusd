// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sharechain/sharevalidation.h>

#include <arith_uint256.h>
#include <chainparams.h>
#include <validation.h>

namespace sharechain {

ShareValidationResult ValidateShareForP2P(const CShare &share,
                                           const ShareChain &chain,
                                           const CChainState &chainstate,
                                           const CChainParams &chainParams,
                                           std::string &error) {
    // Duplicate check
    if (chain.HasShare(share.GetHash())) {
        error = "Duplicate share";
        return ShareValidationResult::DUPLICATE;
    }

    // Check prev share linkage (unless genesis share)
    if (chain.GetShareHeight() > 0 &&
        !share.hashPrevShare.IsNull() &&
        !chain.HasShare(share.hashPrevShare)) {
        error = "Unknown previous share";
        return ShareValidationResult::UNKNOWN_PREV_SHARE;
    }

    // Verify PoW meets share target
    BlockHash powHash = share.lotusHeader.GetHash();
    arith_uint256 hashValue = UintToArith256(powHash);

    if (share.nShareBits > 0) {
        arith_uint256 shareTarget;
        shareTarget.SetCompact(share.nShareBits);
        if (hashValue > shareTarget) {
            error = "Share PoW does not meet share target";
            return ShareValidationResult::INVALID_POW;
        }
    }

    // Verify the Lotus header references a known main chain block
    {
        LOCK(cs_main);
        const CBlockIndex *tip = chainstate.m_chain.Tip();
        if (!tip) {
            error = "No main chain tip";
            return ShareValidationResult::INVALID_MAIN_CHAIN_REF;
        }

        bool foundPrev = false;
        const CBlockIndex *pindex = tip;
        int lookback = 10;
        while (pindex && lookback > 0) {
            if (pindex->GetBlockHash() ==
                share.lotusHeader.hashPrevBlock) {
                foundPrev = true;
                break;
            }
            pindex = pindex->pprev;
            lookback--;
        }

        if (!foundPrev) {
            error = "Share header does not reference a recent main chain block";
            return ShareValidationResult::INVALID_MAIN_CHAIN_REF;
        }
    }

    // Height check
    if (share.nShareHeight == 0 && chain.GetShareHeight() > 0) {
        error = "Invalid share height";
        return ShareValidationResult::INVALID_HEIGHT;
    }

    return ShareValidationResult::VALID;
}

int GetShareMisbehaviorScore(ShareValidationResult result) {
    switch (result) {
        case ShareValidationResult::INVALID_POW:
            return 20;
        case ShareValidationResult::UNKNOWN_PREV_SHARE:
            return 0;
        case ShareValidationResult::DUPLICATE:
            return 0;
        case ShareValidationResult::INVALID_MAIN_CHAIN_REF:
            return 5;
        case ShareValidationResult::INVALID_PAYOUT_ROOT:
            return 10;
        case ShareValidationResult::INVALID_HEIGHT:
            return 10;
        case ShareValidationResult::VALID:
            return 0;
    }
    return 0;
}

} // namespace sharechain
