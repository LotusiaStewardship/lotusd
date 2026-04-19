// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/api_server.h>

#include <api/address_handler.h>
#include <api/api_util.h>
#include <api/chain_handler.h>
#include <api/dashboard_handler.h>
#include <api/events_handler.h>
#include <api/mining_handler.h>
#include <api/openapi_handler.h>
#include <api/network_handler.h>
#include <api/overview_handler.h>
#include <api/stats_handler.h>
#include <api/tx_handler.h>
#include <api/wallet_handler.h>
#include <config.h>
#include <httpserver.h>
#include <logging.h>
#include <rpc/protocol.h>
#include <util/ref.h>

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

static const char *API_PREFIX = "/api/v1/";

using RouteHandler =
    std::function<bool(const util::Ref &, HTTPRequest *,
                       const std::vector<std::string> &, const api::QueryParams &)>;

struct Route {
    HTTPRequest::RequestMethod method;
    std::string prefix;
    RouteHandler handler;
};

static std::vector<Route> g_routes;

static void AddRoute(HTTPRequest::RequestMethod method,
                     const std::string &prefix, RouteHandler handler) {
    g_routes.push_back({method, prefix, std::move(handler)});
}

static bool DispatchRequest(const util::Ref &context, Config &config,
                            HTTPRequest *req, const std::string &strURIPart) {
    std::string fullPath = strURIPart;
    auto qp = api::ParseQueryString(fullPath);
    auto parts = api::SplitPath(fullPath);

    auto method = req->GetRequestMethod();

    if (method == HTTPRequest::OPTIONS) {
        req->WriteHeader("Access-Control-Allow-Origin", "*");
        req->WriteHeader("Access-Control-Allow-Methods",
                         "GET, POST, PUT, DELETE, OPTIONS");
        req->WriteHeader("Access-Control-Allow-Headers",
                         "Content-Type, Authorization");
        req->WriteHeader("Access-Control-Max-Age", "86400");
        req->WriteReply(HTTP_OK);
        return true;
    }

    // Match routes: find longest prefix match
    const Route *bestMatch = nullptr;
    size_t bestLen = 0;

    for (const auto &route : g_routes) {
        if (route.method != method) {
            continue;
        }
        auto routeParts = api::SplitPath(route.prefix);
        if (routeParts.size() > parts.size()) {
            continue;
        }
        bool match = true;
        for (size_t i = 0; i < routeParts.size(); i++) {
            if (routeParts[i] != parts[i]) {
                match = false;
                break;
            }
        }
        if (match && routeParts.size() > bestLen) {
            bestLen = routeParts.size();
            bestMatch = &route;
        }
    }

    if (bestMatch) {
        try {
            return bestMatch->handler(context, req, parts, qp);
        } catch (const std::exception &e) {
            LogPrintf("ERROR: API handler exception: %s\n", e.what());
            api::WriteError(req, HTTP_INTERNAL_SERVER_ERROR,
                            "internal_error", e.what());
            return true;
        }
    }

    api::WriteError(req, HTTP_NOT_FOUND, "not_found",
                    "Endpoint not found: /api/v1/" + fullPath);
    return true;
}

void AddModuleRoute(HTTPRequest::RequestMethod method,
                    const std::string &prefix, ModuleRouteHandler handler) {
    AddRoute(method, prefix, handler);
}

void StartAPI(const util::Ref &context) {
    // REST API v1 init

    g_routes.clear();

    // Chain endpoints
    AddRoute(HTTPRequest::GET, "chain", api::HandleGetChainInfo);
    AddRoute(HTTPRequest::GET, "chain/tip", api::HandleGetChainTip);

    // Block endpoints
    AddRoute(HTTPRequest::GET, "blocks", api::HandleGetBlocks);

    // Transaction endpoints
    AddRoute(HTTPRequest::GET, "txs", api::HandleGetTx);
    AddRoute(HTTPRequest::GET, "mempool", api::HandleGetMempool);
    AddRoute(HTTPRequest::POST, "txs/send", api::HandleSendTx);
    AddRoute(HTTPRequest::POST, "txs/decode", api::HandleDecodeTx);

    // Address/UTXO endpoints
    AddRoute(HTTPRequest::GET, "addresses", api::HandleGetAddress);
    AddRoute(HTTPRequest::GET, "utxos", api::HandleGetUtxos);

    // Network endpoints
    AddRoute(HTTPRequest::GET, "network", api::HandleGetNetworkInfo);
    AddRoute(HTTPRequest::GET, "network/peers", api::HandleGetPeers);

    // Node endpoints
    AddRoute(HTTPRequest::GET, "node", api::HandleGetNodeInfo);

    // Mining endpoints
    AddRoute(HTTPRequest::GET, "mining", api::HandleGetMiningInfo);

    // Stats endpoints
    AddRoute(HTTPRequest::GET, "stats", api::HandleGetStats);

    // Mempool history
    AddRoute(HTTPRequest::GET, "mempool/history", api::HandleGetMempoolHistory);

    // Network nodes
    AddRoute(HTTPRequest::GET, "network/nodes", api::HandleGetNetworkNodes);

    // Overview (combined endpoint)
    AddRoute(HTTPRequest::GET, "overview", api::HandleGetOverview);

    // Wallet endpoints
    AddRoute(HTTPRequest::GET, "wallet", api::HandleGetWalletInfo);

    // Events endpoint (long-poll SSE)
    AddRoute(HTTPRequest::GET, "events", api::HandleGetEvents);

    // OpenAPI schema
    AddRoute(HTTPRequest::GET, "openapi.json", api::HandleGetOpenAPISchema);

    api::StartEvents();
    api::StartStatsCollector(context);

    auto handler = [&context](Config &config, HTTPRequest *req,
                              const std::string &prefix) {
        return DispatchRequest(context, config, req, prefix);
    };
    RegisterHTTPHandler(API_PREFIX, false, handler);

    auto dashboardHandler = [&context](Config &, HTTPRequest *req,
                                       const std::string &) {
        std::vector<std::string> empty;
        api::QueryParams qp;
        return api::HandleGetDashboard(context, req, empty, qp);
    };
    RegisterHTTPHandler("/dashboard", false, dashboardHandler);
}

void InterruptAPI() {}

void StopAPI() {
    LogPrintf("Stopping REST API v1\n");
    api::StopStatsCollector();
    api::StopEvents();
    UnregisterHTTPHandler("/dashboard", true);
    UnregisterHTTPHandler(API_PREFIX, false);
    g_routes.clear();
}
