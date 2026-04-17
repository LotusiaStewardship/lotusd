// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SHARECHAIN_SHARE_H
#define BITCOIN_SHARECHAIN_SHARE_H

#include <primitives/block.h>
#include <script/script.h>
#include <serialize.h>
#include <uint256.h>

namespace sharechain {

class CShare {
public:
    uint256 hashPrevShare;
    CBlockHeader lotusHeader;
    bool fAuxPow = false;
    CScript scriptPubKey;
    uint32_t nShareBits = 0;
    uint32_t nShareHeight = 0;
    uint256 hashPayoutRoot;
    int64_t nTime = 0;
    double nDifficulty = 0;

    SERIALIZE_METHODS(CShare, obj) {
        READWRITE(obj.hashPrevShare);
        READWRITE(obj.lotusHeader);
        READWRITE(obj.fAuxPow);
        READWRITE(obj.scriptPubKey);
        READWRITE(obj.nShareBits);
        READWRITE(obj.nShareHeight);
        READWRITE(obj.hashPayoutRoot);
        READWRITE(obj.nTime);
    }

    uint256 GetHash() const;

    bool IsNull() const {
        return hashPrevShare.IsNull() && nShareHeight == 0;
    }
};

} // namespace sharechain

#endif // BITCOIN_SHARECHAIN_SHARE_H
