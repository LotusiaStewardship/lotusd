// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/network_handler.h>

#include <config.h>
#include <net.h>
#include <net_processing.h>
#include <node/context.h>
#include <rpc/protocol.h>
#include <util/ref.h>
#include <util/system.h>
#include <util/time.h>
#include <validation.h>
#include <version.h>

namespace api {

bool HandleGetNetworkInfo(const util::Ref &ctx, HTTPRequest *req,
                          const std::vector<std::string> &,
                          const QueryParams &) {
    NodeContext *node =
        ctx.Has<NodeContext>() ? &ctx.Get<NodeContext>() : nullptr;

    UniValue result(UniValue::VOBJ);
    result.pushKV("protocol_version", PROTOCOL_VERSION);
    result.pushKV("subversion", userAgent(GetConfig()));

    if (node && node->connman) {
        result.pushKV("connections",
                       int(node->connman->GetNodeCount(CConnman::CONNECTIONS_ALL)));
        result.pushKV("connections_in",
                       int(node->connman->GetNodeCount(CConnman::CONNECTIONS_IN)));
        result.pushKV("connections_out",
                       int(node->connman->GetNodeCount(CConnman::CONNECTIONS_OUT)));
    }

    result.pushKV("network_active",
                   node && node->connman ? node->connman->GetNetworkActive()
                                          : false);

    WriteSuccess(req, result);
    return true;
}

bool HandleGetPeers(const util::Ref &ctx, HTTPRequest *req,
                    const std::vector<std::string> &,
                    const QueryParams &qp) {
    NodeContext *node =
        ctx.Has<NodeContext>() ? &ctx.Get<NodeContext>() : nullptr;
    if (!node || !node->connman) {
        WriteError(req, HTTP_SERVICE_UNAVAILABLE, "no_network",
                   "Network not available");
        return true;
    }

    std::vector<CNodeStats> vstats;
    node->connman->GetNodeStats(vstats);

    UniValue peers(UniValue::VARR);
    for (const auto &stats : vstats) {
        UniValue peer(UniValue::VOBJ);
        peer.pushKV("id", stats.nodeid);
        peer.pushKV("addr", stats.addrName);
        peer.pushKV("subver", stats.cleanSubVer);
        peer.pushKV("inbound", stats.fInbound);
        peer.pushKV("startingheight", stats.m_starting_height);
        peer.pushKV("ping_ms",
                     stats.m_last_ping_time.count() > 0
                         ? double(stats.m_last_ping_time.count()) / 1000.0
                         : -1.0);
        peer.pushKV("bytes_sent", int64_t(stats.nSendBytes));
        peer.pushKV("bytes_recv", int64_t(stats.nRecvBytes));
        peers.push_back(peer);
    }

    WriteSuccess(req, peers);
    return true;
}

bool HandleGetNodeInfo(const util::Ref &ctx, HTTPRequest *req,
                       const std::vector<std::string> &,
                       const QueryParams &) {
    UniValue result(UniValue::VOBJ);
    result.pushKV("version", CLIENT_VERSION);
    result.pushKV("subversion", userAgent(GetConfig()));
    result.pushKV("protocol_version", PROTOCOL_VERSION);
    result.pushKV("uptime", GetTime() - GetStartupTime());

    {
        LOCK(cs_main);
        result.pushKV("initial_block_download",
                       ::ChainstateActive().IsInitialBlockDownload());

        const CBlockIndex *tip = ::ChainActive().Tip();
        if (tip) {
            result.pushKV("chain_height", tip->nHeight);
            result.pushKV("best_block_hash", tip->GetBlockHash().GetHex());
        }
    }

    result.pushKV("datadir", fs::PathToString(gArgs.GetDataDirPath()));

    WriteSuccess(req, result);
    return true;
}

bool HandleGetNetworkNodes(const util::Ref &ctx, HTTPRequest *req,
                            const std::vector<std::string> &,
                            const QueryParams &) {
    NodeContext *node =
        ctx.Has<NodeContext>() ? &ctx.Get<NodeContext>() : nullptr;
    if (!node || !node->connman) {
        WriteError(req, HTTP_SERVICE_UNAVAILABLE, "no_network",
                   "Network not available");
        return true;
    }

    std::vector<CNodeStats> vstats;
    node->connman->GetNodeStats(vstats);

    UniValue addnodeArr(UniValue::VARR);
    UniValue onetryArr(UniValue::VARR);
    for (const auto &stats : vstats) {
        std::string host = stats.addrName;
        if (host.empty()) {
            continue;
        }
        addnodeArr.push_back("addnode=" + host);
        onetryArr.push_back("onetry=" + host);
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("addnode", addnodeArr);
    result.pushKV("onetry", onetryArr);
    WriteSuccess(req, result);
    return true;
}

} // namespace api
