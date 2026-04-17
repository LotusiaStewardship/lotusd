// Copyright (c) 2024 The Bitcoin developers
// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <primitives/auxpow.h>

#include <consensus/params.h>
#include <hash.h>
#include <streams.h>
#include <tinyformat.h>
#include <util/system.h>

uint256 ComputeMerkleRootForBranch(uint256 hash,
                                   const std::vector<uint256> &vMerkleBranch,
                                   uint32_t nIndex) {
    for (const uint256 &merkleHash : vMerkleBranch) {
        if (nIndex & 1) {
            hash = Hash(merkleHash, hash);
        } else {
            hash = Hash(hash, merkleHash);
        }
        nIndex >>= 1;
    }
    return hash;
}

bool ParsedAuxPowCoinbase::Parse(const CScript &scriptCoinbase,
                                 uint256 hashRoot,
                                 ParsedAuxPowCoinbase &out,
                                 std::string &strError) {
    // Root hash in coinbase scriptSig is big endian
    std::reverse(hashRoot.begin(), hashRoot.end());

    CScript::const_iterator pRootHash =
        std::search(scriptCoinbase.begin(), scriptCoinbase.end(),
                    hashRoot.begin(), hashRoot.end());

    if (pRootHash == scriptCoinbase.end()) {
        strError = "AuxPow missing chain merkle root in parent coinbase";
        return false;
    }

    CScript::const_iterator pPrefix =
        std::search(scriptCoinbase.begin(), scriptCoinbase.end(),
                    MERGE_MINE_PREFIX.begin(), MERGE_MINE_PREFIX.end());

    if (pPrefix != scriptCoinbase.end()) {
        if (scriptCoinbase.end() !=
            std::search(pPrefix + 1, scriptCoinbase.end(),
                        MERGE_MINE_PREFIX.begin(), MERGE_MINE_PREFIX.end())) {
            strError = "Multiple merged mining prefixes in coinbase";
            return false;
        }

        if (pPrefix + MERGE_MINE_PREFIX.size() != pRootHash) {
            strError =
                "Merged mining prefix is not just before chain merkle root";
            return false;
        }
    } else {
        if (pRootHash - scriptCoinbase.begin() > 20) {
            strError = "AuxPow chain merkle root can have at most 20 preceding "
                       "bytes of the parent coinbase";
            return false;
        }
    }

    // Need at least 32 (root hash) + 4 (treeSize) + 4 (nonce) = 40 bytes
    ptrdiff_t remaining = scriptCoinbase.end() - pRootHash;
    if (remaining < 40) {
        strError = "AuxPow missing chain merkle tree size and nonce in "
                   "parent coinbase";
        return false;
    }

    // Skip 32 bytes for the root hash, then read treeSize and nonce
    const uint8_t *pData =
        reinterpret_cast<const uint8_t *>(&*(pRootHash + 32));

    out.nTreeSize = (uint32_t)pData[0] | ((uint32_t)pData[1] << 8) |
                    ((uint32_t)pData[2] << 16) | ((uint32_t)pData[3] << 24);

    out.nMergeMineNonce =
        (uint32_t)pData[4] | ((uint32_t)pData[5] << 8) |
        ((uint32_t)pData[6] << 16) | ((uint32_t)pData[7] << 24);

    return true;
}

uint32_t CalcExpectedMerkleTreeIndex(uint32_t nNonce, uint32_t nChainId,
                                     uint32_t merkleHeight) {
    const uint32_t TWIST_FACTOR = 1103515245;
    const uint32_t TWIST_OFFSET = 12345;

    uint32_t rand = nNonce;
    rand = rand * TWIST_FACTOR + TWIST_OFFSET;
    rand += nChainId;
    rand = rand * TWIST_FACTOR + TWIST_OFFSET;

    return rand % (1 << merkleHeight);
}

bool CAuxPow::CheckAuxBlockHash(const uint256 &hashAuxBlock,
                                uint32_t nChainId,
                                const Consensus::Params &params) const {
    if (nIndex != 0) {
        return error("%s: AuxPow nIndex must be 0", __func__);
    }

    if (VersionChainId(parentBlock.nVersion) == nChainId) {
        return error("%s: AuxPow parent has our chain ID", __func__);
    }

    if (vChainMerkleBranch.size() >= 31) {
        return error("%s: AuxPow chain merkle branch too long", __func__);
    }

    if (parentBlock.hashMerkleRoot !=
        ComputeMerkleRootForBranch(coinbaseTx->GetHash(), vMerkleBranch,
                                   nIndex)) {
        return error("%s: AuxPow merkle root incorrect", __func__);
    }

    uint256 hashRoot = ComputeMerkleRootForBranch(
        hashAuxBlock, vChainMerkleBranch, nChainIndex);

    if (coinbaseTx->vin.empty()) {
        return error("%s: AuxPow coinbase transaction missing input", __func__);
    }

    const CScript coinbaseScript = coinbaseTx->vin[0].scriptSig;

    ParsedAuxPowCoinbase parsed;
    std::string strError;
    if (!ParsedAuxPowCoinbase::Parse(coinbaseScript, hashRoot, parsed,
                                     strError)) {
        return error("%s: %s", __func__, strError);
    }

    const uint32_t merkleHeight = vChainMerkleBranch.size();
    if (parsed.nTreeSize != (1U << merkleHeight)) {
        return error(
            "%s: AuxPow merkle branch size does not match parent coinbase",
            __func__);
    }

    const uint32_t expectedIndex = CalcExpectedMerkleTreeIndex(
        parsed.nMergeMineNonce, nChainId, merkleHeight);
    if (nChainIndex != expectedIndex) {
        return error("%s: AuxPow wrong chain index", __func__);
    }

    return true;
}
