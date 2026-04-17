// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRYPTCHAIN_SCRYPTCHAIN_GLOBALS_H
#define BITCOIN_SCRYPTCHAIN_SCRYPTCHAIN_GLOBALS_H

namespace scryptchain {

class ScryptHeaderChain;
class ScryptNetworkManager;
class ScryptMemPool;

struct ScryptChainGlobals {
    ScryptHeaderChain *ltcChain{nullptr};
    ScryptHeaderChain *dogeChain{nullptr};
    ScryptNetworkManager *ltcNetMgr{nullptr};
    ScryptNetworkManager *dogeNetMgr{nullptr};
    ScryptMemPool *ltcMempool{nullptr};
    ScryptMemPool *dogeMempool{nullptr};
};

ScryptChainGlobals &GetScryptChainGlobals();

} // namespace scryptchain

#endif // BITCOIN_SCRYPTCHAIN_SCRYPTCHAIN_GLOBALS_H
