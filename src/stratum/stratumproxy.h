// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_STRATUM_STRATUMPROXY_H
#define BITCOIN_STRATUM_STRATUMPROXY_H

#include <stratum/stratumprotocol.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace stratum {

struct UpstreamPool {
    std::string host;
    uint16_t port = 3334;
    std::string username;
    std::string password = "x";
    int priority = 0;
};

struct ProxyHealth {
    bool connected = false;
    bool authorized = false;
    int64_t lastNotifyTime = 0;
    int64_t lastErrorTime = 0;
    int consecutiveErrors = 0;
    int64_t connectTime = 0;
    std::string lastError;
};

struct ProxyCallbacks {
    std::function<void(const std::string &rawNotifyLine)> onNotify;
    std::function<void(double difficulty)> onSetDifficulty;
    std::function<void(int64_t minerId, bool accepted,
                       const std::string &error)>
        onSubmitResult;
    std::function<void(bool connected, const std::string &reason)>
        onStateChange;
};

class StratumProxyConn {
public:
    StratumProxyConn(const UpstreamPool &pool, const ProxyCallbacks &callbacks);
    ~StratumProxyConn();

    bool Connect();
    void Disconnect();
    bool IsConnected() const { return m_connected.load(); }
    bool IsHealthy() const;

    void ForwardSubmit(int64_t minerId, const UniValue &params);

    const UpstreamPool &GetPool() const { return m_pool; }
    ProxyHealth GetHealth() const;
    std::string GetLabel() const;

    std::string GetUpstreamExtranonce1() const;
    int GetUpstreamExtranonce2Size() const;

private:
    UpstreamPool m_pool;
    ProxyCallbacks m_callbacks;

    int m_sockfd = -1;
    std::atomic<bool> m_connected{false};
    std::atomic<bool> m_authorized{false};
    std::atomic<bool> m_interrupt{false};
    std::thread m_readThread;

    mutable std::mutex m_writeMutex;
    mutable std::mutex m_healthMutex;

    std::string m_upstreamExtranonce1;
    int m_upstreamExtranonce2Size = 4;

    ProxyHealth m_health;
    StratumLineBuffer m_lineBuffer;
    int64_t m_nextReqId = 100;

    std::mutex m_pendingMutex;
    std::map<int64_t, int64_t> m_pendingSubmits;

    void ReadLoop();
    void HandleLine(const std::string &line);
    void HandleResponse(const StratumResponse &resp);
    void HandleNotification(const StratumNotification &notif);
    bool SendRaw(const std::string &data);
    bool DoSubscribe();
    bool DoAuthorize();
    void RecordError(const std::string &err);

    static constexpr int CONNECT_TIMEOUT_SEC = 10;
    static constexpr int HEALTHY_NOTIFY_STALE_SEC = 300;
    static constexpr int MAX_CONSECUTIVE_ERRORS = 5;
};

} // namespace stratum

#endif // BITCOIN_STRATUM_STRATUMPROXY_H
