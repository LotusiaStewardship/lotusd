// Copyright (c) 2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_DISPATCHER_H
#define BITCOIN_API_DISPATCHER_H

#include <api/api_util.h>
#include <httpserver.h>

#include <string>

namespace util {
class Ref;
}

namespace api {

/**
 * Result of an in-process API dispatch.
 *
 * Mirrors what the HTTP layer would have written back to a client. The
 * NNG RestCall tunnel ferries this verbatim across the wire so on-chain
 * indexers, light wallets, and explorer backends can reuse the existing
 * REST surface without ever opening a TCP socket.
 */
struct CapturedApiResponse {
    int status{200};
    std::string content_type{"text/plain"};
    std::string body;

    /** True iff the captured status is in the [200..300) range. */
    bool IsOk() const { return status >= 200 && status < 300; }
};

/**
 * Sentinel codes emitted by DispatchApiCall when the dispatcher itself
 * (not the handler) decides the call cannot proceed. These intentionally
 * use HTTP-style status codes so they line up with what the handler-side
 * helpers (api::WriteError) already produce.
 */
enum DispatchStatus {
    DISPATCH_OK = 0,
    DISPATCH_NOT_FOUND = 404,
    DISPATCH_METHOD_NOT_ALLOWED = 405,
    DISPATCH_BODY_TOO_LARGE = 413,
};

/**
 * Synchronously invoke the API route registered for
 * /api/v1/<path_suffix> and return its captured response.
 *
 * @param context      the node-wide util::Ref that the live HTTP layer
 *                     hands to every handler.
 * @param method       HTTP verb (GET/POST/OPTIONS).
 * @param path_suffix  path relative to /api/v1/, e.g. "rpc/getblock"
 *                     or "chain/tip". Leading and trailing slashes are
 *                     ignored. If the suffix contains a '?', everything
 *                     after it is appended to the parsed query params
 *                     (the explicit `qp` argument always wins).
 * @param qp           query parameters parsed by the caller (NNG side
 *                     decodes them from KvPair vectors).
 * @param body         raw request body for POST handlers; empty for
 *                     GET. Will be exposed via `req->ReadBody()`.
 * @param content_type optional Content-Type for the body.
 * @param out_status   sentinel describing why dispatch failed when the
 *                     return value is empty (route not found, body too
 *                     large, etc.). Set to DISPATCH_OK on success.
 * @param max_body     hard cap on body size; if `body.size()` exceeds
 *                     this, dispatch is refused with
 *                     DISPATCH_BODY_TOO_LARGE. 0 disables the check.
 *
 * @return the captured response. On dispatcher-level failure the body
 *         is empty and `out_status` is set; on handler failure the
 *         handler's own error envelope is returned with `out_status =
 *         DISPATCH_OK`.
 */
CapturedApiResponse DispatchApiCall(const util::Ref &context,
                                    HTTPRequest::RequestMethod method,
                                    const std::string &path_suffix,
                                    const QueryParams &qp,
                                    const std::string &body,
                                    const std::string &content_type,
                                    DispatchStatus &out_status,
                                    size_t max_body = 0);

} // namespace api

#endif // BITCOIN_API_DISPATCHER_H
