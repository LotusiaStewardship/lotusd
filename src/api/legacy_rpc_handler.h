// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_LEGACY_RPC_HANDLER_H
#define BITCOIN_API_LEGACY_RPC_HANDLER_H

#include <api/api_util.h>
#include <httpserver.h>

#include <univalue.h>

#include <string>
#include <unordered_set>
#include <vector>

namespace util {
class Ref;
}

namespace api {

/**
 * Generic, unauthenticated dispatcher for legacy JSON-RPC methods.
 *
 * This sits in front of `tableRPC` and only allows methods that are on a
 * strict allowlist. The allowlist contains:
 *
 *   * Read-only queries (no wallet, no mining template, no peer-set
 *     mutation, no operator-only knob).
 *
 *   * Consensus-validated broadcasts — `sendrawtransaction`, `submitblock`,
 *     `submitheader`, `sendavalancheproof`. These are safe to expose
 *     without auth because their admissibility is decided by the network's
 *     consensus / standardness rules, not by a privileged credential on
 *     this node. The worst an unauthenticated caller can do is relay
 *     garbage that the network already has to reject.
 *
 * Operator / wallet mutations (addnode, setban, generate*, walletpassphrase,
 * finalize/invalidate/parkblock, …) stay on the authenticated JSON-RPC
 * interface and will be rejected here with HTTP 403.
 *
 * It is intended as a public REST surface so that wallet clients
 * (e.g. CryptoWalletExpo) can call `estimatefee`, `decoderawtransaction`,
 * `getrawtransaction`, `sendrawtransaction` and friends without ever
 * holding an `rpcuser:rpcpassword` credential.
 *
 * Two routes are exposed (registered by api_server.cpp):
 *
 *   GET  /api/v1/rpc/<method>?p1=v1&p2=v2 ...
 *        Positional or named query-string parameters. Each value is fed
 *        through the same string -> JSON conversion that the
 *        `bitcoin-cli` client uses (see `RPCConvertValues`).
 *
 *   POST /api/v1/rpc/<method>
 *        Body must be a JSON array (positional) or object (named). No
 *        method name is taken from the body; the URL is authoritative.
 *
 *   GET  /api/v1/rpc            -> lists every method on the allowlist.
 */
bool HandleLegacyRpc(const util::Ref &ctx, HTTPRequest *req,
                     const std::vector<std::string> &parts,
                     const QueryParams &qp);

/** Returns the read-only RPC method allowlist. Exposed for tests/openapi. */
const std::unordered_set<std::string> &LegacyRpcAllowlist();

/**
 * Synchronously execute an allowlist-class RPC (read-only or consensus-
 * validated broadcast) and write the result as a JSON response on `req`.
 *
 * If `resultKey` is non-empty, the RPC's return value is wrapped as
 * `{ "<resultKey>": <result> }` (handy for primitive return values like
 * fees, hashes, counts). Otherwise the value is written verbatim.
 *
 * Errors thrown by the RPC are mapped to appropriate HTTP statuses and a
 * `{error, message, status}` payload, matching the rest of the API.
 *
 * NOTE: This bypasses the public allowlist and is intended for handlers
 * that hard-code a known-safe method name. It MUST NOT be called with a
 * method derived from user input.
 */
bool ProxyReadOnlyRpc(const util::Ref &ctx, HTTPRequest *req,
                      const std::string &method, const UniValue &params,
                      const std::string &resultKey = "");

} // namespace api

#endif // BITCOIN_API_LEGACY_RPC_HANDLER_H
