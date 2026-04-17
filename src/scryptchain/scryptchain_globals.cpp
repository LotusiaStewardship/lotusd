// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <scryptchain/scryptchain_globals.h>

namespace scryptchain {

ScryptChainGlobals &GetScryptChainGlobals() {
    static ScryptChainGlobals instance;
    return instance;
}

} // namespace scryptchain
