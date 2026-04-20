// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/legacy_rpc_handler.h>

#include <config.h>
#include <logging.h>
#include <rpc/client.h>
#include <rpc/protocol.h>
#include <rpc/request.h>
#include <rpc/server.h>
#include <util/ref.h>

#include <algorithm>
#include <exception>
#include <stdexcept>

namespace api {

// ---------------------------------------------------------------------------
// Allowlist of RPCs that are safe to expose without authentication.
//
// A method qualifies for this list iff it falls into ONE of two buckets:
//
//   1. Read-only queries — idempotent, side-effect free, no wallet, no
//      peer-set mutation, no operator-only knob. Cheap-ish to compute so
//      callers cannot trivially DoS the node by hammering them.
//
//   2. Consensus-validated broadcasts — submissions whose admissibility is
//      decided by the network's consensus / standardness rules, not by a
//      privileged credential on this node. Concretely: `sendrawtransaction`,
//      `submitblock`, `submitheader`, `sendavalancheproof`. Letting these
//      in is safe because the worst an unauthenticated caller can do is
//      relay garbage that consensus already has to reject — exactly the
//      same failure mode the node already handles for any p2p peer.
//
// Anything that mutates *operator* state — wallet*, addnode, setban,
// disconnectnode, generate*, generatetoaddress, prioritisetransaction,
// finalize/invalidate/parkblock, stop, walletpassphrase, … — is
// intentionally absent and will be rejected with HTTP 403. Those remain
// on the authenticated JSON-RPC interface only.
// ---------------------------------------------------------------------------
static const std::unordered_set<std::string> kReadOnlyMethods = {
    // blockchain
    "getbestblockhash",
    "getblock",
    "getblockchaininfo",
    "getblockcount",
    "getblockhash",
    "getblockheader",
    "getblockstats",
    "getchaintips",
    "getchaintxstats",
    "getdifficulty",
    "getmempoolancestors",
    "getmempooldescendants",
    "getmempoolentry",
    "getmempoolinfo",
    "getrawmempool",
    "gettxout",
    "gettxoutsetinfo",
    "gettxoutproof",
    "verifytxoutproof",
    // mining / fee
    "getmininginfo",
    "getnetworkhashps",
    "estimatefee",
    // raw transactions (read-only subset)
    "getrawtransaction",
    "decoderawtransaction",
    "decodescript",
    "createrawtransaction",
    "combinerawtransaction",
    "testmempoolaccept",
    "decodepsbt",
    "analyzepsbt",
    // consensus-validated broadcasts (network is the gatekeeper)
    "sendrawtransaction",
    "submitblock",
    "submitheader",
    // network info (read-only subset)
    "getnetworkinfo",
    "getpeerinfo",
    "getconnectioncount",
    "getnettotals",
    "getnodeaddresses",
    "getaddednodeinfo",
    "listbanned",
    // util
    "validateaddress",
    "verifymessage",
    "getdescriptorinfo",
    "deriveaddresses",
    "getcurrencyinfo",
    "getindexinfo",
    // misc / control (safe read-only bits)
    "uptime",
    "getmemoryinfo",
    "getrpcinfo",
    "getexcessiveblock",
    // avalanche (read-only + consensus-validated broadcast)
    "getavalanchekey",
    "getavalanchepeerinfo",
    "getrawavalancheproof",
    "decodeavalancheproof",
    "verifyavalancheproof",
    "sendavalancheproof",
};

const std::unordered_set<std::string> &LegacyRpcAllowlist() {
    return kReadOnlyMethods;
}

// Build a JSON-RPC error reply object with code/message in the same shape as
// httprpc.cpp would have produced, so clients get a familiar structure.
static UniValue MakeRpcError(int code, const std::string &message) {
    UniValue err(UniValue::VOBJ);
    err.pushKV("code", code);
    err.pushKV("message", message);
    return err;
}

static void WriteRpcReply(HTTPRequest *req, int httpStatus,
                          const UniValue &result, const UniValue &error) {
    UniValue reply(UniValue::VOBJ);
    reply.pushKV("result", result.isNull() ? NullUniValue : result);
    reply.pushKV("error", error.isNull() ? NullUniValue : error);
    reply.pushKV("id", NullUniValue);
    WriteJSON(req, httpStatus, reply);
}

static int RpcCodeToHttp(int rpcCode) {
    switch (rpcCode) {
        case RPC_INVALID_REQUEST:
        case RPC_INVALID_PARAMS:
        case RPC_TYPE_ERROR:
        case RPC_DESERIALIZATION_ERROR:
        case RPC_INVALID_PARAMETER:
            return HTTP_BAD_REQUEST;
        case RPC_METHOD_NOT_FOUND:
        case RPC_INVALID_ADDRESS_OR_KEY:
            return HTTP_NOT_FOUND;
        case RPC_FORBIDDEN_BY_SAFE_MODE:
            return HTTP_FORBIDDEN;
        case RPC_IN_WARMUP:
            return HTTP_SERVICE_UNAVAILABLE;
        default:
            return HTTP_INTERNAL_SERVER_ERROR;
    }
}

bool ProxyReadOnlyRpc(const util::Ref &ctx, HTTPRequest *req,
                      const std::string &method, const UniValue &params,
                      const std::string &resultKey) {
    JSONRPCRequest jreq(ctx);
    jreq.strMethod = method;
    jreq.params = params.isNull() ? UniValue(UniValue::VARR) : params;
    jreq.URI = "/api/v1/rpc/" + method;
    jreq.authUser = "rest-anon";

    try {
        UniValue r = tableRPC.execute(GetConfig(), jreq);
        if (!resultKey.empty()) {
            UniValue out(UniValue::VOBJ);
            out.pushKV(resultKey, r);
            WriteSuccess(req, out);
        } else {
            WriteSuccess(req, r);
        }
    } catch (const UniValue &objError) {
        const UniValue &codeV = find_value(objError, "code");
        const UniValue &msgV = find_value(objError, "message");
        int rpcCode = codeV.isNum() ? codeV.get_int() : RPC_MISC_ERROR;
        std::string msg = msgV.isStr() ? msgV.get_str() : objError.write();
        WriteError(req, RpcCodeToHttp(rpcCode), "rpc_error", msg);
    } catch (const std::exception &e) {
        WriteError(req, HTTP_INTERNAL_SERVER_ERROR, "rpc_error", e.what());
    }
    return true;
}

// Convert query-string params into a UniValue suitable for JSON-RPC.
//
// If any key is `params` and its value is a JSON literal (array or object),
// we use that verbatim — this is the easiest way for callers that already
// know how to build proper RPC params.
//
// Otherwise we look for either:
//   * positional: keys "0", "1", "2", ... (or `arg0`, `arg1`, ...)
//   * named: arbitrary keys, passed through as a JSON object
//
// In both cases each value is then run through `RPCConvertValues` /
// `RPCConvertNamedValues` so that integer/bool/array params are typed
// correctly per the RPC's spec.
static UniValue BuildParamsFromQuery(const std::string &method,
                                      const QueryParams &qp,
                                      std::string &err) {
    auto rawParams = qp.Get("params");
    if (rawParams) {
        UniValue v;
        if (!v.read(*rawParams)) {
            err = "`params` is not valid JSON";
            return NullUniValue;
        }
        if (!v.isArray() && !v.isObject()) {
            err = "`params` must be a JSON array or object";
            return NullUniValue;
        }
        return v;
    }

    // Detect positional keys 0..N or arg0..argN
    std::vector<std::string> positional;
    {
        const auto &m = qp.params;
        size_t i = 0;
        while (true) {
            auto numKey = std::to_string(i);
            auto argKey = "arg" + numKey;
            auto it = m.find(numKey);
            if (it == m.end()) {
                it = m.find(argKey);
            }
            if (it == m.end()) {
                break;
            }
            positional.push_back(it->second);
            ++i;
        }
    }

    if (!positional.empty()) {
        try {
            return RPCConvertValues(method, positional);
        } catch (const std::exception &e) {
            err = std::string("failed to parse positional params: ") + e.what();
            return NullUniValue;
        }
    }

    // Named: any key that isn't reserved.
    static const std::unordered_set<std::string> reserved = {
        "params", "method", "callback"};
    std::vector<std::string> nameValPairs;
    for (const auto &kv : qp.params) {
        if (reserved.count(kv.first)) continue;
        nameValPairs.push_back(kv.first + "=" + kv.second);
    }
    if (nameValPairs.empty()) {
        return UniValue(UniValue::VARR);
    }
    try {
        return RPCConvertNamedValues(method, nameValPairs);
    } catch (const std::exception &e) {
        err = std::string("failed to parse named params: ") + e.what();
        return NullUniValue;
    }
}

static UniValue BuildParamsFromBody(const std::string &body, std::string &err) {
    if (body.empty()) {
        return UniValue(UniValue::VARR);
    }
    UniValue v;
    if (!v.read(body)) {
        err = "request body is not valid JSON";
        return NullUniValue;
    }
    if (v.isObject()) {
        // Allow either {"params": [...]} or a plain {key:val, ...} mapping.
        const UniValue &p = find_value(v, "params");
        if (!p.isNull() && (p.isArray() || p.isObject())) {
            return p;
        }
        return v;
    }
    if (v.isArray()) {
        return v;
    }
    err = "request body must be a JSON object or array";
    return NullUniValue;
}

bool HandleLegacyRpc(const util::Ref &ctx, HTTPRequest *req,
                     const std::vector<std::string> &parts,
                     const QueryParams &qp) {
    // parts is the URI split, e.g. for /api/v1/rpc/getblockcount:
    //   parts = ["rpc", "getblockcount"]
    if (parts.size() < 2) {
        // GET /api/v1/rpc -> directory listing of the allowlist.
        if (req->GetRequestMethod() != HTTPRequest::GET) {
            WriteError(req, HTTP_BAD_METHOD, "method_not_allowed",
                       "Only GET is allowed for the RPC directory");
            return true;
        }
        UniValue methods(UniValue::VARR);
        std::vector<std::string> sorted(kReadOnlyMethods.begin(),
                                         kReadOnlyMethods.end());
        std::sort(sorted.begin(), sorted.end());
        for (const auto &m : sorted) {
            methods.push_back(m);
        }
        UniValue out(UniValue::VOBJ);
        out.pushKV("description",
                   "Unauthenticated legacy RPC dispatcher. Exposes "
                   "read-only queries plus consensus-validated broadcasts "
                   "(sendrawtransaction, submitblock, submitheader, "
                   "sendavalancheproof). Use /api/v1/rpc/<method> with "
                   "query params (GET) or JSON body (POST). Operator / "
                   "wallet methods stay on the authenticated JSON-RPC "
                   "interface.");
        out.pushKV("methods", methods);
        WriteSuccess(req, out);
        return true;
    }

    const std::string method = parts[1];

    // Empty / invalid method names get rejected before we even touch the
    // allowlist, to keep error responses uniform.
    if (method.empty()) {
        WriteError(req, HTTP_BAD_REQUEST, "missing_method",
                   "Usage: /api/v1/rpc/<method>");
        return true;
    }

    if (!kReadOnlyMethods.count(method)) {
        WriteError(req, HTTP_FORBIDDEN, "method_not_allowed",
                   "Method '" + method +
                       "' is not on the unauthenticated read-only allowlist. "
                       "Use the authenticated JSON-RPC interface for write "
                       "or operator-only methods.");
        return true;
    }

    // Build the params payload
    std::string parseErr;
    UniValue params;
    auto httpMethod = req->GetRequestMethod();
    if (httpMethod == HTTPRequest::POST) {
        params = BuildParamsFromBody(req->ReadBody(), parseErr);
    } else if (httpMethod == HTTPRequest::GET) {
        params = BuildParamsFromQuery(method, qp, parseErr);
    } else {
        WriteError(req, HTTP_BAD_METHOD, "method_not_allowed",
                   "Only GET and POST are accepted for /api/v1/rpc/*");
        return true;
    }

    if (!parseErr.empty()) {
        WriteError(req, HTTP_BAD_REQUEST, "invalid_params", parseErr);
        return true;
    }

    JSONRPCRequest jreq(ctx);
    jreq.strMethod = method;
    jreq.params = params.isNull() ? UniValue(UniValue::VARR) : params;
    jreq.URI = "/api/v1/rpc/" + method;
    // Mark with a recognisable "user" so node logs can spot REST traffic.
    jreq.authUser = "rest-anon";

    UniValue result;
    try {
        result = tableRPC.execute(GetConfig(), jreq);
    } catch (const UniValue &objError) {
        // Standard JSON-RPC error object: {code, message, ...}
        const UniValue &codeV = find_value(objError, "code");
        const UniValue &msgV = find_value(objError, "message");
        int rpcCode = codeV.isNum() ? codeV.get_int() : RPC_MISC_ERROR;
        std::string msg = msgV.isStr() ? msgV.get_str() : objError.write();
        WriteRpcReply(req, RpcCodeToHttp(rpcCode), NullUniValue,
                      MakeRpcError(rpcCode, msg));
        return true;
    } catch (const std::exception &e) {
        LogPrintf("legacy_rpc(%s) threw: %s\n", method, e.what());
        WriteRpcReply(req, HTTP_INTERNAL_SERVER_ERROR, NullUniValue,
                      MakeRpcError(RPC_INTERNAL_ERROR, e.what()));
        return true;
    } catch (...) {
        WriteRpcReply(req, HTTP_INTERNAL_SERVER_ERROR, NullUniValue,
                      MakeRpcError(RPC_INTERNAL_ERROR, "unknown error"));
        return true;
    }

    WriteRpcReply(req, HTTP_OK, result, NullUniValue);
    return true;
}

} // namespace api
