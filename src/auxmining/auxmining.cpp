// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <auxmining/auxmining.h>
#include <hash.h>
#include <logging.h>
#include <span.h>

#include <algorithm>
#include <set>

static void SerializeCoinbasePayload(std::vector<uint8_t> &out,
                                     const uint256 &root, uint32_t treeSize,
                                     uint32_t nonce) {
    out.clear();
    out.insert(out.end(), MERGE_MINE_PREFIX.begin(), MERGE_MINE_PREFIX.end());

    uint256 rootBE = root;
    std::reverse(rootBE.begin(), rootBE.end());
    out.insert(out.end(), rootBE.begin(), rootBE.end());

    out.push_back(treeSize & 0xff);
    out.push_back((treeSize >> 8) & 0xff);
    out.push_back((treeSize >> 16) & 0xff);
    out.push_back((treeSize >> 24) & 0xff);

    out.push_back(nonce & 0xff);
    out.push_back((nonce >> 8) & 0xff);
    out.push_back((nonce >> 16) & 0xff);
    out.push_back((nonce >> 24) & 0xff);
}

static void ExtractMerkleBranch(const std::vector<uint256> &leaves,
                                uint32_t index,
                                std::vector<uint256> &branch,
                                uint256 &root) {
    branch.clear();
    std::vector<uint256> level = leaves;
    size_t idx = index;

    while (level.size() > 1) {
        size_t siblingIdx = idx ^ 1;
        if (siblingIdx < level.size()) {
            branch.push_back(level[siblingIdx]);
        } else {
            branch.push_back(level[idx]);
        }
        std::vector<uint256> nextLevel;
        for (size_t i = 0; i < level.size(); i += 2) {
            const uint256 &left = level[i];
            const uint256 &right =
                (i + 1 < level.size()) ? level[i + 1] : left;
            nextLevel.push_back(Hash(Span(left), Span(right)));
        }
        level = nextLevel;
        idx /= 2;
    }
    root = level[0];
}

MergeMineCommitment BuildMergeMineCommitment(const uint256 &auxBlockHash,
                                              uint32_t nChainId) {
    MergeMineCommitment commitment;

    commitment.nTreeSize = 1;
    commitment.nMergeMineNonce = 0;
    commitment.nChainIndex = 0;
    commitment.chainMerkleRoot = auxBlockHash;

    SerializeCoinbasePayload(commitment.coinbasePayload,
                             commitment.chainMerkleRoot, commitment.nTreeSize,
                             commitment.nMergeMineNonce);

    return commitment;
}

MultiChainCommitment
BuildMultiChainCommitment(const std::vector<AuxChainEntry> &children) {
    MultiChainCommitment result;

    if (children.empty()) {
        result.nTreeSize = 0;
        result.nMergeMineNonce = 0;
        return result;
    }

    if (children.size() == 1) {
        result.nTreeSize = 1;
        result.nMergeMineNonce = 0;
        result.chainMerkleRoot = children[0].blockHash;
        result.perChain[children[0].chainId] = {0, {}};
        SerializeCoinbasePayload(result.coinbasePayload,
                                 result.chainMerkleRoot, result.nTreeSize,
                                 result.nMergeMineNonce);
        return result;
    }

    // Compute tree size (smallest power of 2 >= children.size())
    uint32_t treeSize = 1;
    uint32_t merkleHeight = 0;
    while (treeSize < children.size()) {
        treeSize <<= 1;
        merkleHeight++;
    }

    // Find a nonce where CalcExpectedMerkleTreeIndex gives unique slots
    // for every child chain
    uint32_t nonce = 0;
    std::map<uint32_t, uint32_t> chainSlots; // chainId -> slot

    for (nonce = 0; nonce < 0xFFFFFFFF; ++nonce) {
        chainSlots.clear();
        std::set<uint32_t> usedSlots;
        bool valid = true;

        for (const auto &child : children) {
            uint32_t slot = CalcExpectedMerkleTreeIndex(
                nonce, child.chainId, merkleHeight);
            if (usedSlots.count(slot)) {
                valid = false;
                break;
            }
            usedSlots.insert(slot);
            chainSlots[child.chainId] = slot;
        }

        if (valid) {
            break;
        }
    }

    result.nTreeSize = treeSize;
    result.nMergeMineNonce = nonce;

    // Build leaves array (zero-padded to treeSize)
    std::vector<uint256> leaves(treeSize);
    for (const auto &child : children) {
        uint32_t slot = chainSlots[child.chainId];
        leaves[slot] = child.blockHash;
    }

    // Extract merkle branch for each child
    for (const auto &child : children) {
        uint32_t slot = chainSlots[child.chainId];
        ChainMerklePath path;
        path.nChainIndex = slot;
        ExtractMerkleBranch(leaves, slot, path.chainMerkleBranch,
                            result.chainMerkleRoot);
        result.perChain[child.chainId] = path;
    }

    SerializeCoinbasePayload(result.coinbasePayload, result.chainMerkleRoot,
                             result.nTreeSize, result.nMergeMineNonce);

    LogPrint(BCLog::MINING,
             "BuildMultiChainCommitment: %d children, treeSize=%u, nonce=%u\n",
             children.size(), treeSize, nonce);

    return result;
}
