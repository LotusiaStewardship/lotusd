// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <modules/module_registry.h>

#include <logging.h>

std::unique_ptr<CModuleRegistry> g_module_registry;

void CModuleRegistry::Register(std::unique_ptr<IChainModule> module) {
    m_modules.push_back(std::move(module));
}

bool CModuleRegistry::InitAll(const fs::path &datadir,
                              const CChainParams &params) {
    fs::path modulesDir = datadir / "modules";
    fs::create_directories(modulesDir);

    std::string names;
    for (auto &mod : m_modules) {
        if (!mod->Init(modulesDir, params)) {
            LogPrintf("❌ Module %s: failed to initialize\n",
                      mod->Name());
            return false;
        }
        if (!names.empty()) names += ", ";
        names += mod->Name();
    }
    if (!names.empty()) {
        LogPrintf("🧩 Modules: %s\n", names);
    }
    return true;
}

void CModuleRegistry::ConnectBlock(const CBlock &block,
                                   const CBlockIndex *pindex,
                                   const CChainParams &params) {
    for (auto &mod : m_modules) {
        mod->ConnectBlock(block, pindex, params);
    }
}

void CModuleRegistry::DisconnectBlock(const CBlock &block,
                                      const CBlockIndex *pindex,
                                      const CChainParams &params) {
    for (auto it = m_modules.rbegin(); it != m_modules.rend(); ++it) {
        (*it)->DisconnectBlock(block, pindex, params);
    }
}

void CModuleRegistry::RegisterAllRoutes(RouteAdder addRoute) {
    for (auto &mod : m_modules) {
        mod->RegisterRoutes(addRoute);
    }
}

void CModuleRegistry::ShutdownAll() {
    for (auto it = m_modules.rbegin(); it != m_modules.rend(); ++it) {
        (*it)->Shutdown();
    }
    m_modules.clear();
}
