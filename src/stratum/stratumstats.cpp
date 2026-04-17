// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stratum/stratumstats.h>

#include <util/time.h>

namespace stratum {

static StatsProvider g_statsProvider;

UniValue FormatStatsJson(const StratumServerStats &stats) {
    UniValue obj(UniValue::VOBJ);

    int64_t now = GetTime();
    obj.pushKV("uptime", now - stats.startTime);
    obj.pushKV("totalConnections", (int64_t)stats.totalConnections);
    obj.pushKV("activeWorkers", (int64_t)stats.activeWorkers);
    obj.pushKV("totalSharesAccepted", (int64_t)stats.totalSharesAccepted);
    obj.pushKV("totalSharesRejected", (int64_t)stats.totalSharesRejected);
    obj.pushKV("totalSharesStale", (int64_t)stats.totalSharesStale);
    obj.pushKV("blocksFound", (int64_t)stats.blocksFound);
    obj.pushKV("networkDifficulty", stats.networkDifficulty);
    obj.pushKV("chainHeight", (int64_t)stats.chainHeight);

    UniValue workersArr(UniValue::VARR);
    for (const auto &w : stats.workers) {
        UniValue wObj(UniValue::VOBJ);
        wObj.pushKV("name", w.workerName);
        wObj.pushKV("payoutAddress", w.payoutAddress);
        wObj.pushKV("algorithm",
                    w.algorithm == MiningAlgorithm::AUXPOW ? "auxpow"
                                                           : "native");
        wObj.pushKV("difficulty", w.currentDifficulty);
        wObj.pushKV("accepted", (int64_t)w.sharesAccepted);
        wObj.pushKV("rejected", (int64_t)w.sharesRejected);
        wObj.pushKV("stale", (int64_t)w.sharesStale);
        wObj.pushKV("hashrate", w.estimatedHashrate);
        wObj.pushKV("lastShareTime", w.lastShareTime);

        std::string stateStr;
        switch (w.state) {
            case StratumWorker::State::CONNECTED:
                stateStr = "connected";
                break;
            case StratumWorker::State::SUBSCRIBED:
                stateStr = "subscribed";
                break;
            case StratumWorker::State::AUTHORIZED:
                stateStr = "authorized";
                break;
            case StratumWorker::State::MINING:
                stateStr = "mining";
                break;
        }
        wObj.pushKV("state", stateStr);
        workersArr.push_back(wObj);
    }
    obj.pushKV("workers", workersArr);

    obj.pushKV("activeTier", stats.activeTier);
    obj.pushKV("activeProxyIndex", stats.activeProxyIndex);

    UniValue poolsArr(UniValue::VARR);
    for (const auto &p : stats.upstreamPools) {
        UniValue pObj(UniValue::VOBJ);
        pObj.pushKV("label", p.label);
        pObj.pushKV("connected", p.connected);
        pObj.pushKV("healthy", p.healthy);
        pObj.pushKV("priority", (int64_t)p.priority);
        pObj.pushKV("consecutiveErrors", (int64_t)p.consecutiveErrors);
        pObj.pushKV("lastError", p.lastError);
        poolsArr.push_back(pObj);
    }
    obj.pushKV("upstreamPools", poolsArr);

    return obj;
}

static bool StratumStatusHandler(Config &config, HTTPRequest *req,
                                  const std::string &) {
    if (!g_statsProvider) {
        req->WriteReply(503, "Stratum server not available");
        return true;
    }

    StratumServerStats stats = g_statsProvider();
    UniValue json = FormatStatsJson(stats);

    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(200, json.write(2) + "\n");
    return true;
}

void RegisterStratumHTTPHandlers(StatsProvider provider) {
    g_statsProvider = std::move(provider);
    RegisterHTTPHandler("/stratum/status", true, StratumStatusHandler);
}

void UnregisterStratumHTTPHandlers() {
    UnregisterHTTPHandler("/stratum/status", true);
    g_statsProvider = nullptr;
}

} // namespace stratum
