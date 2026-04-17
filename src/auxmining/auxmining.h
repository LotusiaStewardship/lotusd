// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_AUXMINING_AUXMINING_H
#define BITCOIN_AUXMINING_AUXMINING_H

#include <primitives/auxpow.h>
#include <primitives/block.h>
#include <script/script.h>
#include <uint256.h>

#include <cstdint>
#include <vector>

/**
 * Data to embed in the parent chain's coinbase transaction for merge-mining.
 */
struct MergeMineCommitment {
    /** Full payload: FABE6D6D + root (big-endian) + treeSize(LE) + nonce(LE) */
    std::vector<uint8_t> coinbasePayload;
    uint256 chainMerkleRoot;
    uint32_t nTreeSize;
    uint32_t nMergeMineNonce;
    uint32_t nChainIndex;
    std::vector<uint256> chainMerkleBranch;
};

/**
 * Build the merge-mine commitment data for embedding in a parent coinbase.
 * @param auxBlockHash The Lotus block hash to commit to.
 * @param nChainId The Lotus chain ID.
 */
MergeMineCommitment
BuildMergeMineCommitment(const uint256 &auxBlockHash, uint32_t nChainId);

#endif // BITCOIN_AUXMINING_AUXMINING_H
