// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <auxmining/auxmining.h>
#include <hash.h>
#include <span.h>

MergeMineCommitment BuildMergeMineCommitment(const uint256 &auxBlockHash,
                                              uint32_t nChainId) {
    MergeMineCommitment commitment;

    // Single-chain case: tree of size 1, index 0, no branch
    commitment.nTreeSize = 1;
    commitment.nMergeMineNonce = 0;
    commitment.nChainIndex = 0;
    commitment.chainMerkleRoot = auxBlockHash;

    // Build coinbase payload: MERGE_MINE_PREFIX + root (big-endian) +
    // treeSize (4 bytes LE) + nonce (4 bytes LE)
    commitment.coinbasePayload.clear();
    commitment.coinbasePayload.insert(commitment.coinbasePayload.end(),
                                       MERGE_MINE_PREFIX.begin(),
                                       MERGE_MINE_PREFIX.end());

    // Root hash in big-endian (reversed)
    uint256 rootBE = commitment.chainMerkleRoot;
    std::reverse(rootBE.begin(), rootBE.end());
    commitment.coinbasePayload.insert(commitment.coinbasePayload.end(),
                                       rootBE.begin(), rootBE.end());

    // Tree size (4 bytes, little-endian)
    uint32_t ts = commitment.nTreeSize;
    commitment.coinbasePayload.push_back(ts & 0xff);
    commitment.coinbasePayload.push_back((ts >> 8) & 0xff);
    commitment.coinbasePayload.push_back((ts >> 16) & 0xff);
    commitment.coinbasePayload.push_back((ts >> 24) & 0xff);

    // Merge nonce (4 bytes, little-endian)
    uint32_t mn = commitment.nMergeMineNonce;
    commitment.coinbasePayload.push_back(mn & 0xff);
    commitment.coinbasePayload.push_back((mn >> 8) & 0xff);
    commitment.coinbasePayload.push_back((mn >> 16) & 0xff);
    commitment.coinbasePayload.push_back((mn >> 24) & 0xff);

    return commitment;
}
