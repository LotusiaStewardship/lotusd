// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stratum/stratumrouter.h>
#include <stratum/stratumworker.h>

#include <chainparams.h>
#include <config.h>
#include <logging.h>
#include <shutdown.h>
#include <tinyformat.h>
#include <util/time.h>
#include <validation.h>

#include <algorithm>

namespace stratum {

std::string TierName(RoutingTier tier) {
    switch (tier) {
        case RoutingTier::LOCAL:
            return "local";
        case RoutingTier::PROXY:
            return "proxy";
        case RoutingTier::NONE:
            return "none";
    }
    return "unknown";
}

StratumRouter::StratumRouter(const StratumConfig &config,
                             const std::vector<UpstreamPool> &pools,
                             const Config &nodeConfig,
                             CChainState &chainstate,
                             const CTxMemPool *mempool,
                             const CChainParams &chainParams,
                             ChainstateManager &chainman,
                             const CScript &coinbaseScript)
    : m_config(config), m_pools(pools), m_nodeConfig(nodeConfig),
      m_chainstate(chainstate), m_chainParams(chainParams),
      m_chainman(chainman) {

    std::sort(m_pools.begin(), m_pools.end(),
              [](const UpstreamPool &a, const UpstreamPool &b) {
                  return a.priority < b.priority;
              });

    m_jobMgr = std::make_unique<StratumJobManager>(
        nodeConfig, chainstate, mempool, chainParams, coinbaseScript,
        4, StratumWorker::EXTRANONCE2_SIZE);
}

StratumRouter::~StratumRouter() {
    Stop();
}

void StratumRouter::SetCallbacks(RouterDownstreamCallbacks callbacks) {
    m_downstream = std::move(callbacks);
}

bool StratumRouter::Start() {
    m_running.store(true);

    for (size_t i = 0; i < m_pools.size(); ++i) {
        auto proxy = std::make_unique<StratumProxyConn>(
            m_pools[i], MakeProxyCallbacks(static_cast<int>(i)));
        m_proxies.push_back(std::move(proxy));
    }

    EvaluateAndSwitch();
    m_healthThread = std::thread([this]() { HealthCheckLoop(); });

    return true;
}

void StratumRouter::Stop() {
    m_running.store(false);

    if (m_healthThread.joinable()) {
        m_healthThread.join();
    }

    for (auto &proxy : m_proxies) {
        proxy->Disconnect();
    }
    m_proxies.clear();
}

bool StratumRouter::IsLocalAvailable() const {
    LOCK(cs_main);
    const CBlockIndex *tip = m_chainstate.m_chain.Tip();
    if (!tip) {
        return false;
    }
    int64_t now = GetTime();
    int64_t tipTime = tip->GetBlockTime();
    return (now - tipTime) < (LOCAL_SYNC_THRESHOLD * 60);
}

void StratumRouter::OnNewTip(int height) {
    if (m_activeTier.load() == RoutingTier::LOCAL) {
        LogPrint(BCLog::MINING,
                 "Stratum router: new tip at height %d, refreshing job\n",
                 height);
        LocalCreateAndBroadcast(true);
    }
    EvaluateAndSwitch();
}

bool StratumRouter::RouteSubmit(int64_t minerId, uint64_t jobId,
                                 const std::string &extranonce1,
                                 const UniValue &submitParams,
                                 const StratumJob **outJob) {
    RoutingTier tier = m_activeTier.load();

    if (tier == RoutingTier::LOCAL) {
        if (outJob && m_jobMgr) {
            *outJob = m_jobMgr->GetJob(jobId);
        }
        return true;
    }

    if (tier == RoutingTier::PROXY) {
        int idx = m_activeProxyIdx.load();
        if (idx >= 0 && idx < (int)m_proxies.size() &&
            m_proxies[idx]->IsConnected()) {
            m_proxies[idx]->ForwardSubmit(minerId, submitParams);
            return true;
        }
    }

    LogPrintf("Stratum router: WARNING -- submit dropped, no backend\n");
    return false;
}

int StratumRouter::GetActiveProxyIndex() const {
    if (m_activeTier.load() != RoutingTier::PROXY) {
        return -1;
    }
    return m_activeProxyIdx.load();
}

ProxyHealth StratumRouter::GetProxyHealth(size_t index) const {
    if (index < m_proxies.size()) {
        return m_proxies[index]->GetHealth();
    }
    return {};
}

void StratumRouter::HealthCheckLoop() {
    while (m_running.load() && !ShutdownRequested()) {
        EvaluateAndSwitch();
        for (int i = 0; i < HEALTH_CHECK_INTERVAL_SEC && m_running.load() &&
                         !ShutdownRequested();
             ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void StratumRouter::EvaluateAndSwitch() {
    std::lock_guard<std::mutex> lock(m_tierMutex);

    RoutingTier current = m_activeTier.load();
    bool localAvail = IsLocalAvailable();

    if (m_config.preferLocal) {
        if (localAvail) {
            if (current != RoutingTier::LOCAL) {
                SwitchToLocal();
            }
            return;
        }
        for (int i = 0; i < (int)m_proxies.size(); ++i) {
            if (m_proxies[i]->IsHealthy()) {
                if (current != RoutingTier::PROXY ||
                    m_activeProxyIdx.load() != i) {
                    SwitchToProxy(i);
                }
                return;
            }
            if (!m_proxies[i]->IsConnected()) {
                ConnectProxy(i);
                if (m_proxies[i]->IsHealthy()) {
                    SwitchToProxy(i);
                    return;
                }
            }
        }
    } else {
        for (int i = 0; i < (int)m_proxies.size(); ++i) {
            if (m_proxies[i]->IsHealthy()) {
                if (current != RoutingTier::PROXY ||
                    m_activeProxyIdx.load() != i) {
                    SwitchToProxy(i);
                }
                return;
            }
            if (!m_proxies[i]->IsConnected()) {
                ConnectProxy(i);
                if (m_proxies[i]->IsHealthy()) {
                    SwitchToProxy(i);
                    return;
                }
            }
        }
        if (localAvail) {
            if (current != RoutingTier::LOCAL) {
                SwitchToLocal();
            }
            return;
        }
    }

    if (current != RoutingTier::NONE) {
        SwitchToNone();
    }
}

void StratumRouter::SwitchToLocal() {
    m_activeTier.store(RoutingTier::LOCAL);
    m_activeProxyIdx.store(-1);

    if (m_config.warnSoloMining) {
        LogPrintf("Stratum router: WARNING -- switched to LOCAL solo mining\n");
        m_warnedSoloMining.store(true);
    }

    LogPrintf("Stratum router: tier switched to LOCAL\n");
    if (m_downstream.tierChanged) {
        m_downstream.tierChanged(RoutingTier::LOCAL, "node synced");
    }

    LocalCreateAndBroadcast(true);
}

void StratumRouter::SwitchToProxy(int index) {
    m_activeTier.store(RoutingTier::PROXY);
    m_activeProxyIdx.store(index);
    m_warnedSoloMining.store(false);

    std::string label = m_proxies[index]->GetLabel();
    LogPrintf("Stratum router: tier switched to PROXY [%s]\n", label);
    if (m_downstream.tierChanged) {
        m_downstream.tierChanged(RoutingTier::PROXY,
                                 strprintf("upstream %s", label));
    }
}

void StratumRouter::SwitchToNone() {
    m_activeTier.store(RoutingTier::NONE);
    m_activeProxyIdx.store(-1);

    LogPrintf("Stratum router: WARNING -- no backend available!\n");
    if (m_downstream.tierChanged) {
        m_downstream.tierChanged(RoutingTier::NONE, "no backend available");
    }
}

void StratumRouter::ConnectProxy(int index) {
    if (index < 0 || index >= (int)m_proxies.size()) {
        return;
    }
    if (m_proxies[index]->IsConnected()) {
        return;
    }
    LogPrint(BCLog::MINING, "Stratum router: connecting proxy %s\n",
             m_proxies[index]->GetLabel());
    m_proxies[index]->Connect();
}

ProxyCallbacks StratumRouter::MakeProxyCallbacks(int index) {
    ProxyCallbacks cbs;

    cbs.onNotify = [this, index](const std::string &raw) {
        if (m_activeTier.load() == RoutingTier::PROXY &&
            m_activeProxyIdx.load() == index) {
            if (m_downstream.broadcastNotify) {
                m_downstream.broadcastNotify(raw);
            }
        }
    };

    cbs.onSetDifficulty = [this, index](double diff) {
        if (m_activeTier.load() == RoutingTier::PROXY &&
            m_activeProxyIdx.load() == index) {
            if (m_downstream.broadcastDifficulty) {
                m_downstream.broadcastDifficulty(diff);
            }
        }
    };

    cbs.onSubmitResult = [this](int64_t minerId, bool accepted,
                                const std::string &error) {
        if (m_downstream.submitResult) {
            m_downstream.submitResult(minerId, accepted, error);
        }
    };

    cbs.onStateChange = [this, index](bool connected,
                                       const std::string &reason) {
        if (!connected) {
            if (m_activeTier.load() == RoutingTier::PROXY &&
                m_activeProxyIdx.load() == index) {
                LogPrintf("Stratum router: active proxy went down (%s)\n",
                          reason);
            }
        }
    };

    return cbs;
}

void StratumRouter::LocalCreateAndBroadcast(bool cleanJobs) {
    if (!m_jobMgr) {
        return;
    }

    StratumJob job;
    std::string error;
    if (!m_jobMgr->CreateJob(cleanJobs, job, error)) {
        LogPrintf("Stratum router: failed to create local job: %s\n", error);
        return;
    }

    m_jobMgr->PruneJobs(m_config.jobCacheSize);

    if (m_downstream.broadcastNotify) {
        UniValue notifyParams = m_jobMgr->FormatNotifyParams(job);
        std::string data = SerializeNotify("mining.notify", notifyParams);
        m_downstream.broadcastNotify(data);
    }
}

} // namespace stratum
