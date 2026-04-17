// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_STRATUM_STRATUMROUTER_H
#define BITCOIN_STRATUM_STRATUMROUTER_H

#include <script/script.h>
#include <stratum/stratumconfig.h>
#include <stratum/stratumjob.h>
#include <stratum/stratumproxy.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class CChainParams;
class CChainState;
class ChainstateManager;
class CTxMemPool;
class Config;

namespace stratum {

enum class RoutingTier {
    NONE,
    LOCAL,
    PROXY,
};

std::string TierName(RoutingTier tier);

struct RouterDownstreamCallbacks {
    std::function<void(const std::string &rawNotifyLine)> broadcastNotify;
    std::function<void(double difficulty)> broadcastDifficulty;
    std::function<void(int64_t minerId, bool accepted,
                       const std::string &error)>
        submitResult;
    std::function<void(RoutingTier newTier, const std::string &detail)>
        tierChanged;
};

class StratumRouter {
public:
    StratumRouter(const StratumConfig &config,
                  const std::vector<UpstreamPool> &pools,
                  const Config &nodeConfig,
                  CChainState &chainstate, const CTxMemPool *mempool,
                  const CChainParams &chainParams,
                  ChainstateManager &chainman,
                  const CScript &coinbaseScript);
    ~StratumRouter();

    void SetCallbacks(RouterDownstreamCallbacks callbacks);

    bool Start();
    void Stop();

    RoutingTier GetActiveTier() const { return m_activeTier.load(); }
    bool IsLocalAvailable() const;
    void OnNewTip(int height);

    bool RouteSubmit(int64_t minerId, uint64_t jobId,
                     const std::string &extranonce1,
                     const UniValue &submitParams,
                     const StratumJob **outJob);

    StratumJobManager *GetJobManager() { return m_jobMgr.get(); }
    int GetActiveProxyIndex() const;
    size_t GetProxyCount() const { return m_proxies.size(); }
    ProxyHealth GetProxyHealth(size_t index) const;

private:
    StratumConfig m_config;
    std::vector<UpstreamPool> m_pools;
    const Config &m_nodeConfig;
    CChainState &m_chainstate;
    const CChainParams &m_chainParams;
    ChainstateManager &m_chainman;

    std::unique_ptr<StratumJobManager> m_jobMgr;
    std::vector<std::unique_ptr<StratumProxyConn>> m_proxies;

    RouterDownstreamCallbacks m_downstream;

    std::atomic<RoutingTier> m_activeTier{RoutingTier::NONE};
    std::atomic<int> m_activeProxyIdx{-1};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_warnedSoloMining{false};

    std::thread m_healthThread;
    mutable std::mutex m_tierMutex;

    void HealthCheckLoop();
    void EvaluateAndSwitch();
    void SwitchToLocal();
    void SwitchToProxy(int index);
    void SwitchToNone();
    void ConnectProxy(int index);

    ProxyCallbacks MakeProxyCallbacks(int index);
    void LocalCreateAndBroadcast(bool cleanJobs);

    static constexpr int HEALTH_CHECK_INTERVAL_SEC = 5;
    static constexpr int LOCAL_SYNC_THRESHOLD = 10;
};

} // namespace stratum

#endif // BITCOIN_STRATUM_STRATUMROUTER_H
