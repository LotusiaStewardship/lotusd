// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_STRATUM_STRATUM_H
#define BITCOIN_STRATUM_STRATUM_H

#include <stratum/stratumconfig.h>
#include <stratum/stratumjob.h>
#include <stratum/stratumprotocol.h>
#include <stratum/stratumrouter.h>
#include <stratum/stratumstats.h>
#include <stratum/stratumsubmit.h>
#include <stratum/stratumworker.h>
#include <validationinterface.h>

#include <atomic>
#include <map>
#include <memory>
#include <thread>

// For evutil_socket_t (used in OnAccept / HandleAccept signatures, must
// match libevent's evconnlistener_cb on every supported platform).
#include <event2/util.h>

struct event_base;
struct evconnlistener;
struct bufferevent;
struct sockaddr;

class CBlockIndex;
class CChainParams;
class CChainState;
class ChainstateManager;
class CTxMemPool;
class Config;

namespace sharechain {
class ShareChain;
}

namespace scryptchain {
class ScryptSolutionHandler;
class ScryptTemplateBuilder;
class ScryptHeaderChain;
class ScryptMemPool;
struct ScryptBlockTemplate;
}

namespace stratum {

class StratumServer final : public CValidationInterface {
public:
    StratumServer(const StratumConfig &config, const Config &nodeConfig,
                  CChainState &chainstate, const CTxMemPool *mempool,
                  const CChainParams &chainParams,
                  ChainstateManager &chainman);
    ~StratumServer();

    bool Start();
    void Interrupt();
    void Stop();

    void BroadcastJob(const StratumJob &job);
    void SendDifficulty(uint32_t sessionId, double difficulty);

    StratumServerStats GetStats() const;
    size_t GetWorkerCount() const;

    void SetShareChain(sharechain::ShareChain *sc) { m_shareChain = sc; }

    /** Trigger new job broadcast when external chain work updates. */
    void OnExternalWorkUpdate();

    void SetMultiChainComponents(
        scryptchain::ScryptTemplateBuilder *templateBuilder,
        scryptchain::ScryptSolutionHandler *solutionHandler,
        scryptchain::ScryptHeaderChain *ltcChain,
        scryptchain::ScryptHeaderChain *dogeChain,
        scryptchain::ScryptMemPool *ltcMempool,
        scryptchain::ScryptMemPool *dogeMempool,
        const CScript &ltcCoinbaseScript,
        const CScript &dogeCoinbaseScript) {
        m_templateBuilder = templateBuilder;
        m_solutionHandler = solutionHandler;
        m_ltcChain = ltcChain;
        m_dogeChain = dogeChain;
        m_ltcMempool = ltcMempool;
        m_dogeMempool = dogeMempool;
        m_ltcCoinbaseScript = ltcCoinbaseScript;
        m_dogeCoinbaseScript = dogeCoinbaseScript;
    }

protected:
    void UpdatedBlockTip(const CBlockIndex *pindexNew,
                         const CBlockIndex *pindexFork,
                         bool fInitialDownload) override;

private:
    struct ClientSession {
        uint32_t sessionId;
        std::unique_ptr<StratumWorker> worker;
        StratumLineBuffer lineBuffer;
        struct bufferevent *bev = nullptr;
        std::set<std::string> submittedNonces;
    };

    StratumConfig m_config;
    const Config &m_nodeConfig;
    CChainState &m_chainstate;
    const CChainParams &m_chainParams;
    ChainstateManager &m_chainman;

    std::unique_ptr<StratumRouter> m_router;
    ExtranonceMgr m_extranonceMgr;

    sharechain::ShareChain *m_shareChain = nullptr;

    scryptchain::ScryptTemplateBuilder *m_templateBuilder{nullptr};
    scryptchain::ScryptSolutionHandler *m_solutionHandler{nullptr};
    scryptchain::ScryptHeaderChain *m_ltcChain{nullptr};
    scryptchain::ScryptHeaderChain *m_dogeChain{nullptr};
    scryptchain::ScryptMemPool *m_ltcMempool{nullptr};
    scryptchain::ScryptMemPool *m_dogeMempool{nullptr};
    CScript m_ltcCoinbaseScript;
    CScript m_dogeCoinbaseScript;

    struct event_base *m_eventBase = nullptr;
    struct evconnlistener *m_listener = nullptr;
    std::thread m_eventThread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_interrupt{false};

    mutable RecursiveMutex m_cs;
    std::map<uint32_t, std::unique_ptr<ClientSession>> m_sessions
        GUARDED_BY(m_cs);
    uint32_t m_nextSessionId GUARDED_BY(m_cs) = 1;

    std::atomic<uint64_t> m_totalConnections{0};
    std::atomic<uint64_t> m_totalSharesAccepted{0};
    std::atomic<uint64_t> m_totalSharesRejected{0};
    std::atomic<uint64_t> m_totalSharesStale{0};
    std::atomic<uint64_t> m_blocksFound{0};
    int64_t m_startTime = 0;

    // libevent's evutil_socket_t is `int` on POSIX and `intptr_t` on Win64,
    // so use it (rather than plain `int`) for any function pointer that has
    // to match an evconnlistener_cb / bufferevent_socket_new signature.
    static void OnAccept(struct evconnlistener *listener, evutil_socket_t fd,
                         struct sockaddr *addr, int socklen, void *ctx);
    static void OnRead(struct bufferevent *bev, void *ctx);
    static void OnEvent(struct bufferevent *bev, short events, void *ctx);

    void HandleAccept(evutil_socket_t fd, struct sockaddr *addr);
    void HandleRead(uint32_t sessionId);
    void HandleDisconnect(uint32_t sessionId);

    void ProcessMessage(ClientSession &session, const StratumMessage &msg);
    void HandleSubscribe(ClientSession &session, const StratumRequest &req);
    void HandleAuthorize(ClientSession &session, const StratumRequest &req);
    void HandleSubmit(ClientSession &session, const StratumRequest &req);
    void HandleAuxPowSubmit(ClientSession &session, const StratumRequest &req);

    void SendToClient(ClientSession &session, const std::string &data);
    void SendResponse(ClientSession &session, int64_t id,
                      const UniValue &result, const UniValue &error);

    void CreateAndBroadcastJob(bool cleanJobs);
    void PopulateMultiChainJob(StratumJob &job);
    void PeriodicMaintenance();

    void BroadcastRawNotify(const std::string &rawLine);
    void BroadcastDifficultyAll(double difficulty);
    void HandleProxySubmitResult(int64_t minerId, bool accepted,
                                 const std::string &error);

    std::map<int64_t, uint32_t> m_pendingProxySubmits GUARDED_BY(m_cs);

    void EventLoop();
};

bool InitStratumServer(const StratumConfig &config, const Config &nodeConfig,
                       CChainState &chainstate, const CTxMemPool *mempool,
                       const CChainParams &chainParams,
                       ChainstateManager &chainman);
void StartStratumServer();
void InterruptStratumServer();
void StopStratumServer();
StratumServer *GetStratumServer();

} // namespace stratum

#endif // BITCOIN_STRATUM_STRATUM_H
