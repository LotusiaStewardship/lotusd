// Copyright (c) 2024 The Bitcoin developers
// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_AUXPOW_H
#define BITCOIN_PRIMITIVES_AUXPOW_H

#include <cstdint>
#include <primitives/parentheader.h>
#include <primitives/transaction.h>

namespace Consensus {
struct Params;
} // namespace Consensus

/** Bit that indicates a block has auxiliary PoW (in the parent chain's
 * nVersion). Bits below that are interpreted as "traditional" version. */
static constexpr int32_t VERSION_AUXPOW_BIT_POS = 8;
static constexpr int32_t VERSION_AUXPOW_BIT = 1 << VERSION_AUXPOW_BIT_POS;

/** Position of the bits reserved for the auxpow chain ID. */
static constexpr int32_t VERSION_CHAIN_ID_BIT_POS = 16;

/** Chain ID used by Lotus for merged mining. */
static constexpr uint32_t AUXPOW_CHAIN_ID = 0x4C; // 'L'

/** Max allowed chain ID */
static constexpr uint32_t MAX_ALLOWED_CHAIN_ID =
    (1 << (32 - VERSION_CHAIN_ID_BIT_POS)) - 1;

/** 4-byte prefix for merge-mining data in the coinbase. */
static const std::array<uint8_t, 4> MERGE_MINE_PREFIX{{0xfa, 0xbe, 'm', 'm'}};

/**
 * Extract the chain ID from a parent block's nVersion.
 */
inline uint32_t VersionChainId(int32_t nVersion) {
    return uint32_t(nVersion) >> VERSION_CHAIN_ID_BIT_POS;
}

/**
 * Like ComputeMerkleRoot, but for a leaf with its merkle branch and index.
 */
uint256 ComputeMerkleRootForBranch(uint256 hash,
                                   const std::vector<uint256> &vMerkleBranch,
                                   uint32_t nIndex);

/**
 * Choose a pseudo-random slot in the chain merkle tree, fixed for a given
 * size/nonce/chain combination.
 */
uint32_t CalcExpectedMerkleTreeIndex(uint32_t nNonce, uint32_t nChainId,
                                     uint32_t merkleHeight);

/** Parsed data from an AuxPow coinbase */
class ParsedAuxPowCoinbase {
public:
    uint32_t nTreeSize;
    uint32_t nMergeMineNonce;

    /**
     * Parse a coinbase of another blockchain for AuxPow data.
     * Returns true on success and populates the output fields.
     */
    static bool Parse(const CScript &scriptCoinbase, uint256 hashRoot,
                      ParsedAuxPowCoinbase &out, std::string &strError);
};

/**
 * Data for the merge-mining auxpow. This is the parent block's coinbase tx
 * that can be verified to be in the parent block, and this transaction's
 * input (the coinbase script) contains the reference to the actual
 * merge-mined block.
 */
class CAuxPow {
public:
    /** The coinbase tx of the parent block encoding the merge-mined block */
    CTransactionRef coinbaseTx;
    uint256 hashBlock;
    std::vector<uint256> vMerkleBranch;
    /** Index of the tx in the block, must always be 0 (i.e. coinbase). */
    uint32_t nIndex;

    /** The merkle branch connecting the aux block to our coinbase. */
    std::vector<uint256> vChainMerkleBranch;

    /** Merkle tree index of the aux block header in the coinbase. */
    uint32_t nChainIndex;

    /** Parent block header (on which the real PoW is done). */
    CParentBlockHeader parentBlock;

    CAuxPow() : nIndex(0), nChainIndex(0) {}

    SERIALIZE_METHODS(CAuxPow, obj) {
        READWRITE(obj.coinbaseTx, obj.hashBlock, obj.vMerkleBranch, obj.nIndex,
                  obj.vChainMerkleBranch, obj.nChainIndex, obj.parentBlock);
    }

    /**
     * Perform all the required AuxPow checks.
     *
     * Verifies that hashAuxBlock is correctly committed in coinbaseTx via a
     * merkle tree in the scriptSig. The leaf index is given by
     * CalcExpectedMerkleTreeIndex.
     *
     * Returns true on success, false on failure (with error logged).
     */
    bool CheckAuxBlockHash(const uint256 &hashAuxBlock, uint32_t nChainId,
                           const Consensus::Params &params) const;
};

#endif // BITCOIN_PRIMITIVES_AUXPOW_H
