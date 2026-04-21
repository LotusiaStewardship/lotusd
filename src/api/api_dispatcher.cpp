// Copyright (c) 2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/api_dispatcher.h>

#include <api/api_server.h>
#include <api/api_util.h>

#include <httpserver.h>
#include <rpc/protocol.h>
#include <util/ref.h>

namespace api {

CapturedApiResponse DispatchApiCall(const util::Ref &context,
                                    HTTPRequest::RequestMethod method,
                                    const std::string &path_suffix,
                                    const QueryParams &qp,
                                    const std::string &body,
                                    const std::string &content_type,
                                    DispatchStatus &out_status,
                                    size_t max_body) {
    out_status = DISPATCH_OK;
    CapturedApiResponse out;

    if (max_body > 0 && body.size() > max_body) {
        out_status = DISPATCH_BODY_TOO_LARGE;
        return out;
    }

    // Strip a leading "/" — callers may or may not include it; the route
    // table is keyed on bare names ("chain", "rpc", …).
    std::string clean = path_suffix;
    while (!clean.empty() && clean.front() == '/') {
        clean.erase(0, 1);
    }

    // Allow callers to encode query params inside the path suffix
    // (e.g. "txs?txid=abcd…"). Anything explicit in `qp` overrides.
    QueryParams merged = api::ParseQueryString(clean);
    for (const auto &kv : qp.params) {
        merged.params[kv.first] = kv.second;
    }
    auto parts = api::SplitPath(clean);

    // OPTIONS is handled by the live HTTP dispatcher, never by a handler;
    // for in-process calls we just synthesize the standard CORS preamble.
    if (method == HTTPRequest::OPTIONS) {
        out.status = HTTP_OK;
        out.content_type = "text/plain";
        out.body.clear();
        return out;
    }

    if (!RouteExists(method, parts)) {
        out_status = DISPATCH_NOT_FOUND;
        return out;
    }

    auto req = HTTPRequest::MakeInMemory(method, "/api/v1/" + clean, body,
                                         content_type);
    const bool ran = RunRouteInProcess(context, req.get(), method, parts,
                                       merged);
    if (!ran) {
        // FindRoute and RouteExists agreed a moment ago that there's a
        // match — losing it between the two calls would mean the route
        // table is being mutated concurrently. Surface as 404 rather
        // than crash.
        out_status = DISPATCH_NOT_FOUND;
        return out;
    }

    // If the handler wrote nothing, fall back to a generic 500 so the
    // caller doesn't have to special-case "ReplySent==false".
    if (!req->ReplySent()) {
        out.status = HTTP_INTERNAL_SERVER_ERROR;
        out.content_type = "application/json";
        out.body = "{\"error\":\"internal_error\",\"message\":\"handler "
                   "did not produce a response\",\"status\":500}\n";
        return out;
    }

    out.status = req->CapturedStatus();
    out.content_type = req->CapturedContentType();
    out.body = req->CapturedBody();
    return out;
}

} // namespace api
