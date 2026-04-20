// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/stratum_handler.h>

#include <config.h>
#include <httpserver.h>
#include <key_io.h>
#include <net_processing.h>
#include <scryptchain/header_chain.h>
#include <scryptchain/mempool.h>
#include <scryptchain/network_manager.h>
#include <scryptchain/scryptchain_globals.h>
#include <sharechain/sharechain.h>
#include <stratum/stratum.h>
#include <stratum/stratumstats.h>
#include <univalue.h>

namespace api {

bool HandleGetStratumInfo(const util::Ref &, HTTPRequest *req,
                          const std::vector<std::string> &,
                          const QueryParams &) {
    stratum::StratumServer *server = stratum::GetStratumServer();
    if (!server) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("enabled", false);
        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(200, obj.write(2) + "\n");
        return true;
    }

    stratum::StratumServerStats stats = server->GetStats();
    UniValue json = stratum::FormatStatsJson(stats);
    json.pushKV("enabled", true);

    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(200, json.write(2) + "\n");
    return true;
}

bool HandleGetShareChainInfo(const util::Ref &, HTTPRequest *req,
                             const std::vector<std::string> &,
                             const QueryParams &) {
    sharechain::ShareChain *sc = g_sharechain;
    if (!sc) {
        UniValue obj(UniValue::VOBJ);
        obj.pushKV("enabled", false);
        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(200, obj.write(2) + "\n");
        return true;
    }

    const CChainParams &params = GetConfig().GetChainParams();

    sharechain::ShareChainStats scStats = sc->GetStats();

    UniValue obj(UniValue::VOBJ);
    obj.pushKV("enabled", true);
    obj.pushKV("height", (int64_t)scStats.height);
    obj.pushKV("windowSize", (int64_t)scStats.windowSize);
    obj.pushKV("sharesInWindow", (int64_t)scStats.sharesInWindow);
    obj.pushKV("shareDifficulty", scStats.shareDifficulty);
    obj.pushKV("shareRate", scStats.shareRate);

    UniValue payoutsArr(UniValue::VARR);
    for (const auto &p : scStats.payouts) {
        UniValue pObj(UniValue::VOBJ);
        CTxDestination dest;
        if (ExtractDestination(p.scriptPubKey, dest)) {
            pObj.pushKV("address", EncodeDestination(dest, params));
        } else {
            pObj.pushKV("address", HexStr(p.scriptPubKey));
        }
        pObj.pushKV("shares", (int64_t)p.shareCount);
        pObj.pushKV("percentage", p.percentage);
        pObj.pushKV("estimatedAmount", (int64_t)(p.amount / SATOSHI));
        payoutsArr.push_back(pObj);
    }
    obj.pushKV("payouts", payoutsArr);

    UniValue recentArr(UniValue::VARR);
    for (const auto &s : scStats.recentShares) {
        UniValue sObj(UniValue::VOBJ);
        sObj.pushKV("hash", s.GetHash().ToString().substr(0, 16));
        sObj.pushKV("height", (int64_t)s.nShareHeight);
        CTxDestination dest;
        if (ExtractDestination(s.scriptPubKey, dest)) {
            sObj.pushKV("miner", EncodeDestination(dest, params));
        } else {
            sObj.pushKV("miner", HexStr(s.scriptPubKey));
        }
        sObj.pushKV("algorithm", s.fAuxPow ? "auxpow" : "native");
        sObj.pushKV("time", (int64_t)s.nTime);
        recentArr.push_back(sObj);
    }
    obj.pushKV("recentShares", recentArr);

    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(200, obj.write(2) + "\n");
    return true;
}

bool HandleGetStratumWorkers(const util::Ref &, HTTPRequest *req,
                             const std::vector<std::string> &,
                             const QueryParams &) {
    stratum::StratumServer *server = stratum::GetStratumServer();
    if (!server) {
        UniValue arr(UniValue::VARR);
        req->WriteHeader("Content-Type", "application/json");
        req->WriteReply(200, arr.write(2) + "\n");
        return true;
    }

    stratum::StratumServerStats stats = server->GetStats();

    UniValue arr(UniValue::VARR);
    for (const auto &w : stats.workers) {
        UniValue wObj(UniValue::VOBJ);
        wObj.pushKV("name", w.workerName);
        wObj.pushKV("payoutAddress", w.payoutAddress);
        wObj.pushKV("algorithm",
                    w.algorithm == stratum::MiningAlgorithm::AUXPOW
                        ? "auxpow"
                        : "native");
        wObj.pushKV("difficulty", w.currentDifficulty);
        wObj.pushKV("accepted", (int64_t)w.sharesAccepted);
        wObj.pushKV("rejected", (int64_t)w.sharesRejected);
        wObj.pushKV("stale", (int64_t)w.sharesStale);
        wObj.pushKV("hashrate", w.estimatedHashrate);
        wObj.pushKV("lastShareTime", (int64_t)w.lastShareTime);

        std::string stateStr;
        switch (w.state) {
            case stratum::StratumWorker::State::CONNECTED:
                stateStr = "connected";
                break;
            case stratum::StratumWorker::State::SUBSCRIBED:
                stateStr = "subscribed";
                break;
            case stratum::StratumWorker::State::AUTHORIZED:
                stateStr = "authorized";
                break;
            case stratum::StratumWorker::State::MINING:
                stateStr = "mining";
                break;
        }
        wObj.pushKV("state", stateStr);
        arr.push_back(wObj);
    }

    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(200, arr.write(2) + "\n");
    return true;
}

static UniValue FormatChainStatus(const char *name,
                                  scryptchain::ScryptHeaderChain *chain,
                                  scryptchain::ScryptNetworkManager *netMgr,
                                  scryptchain::ScryptMemPool *mempool) {
    UniValue obj(UniValue::VOBJ);
    if (!chain) {
        obj.pushKV("enabled", false);
        return obj;
    }

    obj.pushKV("enabled", true);
    obj.pushKV("synced", chain->IsSynced());
    obj.pushKV("height", (int64_t)chain->GetHeight());
    obj.pushKV("peers", (int64_t)(netMgr ? netMgr->GetPeerCount() : 0));

    if (mempool) {
        obj.pushKV("mempoolSize", (int64_t)mempool->GetSize());
        obj.pushKV("mempoolConfirmed", (int64_t)mempool->GetConfirmedSize());
        obj.pushKV("mempoolStaging", (int64_t)mempool->GetStagingSize());
        obj.pushKV("mempoolBytes", (int64_t)mempool->GetMemoryUsage());
    } else {
        obj.pushKV("mempoolSize", (int64_t)0);
        obj.pushKV("mempoolBytes", (int64_t)0);
    }

    return obj;
}

bool HandleGetScryptChains(const util::Ref &, HTTPRequest *req,
                           const std::vector<std::string> &,
                           const QueryParams &) {
    auto &g = scryptchain::GetScryptChainGlobals();

    UniValue result(UniValue::VOBJ);
    result.pushKV("litecoin",
                  FormatChainStatus("litecoin", g.ltcChain, g.ltcNetMgr,
                                    g.ltcMempool));
    result.pushKV("dogecoin",
                  FormatChainStatus("dogecoin", g.dogeChain, g.dogeNetMgr,
                                    g.dogeMempool));

    req->WriteHeader("Content-Type", "application/json");
    req->WriteReply(200, result.write(2) + "\n");
    return true;
}

} // namespace api
