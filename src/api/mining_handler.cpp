// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/mining_handler.h>

#include <api/legacy_rpc_handler.h>
#include <chain.h>
#include <chainparams.h>
#include <node/context.h>
#include <pow/pow.h>
#include <rpc/blockchain.h>
#include <rpc/protocol.h>
#include <sync.h>
#include <txmempool.h>
#include <util/ref.h>
#include <validation.h>

namespace api {

bool HandleGetMiningInfo(const util::Ref &ctx, HTTPRequest *req,
                         const std::vector<std::string> &,
                         const QueryParams &) {
    NodeContext *node =
        ctx.Has<NodeContext>() ? &ctx.Get<NodeContext>() : nullptr;

    UniValue result(UniValue::VOBJ);
    {
        LOCK(cs_main);
        const CBlockIndex *tip = ::ChainActive().Tip();
        if (!tip) {
            WriteError(req, HTTP_SERVICE_UNAVAILABLE, "not_ready",
                       "Chain not loaded");
            return true;
        }

        result.pushKV("height", tip->nHeight);
        result.pushKV("difficulty", GetDifficulty(tip));
        result.pushKV("bits", strprintf("%08x", tip->nBits));
        result.pushKV("chain_work", tip->nChainWork.GetHex());
    }

    if (node && node->mempool) {
        LOCK(node->mempool->cs);
        result.pushKV("mempool_size", int64_t(node->mempool->size()));
        result.pushKV("mempool_bytes",
                       int64_t(node->mempool->GetTotalTxSize()));
    }

    result.pushKV("chain",
                   Params().NetworkIDString());

    WriteSuccess(req, result);
    return true;
}

bool HandleEstimateFee(const util::Ref &ctx, HTTPRequest *req,
                       const std::vector<std::string> &,
                       const QueryParams &) {
    return ProxyReadOnlyRpc(ctx, req, "estimatefee",
                             UniValue(UniValue::VARR), "feerate");
}

bool HandleGetNetworkHashPS(const util::Ref &ctx, HTTPRequest *req,
                            const std::vector<std::string> &,
                            const QueryParams &qp) {
    UniValue params(UniValue::VARR);
    params.push_back(qp.GetInt("nblocks", 120));
    params.push_back(qp.GetInt("height", -1));
    return ProxyReadOnlyRpc(ctx, req, "getnetworkhashps", params, "hashps");
}

bool HandleGetDifficultyRpc(const util::Ref &ctx, HTTPRequest *req,
                            const std::vector<std::string> &,
                            const QueryParams &) {
    return ProxyReadOnlyRpc(ctx, req, "getdifficulty",
                             UniValue(UniValue::VARR), "difficulty");
}

} // namespace api
