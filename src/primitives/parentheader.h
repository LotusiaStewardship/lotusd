// Copyright (c) 2024 The Bitcoin developers
// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_PRIMITIVES_PARENTHEADER_H
#define BITCOIN_PRIMITIVES_PARENTHEADER_H

#include <primitives/blockhash.h>
#include <serialize.h>
#include <uint256.h>

/**
 * A standard 80-byte Bitcoin/Dogecoin block header used as the parent chain
 * header in AuxPoW merged mining. This breaks the cyclic dependency between
 * auxpow (referencing a parent block header) and the Lotus block header
 * (referencing an auxpow via metadata).
 *
 * The parent block does not carry auxpow data itself.
 */
class CParentBlockHeader {
public:
    int32_t nVersion;
    BlockHash hashPrevBlock;
    uint256 hashMerkleRoot;
    uint32_t nTime;
    uint32_t nBits;
    uint32_t nNonce;

    CParentBlockHeader() { SetNull(); }

    SERIALIZE_METHODS(CParentBlockHeader, obj) {
        READWRITE(obj.nVersion, obj.hashPrevBlock, obj.hashMerkleRoot,
                  obj.nTime, obj.nBits, obj.nNonce);
    }

    void SetNull() {
        nVersion = 0;
        hashPrevBlock = BlockHash();
        hashMerkleRoot.SetNull();
        nTime = 0;
        nBits = 0;
        nNonce = 0;
    }

    bool IsNull() const { return (nBits == 0); }

    /** Double-SHA256 block identity hash. */
    BlockHash GetHash() const;

    /** Scrypt PoW hash used by Dogecoin/Litecoin parent chains. */
    BlockHash GetPowHash() const;

    int64_t GetBlockTime() const { return (int64_t)nTime; }
};

#endif // BITCOIN_PRIMITIVES_PARENTHEADER_H
