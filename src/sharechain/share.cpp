// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sharechain/share.h>

#include <hash.h>

namespace sharechain {

uint256 CShare::GetHash() const {
    return SerializeHash(*this);
}

} // namespace sharechain
