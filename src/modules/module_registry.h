// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MODULES_MODULE_REGISTRY_H
#define BITCOIN_MODULES_MODULE_REGISTRY_H

#include <modules/chain_module.h>

#include <memory>
#include <vector>

/**
 * Registry that owns all IChainModule instances and dispatches
 * lifecycle and chain events to them.
 */
class CModuleRegistry {
public:
    /** Takes ownership of the module. */
    void Register(std::unique_ptr<IChainModule> module);

    /**
     * Initialise every registered module.
     * @return true if all modules initialised successfully.
     */
    bool InitAll(const fs::path &datadir, const CChainParams &params);

    /** Dispatch ConnectBlock to every module (in registration order). */
    void ConnectBlock(const CBlock &block, const CBlockIndex *pindex,
                      const CChainParams &params);

    /** Dispatch DisconnectBlock to every module (reverse registration order). */
    void DisconnectBlock(const CBlock &block, const CBlockIndex *pindex,
                         const CChainParams &params);

    /** Let each module register its API routes. */
    void RegisterAllRoutes(RouteAdder addRoute);

    /** Shut down all modules (reverse order). */
    void ShutdownAll();

    size_t Count() const { return m_modules.size(); }

private:
    std::vector<std::unique_ptr<IChainModule>> m_modules;
};

extern std::unique_ptr<CModuleRegistry> g_module_registry;

#endif // BITCOIN_MODULES_MODULE_REGISTRY_H
