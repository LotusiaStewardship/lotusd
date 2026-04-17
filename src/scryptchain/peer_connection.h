// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRYPTCHAIN_PEER_CONNECTION_H
#define BITCOIN_SCRYPTCHAIN_PEER_CONNECTION_H

#include <scryptchain/chain_params.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

struct bufferevent;
struct event_base;

namespace scryptchain {

enum class PeerState {
    CONNECTING,
    VERSION_SENT,
    VERACK_RECEIVED,
    READY,
    DISCONNECTED,
};

struct PeerMessage {
    std::string command;
    std::vector<uint8_t> payload;
};

using MessageCallback =
    std::function<void(int peerId, const PeerMessage &msg)>;
using DisconnectCallback = std::function<void(int peerId)>;

/**
 * Single TCP connection to a Scrypt-chain peer.
 * Uses libevent bufferevent for async I/O.
 * Bitcoin-style message framing: 4-byte magic + 12-byte command +
 * 4-byte payload size + 4-byte checksum + payload.
 */
class ScryptPeerConnection {
public:
    static constexpr size_t HEADER_SIZE = 4 + 12 + 4 + 4; // 24 bytes
    static constexpr size_t MAX_MESSAGE_SIZE = 4 * 1024 * 1024;

    ScryptPeerConnection(int id, const ScryptChainParams &params,
                         struct event_base *base);
    ~ScryptPeerConnection();

    bool Connect(const std::string &host, uint16_t port);
    void Disconnect();

    bool SendMessage(const std::string &command,
                     const std::vector<uint8_t> &payload);
    bool SendMessage(const std::string &command, const uint8_t *data,
                     size_t len);

    void SetMessageCallback(MessageCallback cb) { m_onMessage = std::move(cb); }
    void SetDisconnectCallback(DisconnectCallback cb) {
        m_onDisconnect = std::move(cb);
    }

    int GetId() const { return m_id; }
    PeerState GetState() const { return m_state; }
    void SetState(PeerState state) { m_state = state; }
    const std::string &GetAddress() const { return m_address; }
    int64_t GetLastActivity() const { return m_lastActivity; }
    int GetStartingHeight() const { return m_startingHeight; }
    void SetStartingHeight(int h) { m_startingHeight = h; }

    bool IsReady() const { return m_state == PeerState::READY; }

    void UpdateActivity();

private:
    static void OnRead(struct bufferevent *bev, void *ctx);
    static void OnEvent(struct bufferevent *bev, short events, void *ctx);

    void ProcessRecvBuffer();
    bool ParseMessage(PeerMessage &msg);
    void ComputeChecksum(const uint8_t *data, size_t len,
                         uint8_t checksum[4]) const;

    int m_id;
    const ScryptChainParams &m_params;
    struct event_base *m_base;
    struct bufferevent *m_bev{nullptr};
    PeerState m_state{PeerState::CONNECTING};
    std::string m_address;
    int64_t m_lastActivity{0};
    int m_startingHeight{0};

    std::vector<uint8_t> m_recvBuffer;

    MessageCallback m_onMessage;
    DisconnectCallback m_onDisconnect;
};

} // namespace scryptchain

#endif // BITCOIN_SCRYPTCHAIN_PEER_CONNECTION_H
