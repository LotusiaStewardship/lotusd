// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <scryptchain/network_manager.h>

#include <scryptchain/msg_handler.h>
#include <logging.h>
#include <util/time.h>

#include <event2/event.h>

#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
// <windows.h> (transitively included by winsock2.h) #define-s SendMessage to
// either SendMessageA or SendMessageW, which collides with this class's own
// SendMessage() member function and the ScryptPeerConnection::SendMessage
// it forwards to. We don't use the Win32 USER message API here, so just
// undefine the macro.
#ifdef SendMessage
#undef SendMessage
#endif
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#endif

#include <algorithm>
#include <random>

namespace scryptchain {

ScryptNetworkManager::ScryptNetworkManager(const ScryptChainParams &params,
                                           int maxOutbound)
    : m_params(params), m_maxOutbound(maxOutbound) {}

ScryptNetworkManager::~ScryptNetworkManager() {
    Stop();
}

bool ScryptNetworkManager::Start(std::string &error) {
    if (m_running) {
        error = "Already running";
        return false;
    }

    m_eventBase = event_base_new();
    if (!m_eventBase) {
        error = "Failed to create event base";
        return false;
    }

    m_running = true;
    m_interrupt = false;

    m_thread = std::thread([this] { EventLoop(); });

    LogPrintf("ScryptNetworkManager(%s): started\n", m_params.name);
    return true;
}

void ScryptNetworkManager::Interrupt() {
    m_interrupt = true;
    if (m_eventBase) {
        event_base_loopbreak(m_eventBase);
    }
}

void ScryptNetworkManager::Stop() {
    Interrupt();
    if (m_thread.joinable()) {
        m_thread.join();
    }

    {
        std::lock_guard<std::mutex> lock(m_cs);
        m_peers.clear();
    }

    if (m_maintenanceTimer) {
        event_free(m_maintenanceTimer);
        m_maintenanceTimer = nullptr;
    }
    if (m_eventBase) {
        event_base_free(m_eventBase);
        m_eventBase = nullptr;
    }

    m_running = false;
    LogPrintf("ScryptNetworkManager(%s): stopped\n", m_params.name);
}

void ScryptNetworkManager::EventLoop() {
    ResolveDnsSeeds();
    MaintainConnections();

    struct timeval tv = {10, 0};
    m_maintenanceTimer =
        event_new(m_eventBase, -1, EV_PERSIST, OnMaintenance, this);
    event_add(m_maintenanceTimer, &tv);

    while (!m_interrupt) {
        event_base_loop(m_eventBase, EVLOOP_ONCE);
    }
}

void ScryptNetworkManager::OnMaintenance(evutil_socket_t /*fd*/,
                                         short /*what*/, void *ctx) {
    auto *mgr = static_cast<ScryptNetworkManager *>(ctx);
    if (mgr->m_interrupt) {
        return;
    }
    mgr->MaintainConnections();
}

void ScryptNetworkManager::ResolveDnsSeeds() {
    LogPrintf("ScryptNetworkManager(%s): resolving DNS seeds...\n",
              m_params.name);

    std::vector<std::pair<std::string, uint16_t>> addresses;

    for (const auto &seed : m_params.dnsSeeds) {
        struct addrinfo hints{}, *result = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        int err = getaddrinfo(seed.c_str(), nullptr, &hints, &result);
        if (err != 0 || !result) {
            LogPrintf("ScryptNetworkManager(%s): seed %s failed: %s\n",
                      m_params.name, seed, gai_strerror(err));
            continue;
        }

        for (auto *rp = result; rp; rp = rp->ai_next) {
            char addrBuf[INET6_ADDRSTRLEN];
            if (rp->ai_family == AF_INET) {
                auto *sin = reinterpret_cast<struct sockaddr_in *>(rp->ai_addr);
                inet_ntop(AF_INET, &sin->sin_addr, addrBuf, sizeof(addrBuf));
            } else if (rp->ai_family == AF_INET6) {
                auto *sin6 =
                    reinterpret_cast<struct sockaddr_in6 *>(rp->ai_addr);
                inet_ntop(AF_INET6, &sin6->sin6_addr, addrBuf,
                          sizeof(addrBuf));
            } else {
                continue;
            }
            addresses.emplace_back(std::string(addrBuf), m_params.defaultPort);
        }

        freeaddrinfo(result);
    }

    // Shuffle
    std::random_device rd;
    std::mt19937 rng(rd());
    std::shuffle(addresses.begin(), addresses.end(), rng);

    {
        std::lock_guard<std::mutex> lock(m_addrMutex);
        for (auto &addr : addresses) {
            m_addressQueue.push_back(std::move(addr));
        }
        m_seedsResolved = true;
    }

    LogPrintf("ScryptNetworkManager(%s): resolved %d addresses from seeds\n",
              m_params.name, addresses.size());
}

void ScryptNetworkManager::MaintainConnections() {
    if (m_interrupt) {
        return;
    }

    // Disconnect stale peers (no activity for 5 minutes)
    {
        std::lock_guard<std::mutex> lock(m_cs);
        int64_t now = GetTime();
        std::vector<int> toRemove;
        for (auto &[id, peer] : m_peers) {
            if (peer->GetState() == PeerState::DISCONNECTED) {
                toRemove.push_back(id);
            } else if (now - peer->GetLastActivity() > 300) {
                LogPrint(BCLog::NET,
                         "ScryptNetworkManager(%s): peer %s stale, removing\n",
                         m_params.name, peer->GetAddress());
                toRemove.push_back(id);
            }
        }
        for (int id : toRemove) {
            m_peers.erase(id);
        }
    }

    // Connect to fill slots
    int currentCount = GetPeerCount();
    int needed = m_maxOutbound - currentCount;

    for (int i = 0; i < needed; ++i) {
        std::pair<std::string, uint16_t> addr;
        {
            std::lock_guard<std::mutex> lock(m_addrMutex);
            if (m_addressQueue.empty()) {
                break;
            }
            addr = m_addressQueue.front();
            m_addressQueue.pop_front();
            m_addressQueue.push_back(addr);
        }
        ConnectToAddress(addr.first, addr.second);
    }
}

void ScryptNetworkManager::ConnectToAddress(const std::string &host,
                                            uint16_t port) {
    // Check if already connected to this address
    {
        std::lock_guard<std::mutex> lock(m_cs);
        std::string addrStr = host + ":" + std::to_string(port);
        for (const auto &[_, peer] : m_peers) {
            if (peer->GetAddress() == addrStr) {
                return;
            }
        }
    }

    int peerId;
    {
        std::lock_guard<std::mutex> lock(m_cs);
        peerId = m_nextPeerId++;
    }

    auto peer = std::make_unique<ScryptPeerConnection>(peerId, m_params,
                                                       m_eventBase);
    peer->SetMessageCallback(
        [this](int id, const PeerMessage &msg) { OnPeerMessage(id, msg); });
    peer->SetDisconnectCallback(
        [this](int id) { OnPeerDisconnect(id); });

    if (!peer->Connect(host, port)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_cs);
    m_peers[peerId] = std::move(peer);
}

void ScryptNetworkManager::OnPeerMessage(int peerId, const PeerMessage &msg) {
    if (m_msgHandler) {
        m_msgHandler->ProcessMessage(peerId, msg);
    }
}

void ScryptNetworkManager::OnPeerDisconnect(int peerId) {
    LogPrint(BCLog::NET, "ScryptNetworkManager(%s): peer %d disconnected\n",
             m_params.name, peerId);
}

void ScryptNetworkManager::SubmitBlock(const std::vector<uint8_t> &blockData) {
    BroadcastMessage("block", blockData);
}

void ScryptNetworkManager::SendMessage(int peerId, const std::string &command,
                                       const std::vector<uint8_t> &payload) {
    std::lock_guard<std::mutex> lock(m_cs);
    auto it = m_peers.find(peerId);
    if (it != m_peers.end()) {
        it->second->SendMessage(command, payload);
    }
}

void ScryptNetworkManager::BroadcastMessage(
    const std::string &command, const std::vector<uint8_t> &payload) {
    std::lock_guard<std::mutex> lock(m_cs);
    for (auto &[_, peer] : m_peers) {
        if (peer->IsReady()) {
            peer->SendMessage(command, payload);
        }
    }
}

int ScryptNetworkManager::GetPeerCount() const {
    std::lock_guard<std::mutex> lock(m_cs);
    int count = 0;
    for (const auto &[_, peer] : m_peers) {
        if (peer->GetState() != PeerState::DISCONNECTED) {
            ++count;
        }
    }
    return count;
}

} // namespace scryptchain
