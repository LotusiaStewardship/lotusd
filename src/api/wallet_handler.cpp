// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/wallet_handler.h>

#include <config.h>
#include <rpc/protocol.h>
#include <rpc/server.h>
#include <util/ref.h>

namespace api {

bool HandleGetWalletInfo(const util::Ref &ctx, HTTPRequest *req,
                         const std::vector<std::string> &parts,
                         const QueryParams &qp) {
    // Wallet endpoints proxy to existing RPC commands.
    // The wallet module is conditionally compiled and may not be available.
    std::string subResource =
        (parts.size() >= 2) ? parts[1] : std::string();

    if (subResource == "balance") {
        // Proxy to getbalance RPC
        JSONRPCRequest rpcReq(ctx);
        rpcReq.strMethod = "getbalance";
        rpcReq.params = UniValue(UniValue::VARR);

        try {
            UniValue rpcResult = tableRPC.execute(GetConfig(), rpcReq);
            UniValue result(UniValue::VOBJ);
            result.pushKV("balance", rpcResult);
            WriteSuccess(req, result);
        } catch (const UniValue &objError) {
            const UniValue &code = find_value(objError, "code");
            const UniValue &msg = find_value(objError, "message");
            WriteError(req, HTTP_INTERNAL_SERVER_ERROR, "rpc_error",
                       msg.isStr() ? msg.get_str() : "Wallet RPC error");
        } catch (const std::exception &e) {
            WriteError(req, HTTP_INTERNAL_SERVER_ERROR, "rpc_error",
                       e.what());
        }
        return true;
    }

    if (subResource == "addresses") {
        // Proxy to getaddressesbylabel ""
        JSONRPCRequest rpcReq(ctx);
        rpcReq.strMethod = "listreceivedbyaddress";
        rpcReq.params = UniValue(UniValue::VARR);
        rpcReq.params.push_back(0);     // minconf
        rpcReq.params.push_back(true);  // include_empty

        try {
            UniValue rpcResult = tableRPC.execute(GetConfig(), rpcReq);
            WriteSuccess(req, rpcResult);
        } catch (const UniValue &objError) {
            const UniValue &msg = find_value(objError, "message");
            WriteError(req, HTTP_INTERNAL_SERVER_ERROR, "rpc_error",
                       msg.isStr() ? msg.get_str() : "Wallet RPC error");
        } catch (const std::exception &e) {
            WriteError(req, HTTP_INTERNAL_SERVER_ERROR, "rpc_error",
                       e.what());
        }
        return true;
    }

    // Default: wallet info
    JSONRPCRequest rpcReq(ctx);
    rpcReq.strMethod = "getwalletinfo";
    rpcReq.params = UniValue(UniValue::VARR);

    try {
        UniValue rpcResult = tableRPC.execute(GetConfig(), rpcReq);
        WriteSuccess(req, rpcResult);
    } catch (const UniValue &objError) {
        const UniValue &msg = find_value(objError, "message");
        int code = HTTP_INTERNAL_SERVER_ERROR;
        std::string errMsg = msg.isStr() ? msg.get_str() : "Wallet not available";
        const UniValue &rpcCode = find_value(objError, "code");
        if (rpcCode.isNum() && rpcCode.get_int() == RPC_METHOD_NOT_FOUND) {
            code = HTTP_NOT_FOUND;
            errMsg = "Wallet not loaded or wallet support not compiled";
        }
        WriteError(req, code, "wallet_error", errMsg);
    } catch (const std::exception &e) {
        WriteError(req, HTTP_INTERNAL_SERVER_ERROR, "wallet_error",
                   e.what());
    }
    return true;
}

} // namespace api
