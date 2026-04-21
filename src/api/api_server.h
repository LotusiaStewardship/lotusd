// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_SERVER_H
#define BITCOIN_API_SERVER_H

#include <modules/chain_module.h>

#include <string>
#include <vector>

namespace util {
class Ref;
}

void StartAPI(const util::Ref &context);
void InterruptAPI();
void StopAPI();

/** Expose AddRoute for external callers (module system). */
void AddModuleRoute(HTTPRequest::RequestMethod method,
                    const std::string &prefix, ModuleRouteHandler handler);

namespace api {
struct QueryParams;

/**
 * In-process route invocation, used by the NNG RestCall tunnel.
 *
 * Looks up the best-matching route for (`method`, `parts`) in the same
 * registration table the live HTTP dispatcher uses, runs its handler
 * with the supplied request object, and returns true iff a route was
 * found and invoked. The caller is responsible for any reply/capture —
 * passing an in-memory HTTPRequest (HTTPRequest::MakeInMemory) lets the
 * NNG layer harvest the response without touching a socket.
 *
 * The response cache is intentionally bypassed: NNG callers don't
 * benefit from HTTP's stale-while-revalidate semantics and we don't
 * want to populate the cache with results from a non-HTTP origin.
 *
 * Returns false if no route matches; on handler exception, fills the
 * response with a 500 error envelope and still returns true.
 */
bool RunRouteInProcess(const util::Ref &context,
                       HTTPRequest *req,
                       HTTPRequest::RequestMethod method,
                       const std::vector<std::string> &parts,
                       const QueryParams &qp);

/** True iff a registered route matches (method, parts). */
bool RouteExists(HTTPRequest::RequestMethod method,
                 const std::vector<std::string> &parts);
} // namespace api

#endif // BITCOIN_API_SERVER_H
