// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <modules/module_registry.h>
#include <modules/rank/rank_module.h>

void RegisterBuiltinModules(CModuleRegistry &registry) {
    registry.Register(std::make_unique<CRankModule>());
}
