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
#include <map>
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
 * Entry for a child chain in a multi-chain merge-mine commitment.
 */
struct AuxChainEntry {
    uint256 blockHash;
    uint32_t chainId;
};

/**
 * Per-chain result from BuildMultiChainCommitment.
 */
struct ChainMerklePath {
    uint32_t nChainIndex;
    std::vector<uint256> chainMerkleBranch;
};

/**
 * Result of building a multi-chain merge-mine commitment.
 */
struct MultiChainCommitment {
    std::vector<uint8_t> coinbasePayload;
    uint256 chainMerkleRoot;
    uint32_t nTreeSize;
    uint32_t nMergeMineNonce;
    std::map<uint32_t, ChainMerklePath> perChain; // chainId -> merkle path
};

/**
 * Build the merge-mine commitment data for embedding in a parent coinbase.
 * Single-chain variant (backward compatible).
 */
MergeMineCommitment
BuildMergeMineCommitment(const uint256 &auxBlockHash, uint32_t nChainId);

/**
 * Build a multi-chain merge-mine commitment for N child chains.
 * Finds a nonce that places each chain at its CalcExpectedMerkleTreeIndex slot.
 * Returns commitment data with per-chain merkle branches.
 */
MultiChainCommitment
BuildMultiChainCommitment(const std::vector<AuxChainEntry> &children);

#endif // BITCOIN_AUXMINING_AUXMINING_H
