// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <scryptchain/peer_connection.h>

#include <crypto/sha256.h>
#include <logging.h>
#include <util/time.h>

#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <event2/event.h>
#include <event2/util.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>

#include <algorithm>
#include <cassert>
#include <cstring>

namespace scryptchain {

ScryptPeerConnection::ScryptPeerConnection(int id,
                                           const ScryptChainParams &params,
                                           struct event_base *base)
    : m_id(id), m_params(params), m_base(base) {
    m_lastActivity = GetTime();
}

ScryptPeerConnection::~ScryptPeerConnection() {
    Disconnect();
}

bool ScryptPeerConnection::Connect(const std::string &host, uint16_t port) {
    m_address = host + ":" + std::to_string(port);

    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(port);
    int err = getaddrinfo(host.c_str(), portStr.c_str(), &hints, &res);
    if (err != 0 || !res) {
        LogPrintf("ScryptPeer(%s): DNS resolution failed: %s\n", m_address,
                  gai_strerror(err));
        return false;
    }

    m_bev = bufferevent_socket_new(m_base, -1, BEV_OPT_CLOSE_ON_FREE);
    if (!m_bev) {
        freeaddrinfo(res);
        return false;
    }

    bufferevent_setcb(m_bev, OnRead, nullptr, OnEvent, this);
    bufferevent_enable(m_bev, EV_READ | EV_WRITE);

    if (bufferevent_socket_connect(m_bev, res->ai_addr,
                                   static_cast<int>(res->ai_addrlen)) < 0) {
        LogPrintf("ScryptPeer(%s): connect failed\n", m_address);
        bufferevent_free(m_bev);
        m_bev = nullptr;
        freeaddrinfo(res);
        return false;
    }

    freeaddrinfo(res);
    m_state = PeerState::CONNECTING;
    LogPrint(BCLog::NET, "ScryptPeer(%s): connecting...\n", m_address);
    return true;
}

void ScryptPeerConnection::Disconnect() {
    if (m_bev) {
        bufferevent_free(m_bev);
        m_bev = nullptr;
    }
    if (m_state != PeerState::DISCONNECTED) {
        m_state = PeerState::DISCONNECTED;
        if (m_onDisconnect) {
            m_onDisconnect(m_id);
        }
    }
}

bool ScryptPeerConnection::SendMessage(const std::string &command,
                                       const std::vector<uint8_t> &payload) {
    return SendMessage(command, payload.data(), payload.size());
}

bool ScryptPeerConnection::SendMessage(const std::string &command,
                                       const uint8_t *data, size_t len) {
    if (!m_bev || m_state == PeerState::DISCONNECTED) {
        return false;
    }

    std::vector<uint8_t> header(HEADER_SIZE);

    // Magic bytes
    std::memcpy(header.data(), m_params.netMagic, 4);

    // Command (12 bytes, zero-padded)
    std::memset(header.data() + 4, 0, 12);
    size_t cmdLen = std::min(command.size(), size_t{12});
    std::memcpy(header.data() + 4, command.data(), cmdLen);

    // Payload length (4 bytes LE)
    uint32_t payloadLen = static_cast<uint32_t>(len);
    std::memcpy(header.data() + 16, &payloadLen, 4);

    // Checksum (4 bytes)
    uint8_t checksum[4];
    ComputeChecksum(data, len, checksum);
    std::memcpy(header.data() + 20, checksum, 4);

    struct evbuffer *output = bufferevent_get_output(m_bev);
    evbuffer_add(output, header.data(), header.size());
    if (len > 0) {
        evbuffer_add(output, data, len);
    }

    return true;
}

void ScryptPeerConnection::ComputeChecksum(const uint8_t *data, size_t len,
                                           uint8_t checksum[4]) const {
    uint8_t hash1[CSHA256::OUTPUT_SIZE], hash2[CSHA256::OUTPUT_SIZE];
    CSHA256().Write(data, len).Finalize(hash1);
    CSHA256().Write(hash1, sizeof(hash1)).Finalize(hash2);
    std::memcpy(checksum, hash2, 4);
}

void ScryptPeerConnection::OnRead(struct bufferevent *bev, void *ctx) {
    auto *peer = static_cast<ScryptPeerConnection *>(ctx);
    struct evbuffer *input = bufferevent_get_input(bev);

    size_t available = evbuffer_get_length(input);
    if (available == 0) {
        return;
    }

    size_t oldSize = peer->m_recvBuffer.size();
    peer->m_recvBuffer.resize(oldSize + available);
    evbuffer_remove(input, peer->m_recvBuffer.data() + oldSize, available);

    peer->ProcessRecvBuffer();
    peer->UpdateActivity();
}

void ScryptPeerConnection::OnEvent(struct bufferevent *bev, short events,
                                   void *ctx) {
    auto *peer = static_cast<ScryptPeerConnection *>(ctx);

    if (events & BEV_EVENT_CONNECTED) {
        LogPrint(BCLog::NET, "ScryptPeer(%s): connected\n", peer->m_address);
        peer->UpdateActivity();
        return;
    }

    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR | BEV_EVENT_TIMEOUT)) {
        LogPrint(BCLog::NET, "ScryptPeer(%s): disconnected (events=%d)\n",
                 peer->m_address, events);
        peer->m_bev = nullptr;
        bev = nullptr;
        peer->m_state = PeerState::DISCONNECTED;
        if (peer->m_onDisconnect) {
            peer->m_onDisconnect(peer->m_id);
        }
    }
}

void ScryptPeerConnection::ProcessRecvBuffer() {
    PeerMessage msg;
    while (ParseMessage(msg)) {
        if (m_onMessage) {
            m_onMessage(m_id, msg);
        }
    }
}

bool ScryptPeerConnection::ParseMessage(PeerMessage &msg) {
    if (m_recvBuffer.size() < HEADER_SIZE) {
        return false;
    }

    // Verify magic
    if (std::memcmp(m_recvBuffer.data(), m_params.netMagic, 4) != 0) {
        LogPrintf("ScryptPeer(%s): bad magic, disconnecting\n", m_address);
        Disconnect();
        return false;
    }

    // Extract command
    char cmdBuf[13]{};
    std::memcpy(cmdBuf, m_recvBuffer.data() + 4, 12);
    msg.command = std::string(cmdBuf);

    // Payload length
    uint32_t payloadLen;
    std::memcpy(&payloadLen, m_recvBuffer.data() + 16, 4);

    if (payloadLen > MAX_MESSAGE_SIZE) {
        LogPrintf("ScryptPeer(%s): message too large (%u), disconnecting\n",
                  m_address, payloadLen);
        Disconnect();
        return false;
    }

    size_t totalLen = HEADER_SIZE + payloadLen;
    if (m_recvBuffer.size() < totalLen) {
        return false;
    }

    // Verify checksum
    uint8_t expectedChecksum[4];
    ComputeChecksum(m_recvBuffer.data() + HEADER_SIZE, payloadLen,
                    expectedChecksum);
    if (std::memcmp(m_recvBuffer.data() + 20, expectedChecksum, 4) != 0) {
        LogPrintf("ScryptPeer(%s): bad checksum for %s, disconnecting\n",
                  m_address, msg.command);
        Disconnect();
        return false;
    }

    msg.payload.assign(m_recvBuffer.begin() + HEADER_SIZE,
                       m_recvBuffer.begin() + totalLen);

    m_recvBuffer.erase(m_recvBuffer.begin(),
                       m_recvBuffer.begin() + totalLen);
    return true;
}

void ScryptPeerConnection::UpdateActivity() {
    m_lastActivity = GetTime();
}

} // namespace scryptchain
