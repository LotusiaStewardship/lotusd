// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_SERVER_H
#define BITCOIN_API_SERVER_H

#include <modules/chain_module.h>

namespace util {
class Ref;
}

void StartAPI(const util::Ref &context);
void InterruptAPI();
void StopAPI();

/** Expose AddRoute for external callers (module system). */
void AddModuleRoute(HTTPRequest::RequestMethod method,
                    const std::string &prefix, ModuleRouteHandler handler);

#endif // BITCOIN_API_SERVER_H
