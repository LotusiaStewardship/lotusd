// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRYPTCHAIN_NETWORK_MANAGER_H
#define BITCOIN_SCRYPTCHAIN_NETWORK_MANAGER_H

#include <scryptchain/chain_params.h>
#include <scryptchain/peer_connection.h>

// For evutil_socket_t (used in OnMaintenance, must match libevent's
// event_callback_fn on every supported platform).
#include <event2/util.h>
// On Windows, <event2/util.h> transitively includes <winsock2.h> which
// pulls in <windows.h>, which #define-s SendMessage to SendMessageA /
// SendMessageW. That clobbers our SendMessage member function below
// (and would silently rename it in every translation unit that includes
// this header). We don't use the Win32 USER message API, so undefine
// the macro right at the source.
#ifdef SendMessage
#undef SendMessage
#endif

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct event_base;
struct event;

namespace scryptchain {

class ScryptHeaderChain;
class ScryptMsgHandler;

using TipUpdateCallback = std::function<void()>;

class ScryptNetworkManager {
public:
    ScryptNetworkManager(const ScryptChainParams &params,
                         int maxOutbound = 8);
    ~ScryptNetworkManager();

    bool Start(std::string &error);
    void Stop();
    void Interrupt();

    void SetMsgHandler(ScryptMsgHandler *handler) { m_msgHandler = handler; }
    void SetTipUpdateCallback(TipUpdateCallback cb) {
        m_tipUpdateCb = std::move(cb);
    }

    void SubmitBlock(const std::vector<uint8_t> &blockData);
    void SendMessage(int peerId, const std::string &command,
                     const std::vector<uint8_t> &payload);
    void BroadcastMessage(const std::string &command,
                          const std::vector<uint8_t> &payload);

    int GetPeerCount() const;
    const ScryptChainParams &GetParams() const { return m_params; }
    struct event_base *GetEventBase() { return m_eventBase; }

private:
    void EventLoop();
    void ResolveDnsSeeds();
    void MaintainConnections();
    void ConnectToAddress(const std::string &host, uint16_t port);
    void OnPeerMessage(int peerId, const PeerMessage &msg);
    void OnPeerDisconnect(int peerId);

    static void OnMaintenance(evutil_socket_t fd, short what, void *ctx);

    const ScryptChainParams &m_params;
    int m_maxOutbound;

    struct event_base *m_eventBase{nullptr};
    struct event *m_maintenanceTimer{nullptr};
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_interrupt{false};

    ScryptMsgHandler *m_msgHandler{nullptr};
    TipUpdateCallback m_tipUpdateCb;

    mutable std::mutex m_cs;
    std::map<int, std::unique_ptr<ScryptPeerConnection>> m_peers;
    int m_nextPeerId{1};

    std::mutex m_addrMutex;
    std::deque<std::pair<std::string, uint16_t>> m_addressQueue;
    bool m_seedsResolved{false};
};

} // namespace scryptchain

#endif // BITCOIN_SCRYPTCHAIN_NETWORK_MANAGER_H
