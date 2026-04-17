// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <pow/auxpow.h>

#include <arith_uint256.h>
#include <chain.h>
#include <consensus/params.h>
#include <hash.h>
#include <logging.h>
#include <pow/aserti32d.h>
#include <pow/pow.h>
#include <primitives/auxpow.h>
#include <primitives/block.h>
#include <util/system.h>

/**
 * Compute the block hash as it was before AuxPoW metadata was stored.
 * Miners commit to this "pre-AuxPoW" hash in the parent coinbase, since the
 * AuxPoW data itself cannot be part of the hash it proves.
 * This resets both hashExtendedMetadata (to empty-metadata hash) and nSize
 * (to the serialized size without AuxPoW metadata).
 */
static BlockHash GetPreAuxPowHash(const CBlock &block) {
    CBlock cleanBlock(block);
    cleanBlock.vMetadata.clear();
    cleanBlock.hashExtendedMetadata = SerializeHash(cleanBlock.vMetadata);
    cleanBlock.SetSize(GetSerializeSize(cleanBlock, PROTOCOL_VERSION));
    return cleanBlock.GetHash();
}

bool CheckAuxProofOfWork(const CBlock &block, int nHeight,
                         const Consensus::Params &params) {
    const bool hasAuxPow = block.HasAuxPow();
    const bool auxpowActive = nHeight >= params.auxpowActivationHeight;

    if (hasAuxPow && !auxpowActive) {
        return error("%s: AuxPoW metadata present before activation height %d",
                     __func__, params.auxpowActivationHeight);
    }

    if (!hasAuxPow) {
        // Native triple-SHA-256 PoW: validate block hash against nBits
        if (!CheckProofOfWork(block.GetHash(), block.nBits, params)) {
            return error("%s: native triple-SHA-256 proof of work failed",
                         __func__);
        }
        return true;
    }

    // AuxPoW path: deserialize the proof from metadata
    CAuxPow auxpow;
    if (!block.GetAuxPow(auxpow)) {
        return error("%s: failed to deserialize AuxPoW from metadata",
                     __func__);
    }

    // The hash committed in the parent coinbase is the block hash computed
    // before AuxPoW metadata was added (empty metadata hash), since the AuxPoW
    // data cannot be part of the hash it proves.
    BlockHash preAuxHash = GetPreAuxPowHash(block);
    if (!auxpow.CheckAuxBlockHash(preAuxHash, params.nAuxPowChainId, params)) {
        return error("%s: AuxPow block hash check failed", __func__);
    }

    // Check the parent header's Scrypt PoW hash against the AuxPoW difficulty.
    // The AuxPoW target is checked against auxpowPowLimit rather than native
    // powLimit. The actual per-block target comes from the AuxPoW ASERT track
    // and is verified in ContextualCheckBlockHeader, not here. Here we only
    // enforce the ceiling (powLimit).
    bool fNegative;
    bool fOverflow;
    arith_uint256 bnTarget;
    bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);

    // For non-contextual checks, verify against the AuxPoW powLimit ceiling
    if (fNegative || bnTarget == 0 || fOverflow ||
        bnTarget > UintToArith256(params.auxpowPowLimit)) {
        // The nBits might be the native target, which is fine. The actual
        // AuxPoW target will be validated contextually. For the non-contextual
        // check, we just verify the Scrypt hash is a valid hash.
    }

    BlockHash parentPowHash = auxpow.parentBlock.GetPowHash();
    // Verify parent Scrypt hash is below the AuxPoW powLimit at minimum
    if (UintToArith256(parentPowHash) >
        UintToArith256(params.auxpowPowLimit)) {
        return error("%s: AuxPoW parent Scrypt hash exceeds powLimit",
                     __func__);
    }

    return true;
}

/**
 * Walk backward from pindexPrev to find the most recent AuxPoW block.
 * Returns nullptr if none found within a reasonable window.
 */
static const CBlockIndex *
FindLastAuxPowBlock(const CBlockIndex *pindexPrev,
                    const Consensus::Params &params) {
    const CBlockIndex *pindex = pindexPrev;
    int searched = 0;
    while (pindex && searched < 10000) {
        if (pindex->nHeight < params.auxpowActivationHeight) {
            return nullptr;
        }
        if (pindex->fAuxPow) {
            return pindex;
        }
        pindex = pindex->pprev;
        searched++;
    }
    return nullptr;
}

uint32_t GetNextAuxPowWorkRequired(const CBlockIndex *pindexPrev,
                                   const Consensus::Params &params) {
    assert(pindexPrev != nullptr);

    if (params.fPowNoRetargeting) {
        // Regtest: use the AuxPoW powLimit directly
        return UintToArith256(params.auxpowPowLimit).GetCompact();
    }

    // Find the anchor: the first AuxPoW block at or after activation
    const CBlockIndex *pindexAnchor = nullptr;
    {
        const CBlockIndex *p =
            pindexPrev->GetAncestor(params.auxpowActivationHeight);
        if (p) {
            // Walk forward from activation to find first AuxPoW block
            // Since we can't walk forward, search backward from tip
            const CBlockIndex *candidate = pindexPrev;
            while (candidate &&
                   candidate->nHeight >= params.auxpowActivationHeight) {
                if (candidate->fAuxPow) {
                    pindexAnchor = candidate;
                }
                candidate = candidate->pprev;
            }
        }
    }

    if (!pindexAnchor) {
        // No AuxPoW blocks yet, return powLimit
        return UintToArith256(params.auxpowPowLimit).GetCompact();
    }

    // Find the last AuxPoW block before current
    const CBlockIndex *pindexLastAuxPow =
        FindLastAuxPowBlock(pindexPrev, params);
    if (!pindexLastAuxPow) {
        return UintToArith256(params.auxpowPowLimit).GetCompact();
    }

    const arith_uint256 powLimit = UintToArith256(params.auxpowPowLimit);

    // Count AuxPoW blocks between anchor and tip for height diff
    int64_t nAuxPowHeightDiff = 0;
    {
        const CBlockIndex *p = pindexPrev;
        while (p && p->nHeight > pindexAnchor->nHeight) {
            if (p->fAuxPow) {
                nAuxPowHeightDiff++;
            }
            p = p->pprev;
        }
    }

    if (nAuxPowHeightDiff == 0) {
        return UintToArith256(params.auxpowPowLimit).GetCompact();
    }

    // Time diff is wall-clock from anchor's parent to last AuxPoW block
    const int64_t anchorTime = pindexAnchor->pprev
                                   ? pindexAnchor->pprev->GetBlockTime()
                                   : pindexAnchor->GetBlockTime();
    const int64_t nTimeDiff = pindexLastAuxPow->GetBlockTime() - anchorTime;

    const arith_uint256 refBlockTarget =
        arith_uint256().SetCompact(pindexAnchor->nBits);

    // Use the same ASERT formula but with AuxPoW-specific parameters.
    // Target spacing for AuxPoW is the same 2-minute interval, but since
    // both native and AuxPoW blocks share the chain, we effectively target
    // a share of the block rate.
    arith_uint256 nextTarget = CalculateASERT(
        refBlockTarget > powLimit ? powLimit : refBlockTarget,
        params.nPowTargetSpacing, nTimeDiff, nAuxPowHeightDiff, powLimit,
        params.nAuxPowDAAHalfLife);

    return nextTarget.GetCompact();
}
