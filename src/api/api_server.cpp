// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/api_server.h>

#include <api/address_handler.h>
#include <api/api_util.h>
#include <api/chain_handler.h>
#include <api/dashboard_handler.h>
#include <api/events_handler.h>
#include <api/health_handler.h>
#include <api/legacy_rpc_handler.h>
#include <api/mining_handler.h>
#include <api/openapi_handler.h>
#include <api/network_handler.h>
#include <api/overview_handler.h>
#include <api/sitemap_handler.h>
#include <api/stats_handler.h>
#include <api/stratum_handler.h>
#include <api/tx_handler.h>
#include <api/wallet_handler.h>
#include <config.h>
#include <httpserver.h>
#include <logging.h>
#include <rpc/protocol.h>
#include <util/ref.h>

#include <chrono>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

static const char *API_PREFIX = "/api/v1/";

using RouteHandler =
    std::function<bool(const util::Ref &, HTTPRequest *,
                       const std::vector<std::string> &, const api::QueryParams &)>;

struct Route {
    HTTPRequest::RequestMethod method;
    std::string prefix;
    RouteHandler handler;
    int cacheTTL; // seconds; 0 = no cache
};

static std::vector<Route> g_routes;

static void AddRoute(HTTPRequest::RequestMethod method,
                     const std::string &prefix, RouteHandler handler,
                     int cacheTTL = 0) {
    g_routes.push_back({method, prefix, std::move(handler), cacheTTL});
}

// ── Response cache with stale-while-revalidate ──────────────────────────

struct CacheEntry {
    std::string body;
    int status;
    int64_t cachedAt;   // unix timestamp (seconds)
    bool refreshing;    // background revalidation in progress
};

static std::mutex g_cache_mutex;
static std::unordered_map<std::string, CacheEntry> g_cache;

static int64_t NowUnix() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

static void ServeCached(HTTPRequest *req, const CacheEntry &entry) {
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteHeader("X-Cached-At", std::to_string(entry.cachedAt));
    req->WriteHeader("X-Cache", "HIT");
    req->WriteReply(entry.status, entry.body);
}

// Execute handler in capture mode; returns true on success.
static bool RunCaptured(const util::Ref &context, const RouteHandler &handler,
                        HTTPRequest *req,
                        const std::vector<std::string> &parts,
                        const api::QueryParams &qp,
                        api::CapturedResponse &out) {
    api::StartCapture(&out);
    try {
        handler(context, req, parts, qp);
    } catch (const std::exception &e) {
        api::StopCapture();
        return false;
    }
    api::StopCapture();
    return true;
}

static const Route *FindRoute(HTTPRequest::RequestMethod method,
                               const std::vector<std::string> &parts) {
    const Route *bestMatch = nullptr;
    size_t bestLen = 0;
    for (const auto &route : g_routes) {
        if (route.method != method) continue;
        auto routeParts = api::SplitPath(route.prefix);
        if (routeParts.size() > parts.size()) continue;
        bool match = true;
        for (size_t i = 0; i < routeParts.size(); i++) {
            if (routeParts[i] != parts[i]) { match = false; break; }
        }
        if (match && routeParts.size() > bestLen) {
            bestLen = routeParts.size();
            bestMatch = &route;
        }
    }
    return bestMatch;
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

    const Route *bestMatch = FindRoute(method, parts);

    if (!bestMatch) {
        api::WriteError(req, HTTP_NOT_FOUND, "not_found",
                        "Endpoint not found: /api/v1/" + fullPath);
        return true;
    }

    // No cache for non-GET or zero-TTL routes
    if (method != HTTPRequest::GET || bestMatch->cacheTTL <= 0) {
        try {
            return bestMatch->handler(context, req, parts, qp);
        } catch (const std::exception &e) {
            LogPrintf("ERROR: API handler exception: %s\n", e.what());
            api::WriteError(req, HTTP_INTERNAL_SERVER_ERROR,
                            "internal_error", e.what());
            return true;
        }
    }

    // ── Cache path (GET with TTL > 0) ───────────────────────────────────
    std::string cacheKey = strURIPart;
    int64_t now = NowUnix();
    int ttl = bestMatch->cacheTTL;

    // Snapshot cache state under the mutex; never do I/O while locked.
    enum CacheHit { MISS, FRESH, STALE, STALE_NEEDS_REFRESH };
    CacheHit hit = MISS;
    std::string snapBody;
    int snapStatus = 200;
    int64_t snapCachedAt = 0;

    {
        std::lock_guard<std::mutex> lock(g_cache_mutex);
        auto it = g_cache.find(cacheKey);
        if (it != g_cache.end()) {
            snapBody = it->second.body;
            snapStatus = it->second.status;
            snapCachedAt = it->second.cachedAt;
            int64_t age = now - snapCachedAt;
            if (age < ttl) {
                hit = FRESH;
            } else if (it->second.refreshing) {
                hit = STALE;
            } else {
                hit = STALE_NEEDS_REFRESH;
                it->second.refreshing = true;
            }
        }
    }
    // Mutex released — all I/O and thread creation below is lock-free.

    if (hit == FRESH || hit == STALE) {
        CacheEntry snap{snapBody, snapStatus, snapCachedAt, false};
        ServeCached(req, snap);
        return true;
    }

    if (hit == STALE_NEEDS_REFRESH) {
        CacheEntry snap{snapBody, snapStatus, snapCachedAt, false};
        ServeCached(req, snap);
        // Background revalidation
        std::string bgKey = cacheKey;
        RouteHandler bgHandler = bestMatch->handler;
        std::vector<std::string> bgParts = parts;
        api::QueryParams bgQp = qp;
        std::thread([&context, bgHandler, bgParts, bgQp, bgKey]() {
            HTTPRequest dummy;
            api::CapturedResponse cap;
            if (RunCaptured(context, bgHandler, &dummy, bgParts,
                            bgQp, cap)) {
                std::lock_guard<std::mutex> lk(g_cache_mutex);
                auto &e = g_cache[bgKey];
                e.body = std::move(cap.body);
                e.status = cap.status;
                e.cachedAt = NowUnix();
                e.refreshing = false;
            } else {
                std::lock_guard<std::mutex> lk(g_cache_mutex);
                if (g_cache.count(bgKey))
                    g_cache[bgKey].refreshing = false;
            }
        }).detach();
        return true;
    }

    // Cold cache: run handler synchronously, cache, serve.
    api::CapturedResponse cap;
    if (RunCaptured(context, bestMatch->handler, req, parts, qp, cap)) {
        CacheEntry entry;
        entry.body = cap.body;
        entry.status = cap.status;
        entry.cachedAt = now;
        entry.refreshing = false;
        {
            std::lock_guard<std::mutex> lock(g_cache_mutex);
            g_cache[cacheKey] = entry;
        }
        ServeCached(req, entry);
    } else {
        api::WriteError(req, HTTP_INTERNAL_SERVER_ERROR,
                        "internal_error", "handler failed");
    }
    return true;
}

void AddModuleRoute(HTTPRequest::RequestMethod method,
                    const std::string &prefix, ModuleRouteHandler handler) {
    AddRoute(method, prefix, handler);
}

namespace api {

bool RouteExists(HTTPRequest::RequestMethod method,
                 const std::vector<std::string> &parts) {
    return FindRoute(method, parts) != nullptr;
}

bool RunRouteInProcess(const util::Ref &context, HTTPRequest *req,
                       HTTPRequest::RequestMethod method,
                       const std::vector<std::string> &parts,
                       const QueryParams &qp) {
    const Route *route = FindRoute(method, parts);
    if (!route) return false;
    try {
        route->handler(context, req, parts, qp);
    } catch (const std::exception &e) {
        LogPrintf("ERROR: API in-process handler exception: %s\n", e.what());
        // The in-memory request only honors the first WriteReply, so this
        // is a no-op if the handler already produced a response.
        api::WriteError(req, HTTP_INTERNAL_SERVER_ERROR, "internal_error",
                        e.what());
    }
    return true;
}

} // namespace api

void StartAPI(const util::Ref &context) {
    // REST API v1 init

    g_routes.clear();

    //                                                       TTL (seconds)
    AddRoute(HTTPRequest::GET, "chain",     api::HandleGetChainInfo,    5);
    AddRoute(HTTPRequest::GET, "chain/tip", api::HandleGetChainTip,     5);
    AddRoute(HTTPRequest::GET, "blocks",    api::HandleGetBlocks,      10);
    AddRoute(HTTPRequest::GET, "txs",       api::HandleGetTx,          15);
    AddRoute(HTTPRequest::GET, "mempool",   api::HandleGetMempool,      5);
    AddRoute(HTTPRequest::POST,"txs/send",      api::HandleSendTx);
    AddRoute(HTTPRequest::POST,"txs/broadcast", api::HandleBroadcastTx);
    AddRoute(HTTPRequest::POST,"txs/decode",    api::HandleDecodeTx);
    AddRoute(HTTPRequest::GET, "addresses", api::HandleGetAddress,     15);
    AddRoute(HTTPRequest::GET, "utxos",     api::HandleGetUtxos,       10);
    AddRoute(HTTPRequest::GET, "network",   api::HandleGetNetworkInfo, 30);
    AddRoute(HTTPRequest::GET, "network/peers", api::HandleGetPeers,   30);
    AddRoute(HTTPRequest::GET, "node",      api::HandleGetNodeInfo,    30);
    AddRoute(HTTPRequest::GET, "mining",    api::HandleGetMiningInfo,  15);
    AddRoute(HTTPRequest::GET, "stats",     api::HandleGetStats,       30);
    AddRoute(HTTPRequest::GET, "mempool/history", api::HandleGetMempoolHistory, 30);
    AddRoute(HTTPRequest::GET, "network/nodes", api::HandleGetNetworkNodes, 60);
    AddRoute(HTTPRequest::GET, "overview",  api::HandleGetOverview,    10);
    AddRoute(HTTPRequest::GET, "wallet",    api::HandleGetWalletInfo,  15);
    AddRoute(HTTPRequest::GET, "events",    api::HandleGetEvents);
    AddRoute(HTTPRequest::GET, "health",    api::HandleGetHealth,       1);
    AddRoute(HTTPRequest::GET, "openapi.json", api::HandleGetOpenAPISchema, 3600);

    // ── Unauthenticated read-only legacy RPC surface ───────────────────
    //
    // Generic dispatcher: GET/POST /api/v1/rpc/<method>
    // Dedicated REST aliases for the methods most often needed by light
    // wallets so callers don't have to remember the JSON-RPC param order.
    AddRoute(HTTPRequest::GET,  "rpc",  api::HandleLegacyRpc);
    AddRoute(HTTPRequest::POST, "rpc",  api::HandleLegacyRpc);

    AddRoute(HTTPRequest::GET, "mining/estimatefee",   api::HandleEstimateFee, 5);
    AddRoute(HTTPRequest::GET, "mining/networkhashps", api::HandleGetNetworkHashPS, 30);
    AddRoute(HTTPRequest::GET, "mining/difficulty",    api::HandleGetDifficultyRpc, 30);

    AddRoute(HTTPRequest::GET, "chain/best-block-hash", api::HandleGetBestBlockHash, 5);
    AddRoute(HTTPRequest::GET, "chain/block-count",     api::HandleGetBlockCount,     5);
    AddRoute(HTTPRequest::GET, "chain/block-hash",      api::HandleGetBlockHash,     30);
    AddRoute(HTTPRequest::GET, "chain/block-header",    api::HandleGetBlockHeader,   30);
    AddRoute(HTTPRequest::GET, "chain/tips",            api::HandleGetChainTips,     30);
    AddRoute(HTTPRequest::GET, "chain/txout",           api::HandleGetTxOut,          5);

    AddRoute(HTTPRequest::GET, "mempool/raw",  api::HandleGetRawMempool,  5);
    AddRoute(HTTPRequest::GET, "mempool/info", api::HandleGetMempoolInfo, 5);

    AddRoute(HTTPRequest::POST, "scripts/decode",       api::HandleDecodeScript);
    AddRoute(HTTPRequest::POST, "txs/decoderawtransaction", api::HandleDecodeTx);

    // Merge-mining / Stratum / share-chain visibility endpoints
    // (ported from lotused on consolidation; see scryptchain + stratum
    // libraries for the underlying state).
    AddRoute(HTTPRequest::GET, "stratum",          api::HandleGetStratumInfo,    5);
    AddRoute(HTTPRequest::GET, "stratum/workers",  api::HandleGetStratumWorkers, 5);
    AddRoute(HTTPRequest::GET, "sharechain",       api::HandleGetShareChainInfo, 5);
    AddRoute(HTTPRequest::GET, "scryptchains",     api::HandleGetScryptChains,   5);

    api::StartEvents();
    api::StartStatsCollector(context);
    api::StartSitemapPreheater();

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

    // Sitemap & robots — root-level discovery endpoints for crawlers.
    auto sitemapHandler = [](Config &, HTTPRequest *req,
                             const std::string &suffix) {
        // suffix is the substring after the registered prefix.
        // For "/sitemap" prefix, suffix is e.g. ".xml" or "-blocks-3.xml".
        return api::HandleSitemap(req, "/sitemap" + suffix);
    };
    RegisterHTTPHandler("/sitemap", false, sitemapHandler);

    auto robotsHandler = [](Config &, HTTPRequest *req, const std::string &) {
        return api::HandleSitemap(req, "/robots.txt");
    };
    RegisterHTTPHandler("/robots.txt", true, robotsHandler);

    // Favicon — same SVG payload served for both modern (.svg) and legacy
    // (.ico) URLs so browsers and crawlers always get an icon.
    auto faviconHandler = [](Config &, HTTPRequest *req, const std::string &) {
        return api::HandleFavicon(req);
    };
    RegisterHTTPHandler("/favicon.svg", true, faviconHandler);
    RegisterHTTPHandler("/favicon.ico", true, faviconHandler);
}

void InterruptAPI() {}

void StopAPI() {
    LogPrintf("Stopping REST API v1\n");
    api::StopStatsCollector();
    api::StopEvents();
    api::StopSitemapPreheater();
    UnregisterHTTPHandler("/favicon.ico", true);
    UnregisterHTTPHandler("/favicon.svg", true);
    UnregisterHTTPHandler("/robots.txt", true);
    UnregisterHTTPHandler("/sitemap", false);
    UnregisterHTTPHandler("/dashboard", false);
    UnregisterHTTPHandler(API_PREFIX, false);
    g_routes.clear();
    {
        std::lock_guard<std::mutex> lock(g_cache_mutex);
        g_cache.clear();
    }
}
