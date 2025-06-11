// Copyright (c) 2024 The Lotus Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VALIDATION_SCOPE_H
#define BITCOIN_VALIDATION_SCOPE_H

#include <script/script.h>
#include <validation.h>

/**
 * RAII wrapper for script checks to ensure proper cleanup
 */
class ScriptCheckScope {
    std::vector<CScriptCheck> &checks;

public:
    explicit ScriptCheckScope(std::vector<CScriptCheck> &c) : checks(c) {}
    ~ScriptCheckScope() { checks.clear(); }
};

/**
 * RAII wrapper for MemPoolAccept workspace to ensure proper cleanup
 */
class WorkspaceScope {
    MemPoolAccept::Workspace &ws;

public:
    explicit WorkspaceScope(MemPoolAccept::Workspace &w) : ws(w) {}
    ~WorkspaceScope() {
        ws.m_ancestors.clear();
        ws.m_entry.reset();
    }
};

#endif // BITCOIN_VALIDATION_SCOPE_H