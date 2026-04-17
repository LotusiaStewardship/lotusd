// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MODULES_CHAIN_MODULE_H
#define BITCOIN_MODULES_CHAIN_MODULE_H

#include <fs.h>
#include <httpserver.h>

#include <functional>
#include <string>
#include <vector>

class CBlock;
class CBlockIndex;
class CChainParams;

namespace api {
struct QueryParams;
}
namespace util {
class Ref;
}

using ModuleRouteHandler =
    std::function<bool(const util::Ref &, HTTPRequest *,
                       const std::vector<std::string> &,
                       const api::QueryParams &)>;

using RouteAdder = std::function<void(HTTPRequest::RequestMethod,
                                      const std::string &,
                                      ModuleRouteHandler)>;

/**
 * Interface for pluggable chain-processing modules.
 *
 * Each module:
 *  - Has its own SQLite database under datadir/modules/<name>.sqlite
 *  - Receives synchronous ConnectBlock/DisconnectBlock hooks from validation
 *  - Can register its own /api/v1/{prefix}/... HTTP routes
 *  - Is self-sufficient and can be enabled/disabled independently
 */
class IChainModule {
public:
    virtual ~IChainModule() = default;

    /** Short unique name used for the SQLite filename and logging. */
    virtual std::string Name() const = 0;

    /**
     * Initialise the module. Called once during node startup.
     * @param datadir  The node's data directory (module should create
     *                 its own DB at datadir/modules/<Name()>.sqlite).
     * @param params   Chain parameters.
     * @return true on success.
     */
    virtual bool Init(const fs::path &datadir,
                      const CChainParams &params) = 0;

    /**
     * Process a newly connected block. Called synchronously inside
     * CChainState::ConnectBlock, after UTXO updates and block analytics.
     * The module must handle its own SQLite transaction boundaries.
     */
    virtual void ConnectBlock(const CBlock &block,
                              const CBlockIndex *pindex,
                              const CChainParams &params) = 0;

    /**
     * Reverse a disconnected block. Called synchronously inside
     * DisconnectBlock, before the block analytics undo.
     */
    virtual void DisconnectBlock(const CBlock &block,
                                 const CBlockIndex *pindex,
                                 const CChainParams &params) = 0;

    /**
     * Register HTTP routes for this module's API surface.
     * The addRoute callback registers under /api/v1/ automatically.
     */
    virtual void RegisterRoutes(RouteAdder addRoute) = 0;

    /** Clean up resources. Called during node shutdown. */
    virtual void Shutdown() = 0;
};

#endif // BITCOIN_MODULES_CHAIN_MODULE_H
