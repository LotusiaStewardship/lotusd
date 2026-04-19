// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/overview_handler.h>

#include <chain.h>
#include <chainparams.h>
#include <config.h>
#include <net.h>
#include <node/context.h>
#include <pow/pow.h>
#include <rpc/blockchain.h>
#include <rpc/protocol.h>
#include <sync.h>
#include <txmempool.h>
#include <util/ref.h>
#include <util/time.h>
#include <validation.h>
#include <version.h>

namespace api {

bool HandleGetOverview(const util::Ref &ctx, HTTPRequest *req,
                       const std::vector<std::string> &,
                       const QueryParams &) {
    NodeContext *node =
        ctx.Has<NodeContext>() ? &ctx.Get<NodeContext>() : nullptr;

    UniValue result(UniValue::VOBJ);

    // Chain info
    UniValue chain(UniValue::VOBJ);
    {
        LOCK(cs_main);
        const CBlockIndex *tip = ::ChainActive().Tip();
        if (!tip) {
            WriteError(req, HTTP_SERVICE_UNAVAILABLE, "not_ready",
                       "Chain not yet loaded");
            return true;
        }

        chain.pushKV("height", tip->nHeight);
        chain.pushKV("best_block_hash", tip->GetBlockHash().GetHex());
        chain.pushKV("difficulty", GetDifficulty(tip));
        chain.pushKV("median_time", int64_t(tip->GetMedianTimePast()));
        chain.pushKV("chain_work", tip->nChainWork.GetHex());

        // Latest block summary
        UniValue latest(UniValue::VOBJ);
        latest.pushKV("hash", tip->GetBlockHash().GetHex());
        latest.pushKV("height", tip->nHeight);
        latest.pushKV("time", int64_t(tip->GetBlockTime()));
        latest.pushKV("n_tx", int64_t(tip->nTx));
        latest.pushKV("size", int64_t(tip->nSize));
        result.pushKV("latest_block", latest);
    }
    result.pushKV("chain", chain);

    // Mining info
    UniValue mining(UniValue::VOBJ);
    {
        LOCK(cs_main);
        const CBlockIndex *tip = ::ChainActive().Tip();
        if (tip) {
            mining.pushKV("height", tip->nHeight);
            mining.pushKV("difficulty", GetDifficulty(tip));
            mining.pushKV("bits", strprintf("%08x", tip->nBits));
        }
    }
    result.pushKV("mining", mining);

    // Mempool info
    UniValue mempool(UniValue::VOBJ);
    if (node && node->mempool) {
        LOCK(node->mempool->cs);
        mempool.pushKV("size", int64_t(node->mempool->size()));
        mempool.pushKV("bytes", int64_t(node->mempool->GetTotalTxSize()));
    } else {
        mempool.pushKV("size", int64_t(0));
        mempool.pushKV("bytes", int64_t(0));
    }
    result.pushKV("mempool", mempool);

    // Network info
    UniValue network(UniValue::VOBJ);
    if (node && node->connman) {
        network.pushKV("connections",
                        int(node->connman->GetNodeCount(
                            CConnman::CONNECTIONS_ALL)));
        network.pushKV("connections_in",
                        int(node->connman->GetNodeCount(
                            CConnman::CONNECTIONS_IN)));
        network.pushKV("connections_out",
                        int(node->connman->GetNodeCount(
                            CConnman::CONNECTIONS_OUT)));
    }
    network.pushKV("protocol_version", PROTOCOL_VERSION);
    network.pushKV("subversion", userAgent(GetConfig()));
    result.pushKV("network", network);

    WriteSuccess(req, result);
    return true;
}

} // namespace api
