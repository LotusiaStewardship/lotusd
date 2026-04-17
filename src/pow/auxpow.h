// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_POW_AUXPOW_H
#define BITCOIN_POW_AUXPOW_H

#include <cstdint>

class CBlock;
class CBlockIndex;

namespace Consensus {
struct Params;
}

/**
 * Check proof-of-work for a Lotus block, supporting both native triple-SHA-256
 * and AuxPoW (Scrypt via Dogecoin parent chain).
 *
 * - If the block has no AuxPoW metadata: validates using native PoW against
 *   nBits.
 * - If the block has AuxPoW metadata and height >= activation: validates the
 *   AuxPoW proof (commitment in parent coinbase, parent Scrypt hash against
 *   the AuxPoW difficulty target).
 * - If AuxPoW metadata is present before activation: rejects.
 */
bool CheckAuxProofOfWork(const CBlock &block, int nHeight,
                         const Consensus::Params &params);

/**
 * Compute the AuxPoW difficulty target for the next block, using a separate
 * ASERT instance that only considers AuxPoW blocks.
 *
 * Returns the compact target (nBits format) for the AuxPoW track.
 */
uint32_t GetNextAuxPowWorkRequired(const CBlockIndex *pindexPrev,
                                   const Consensus::Params &params);

#endif // BITCOIN_POW_AUXPOW_H
