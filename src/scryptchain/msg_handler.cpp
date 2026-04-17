// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <scryptchain/msg_handler.h>

#include <hash.h>
#include <logging.h>
#include <primitives/parentheader.h>
#include <scryptchain/header_chain.h>
#include <scryptchain/network_manager.h>
#include <serialize.h>
#include <streams.h>
#include <util/time.h>

#include <cassert>
#include <random>

namespace scryptchain {

static constexpr int MAX_HEADERS_PER_MSG = 2000;

// Inv types matching Bitcoin protocol
static constexpr uint32_t MSG_TX = 1;
static constexpr uint32_t MSG_BLOCK = 2;
static constexpr uint32_t MSG_FILTERED_BLOCK = 3;
static constexpr uint32_t MSG_WITNESS_TX = 0x40000001;
static constexpr uint32_t MSG_WITNESS_BLOCK = 0x40000002;

ScryptMsgHandler::ScryptMsgHandler(const ScryptChainParams &params,
                                   ScryptHeaderChain &headerChain,
                                   ScryptNetworkManager &netMgr)
    : m_params(params), m_headerChain(headerChain), m_netMgr(netMgr) {}

ScryptMsgHandler::~ScryptMsgHandler() = default;

void ScryptMsgHandler::ProcessMessage(int peerId, const PeerMessage &msg) {
    if (msg.command == "version") {
        HandleVersion(peerId, msg.payload);
    } else if (msg.command == "verack") {
        HandleVerack(peerId);
    } else if (msg.command == "headers") {
        HandleHeaders(peerId, msg.payload);
    } else if (msg.command == "ping") {
        HandlePing(peerId, msg.payload);
    } else if (msg.command == "pong") {
        HandlePong(peerId, msg.payload);
    } else if (msg.command == "inv") {
        HandleInv(peerId, msg.payload);
    } else if (msg.command == "tx") {
        HandleTx(peerId, msg.payload);
    } else if (msg.command == "sendheaders") {
        HandleSendHeaders(peerId);
    } else if (msg.command == "sendcmpct" || msg.command == "feefilter" ||
               msg.command == "addr" || msg.command == "addrv2" ||
               msg.command == "wtxidrelay" || msg.command == "sendaddrv2" ||
               msg.command == "getheaders" || msg.command == "getaddr") {
        // Silently ignore these
    } else {
        LogPrint(BCLog::NET,
                 "ScryptMsgHandler(%s): unknown message '%s' from peer %d\n",
                 m_params.name, msg.command, peerId);
    }
}

void ScryptMsgHandler::HandleVersion(int peerId,
                                     const std::vector<uint8_t> &payload) {
    if (payload.size() < 46) {
        LogPrint(BCLog::NET,
                 "ScryptMsgHandler(%s): version too short from peer %d\n",
                 m_params.name, peerId);
        return;
    }

    int32_t version;
    std::memcpy(&version, payload.data(), 4);

    int32_t startHeight = 0;
    if (payload.size() >= 85) {
        std::memcpy(&startHeight, payload.data() + 80, 4);
    }

    LogPrint(BCLog::NET,
             "ScryptMsgHandler(%s): peer %d version=%d height=%d\n",
             m_params.name, peerId, version, startHeight);

    {
        std::lock_guard<std::mutex> lock(m_peerMutex);
        auto &state = m_peerStates[peerId];
        state.startingHeight = startHeight;
    }

    SendVerack(peerId);

    {
        std::lock_guard<std::mutex> lock(m_peerMutex);
        auto &state = m_peerStates[peerId];
        if (!state.versionSent) {
            SendVersion(peerId);
            state.versionSent = true;
        }
    }
}

void ScryptMsgHandler::HandleVerack(int peerId) {
    LogPrint(BCLog::NET,
             "ScryptMsgHandler(%s): verack from peer %d\n",
             m_params.name, peerId);

    {
        std::lock_guard<std::mutex> lock(m_peerMutex);
        m_peerStates[peerId].verackReceived = true;
    }

    SendSendHeaders(peerId);
    RequestHeaders(peerId);
}

void ScryptMsgHandler::HandleHeaders(int peerId,
                                     const std::vector<uint8_t> &payload) {
    if (payload.empty()) {
        return;
    }

    try {
        CDataStream ss(payload, SER_NETWORK, m_params.protocolVersion);

        uint64_t nHeaders;
        ss >> COMPACTSIZE(nHeaders);

        if (nHeaders > MAX_HEADERS_PER_MSG) {
            LogPrintf("ScryptMsgHandler(%s): peer %d sent %llu headers "
                      "(max %d), disconnecting\n",
                      m_params.name, peerId, nHeaders, MAX_HEADERS_PER_MSG);
            return;
        }

        std::vector<CParentBlockHeader> headers;
        headers.reserve(nHeaders);

        for (uint64_t i = 0; i < nHeaders; ++i) {
            CParentBlockHeader hdr;
            ss >> hdr;

            // Read and discard the tx count (always 0 in headers message)
            uint64_t nTx;
            ss >> COMPACTSIZE(nTx);

            headers.push_back(hdr);
        }

        if (headers.empty()) {
            return;
        }

        int heightBefore = m_headerChain.GetHeight();

        std::string error;
        if (!m_headerChain.AcceptHeaders(headers, error)) {
            LogPrintf("ScryptMsgHandler(%s): reject headers from peer %d: "
                      "%s\n",
                      m_params.name, peerId, error);
            return;
        }

        int heightAfter = m_headerChain.GetHeight();

        if (heightAfter > heightBefore) {
            LogPrint(BCLog::NET,
                     "ScryptMsgHandler(%s): headers %d->%d from peer %d\n",
                     m_params.name, heightBefore, heightAfter, peerId);
        }

        // If we received a full batch, ask for more
        if (nHeaders == MAX_HEADERS_PER_MSG) {
            m_syncing = true;
            RequestHeaders(peerId);
        } else {
            if (m_syncing) {
                m_syncing = false;
                LogPrintf("ScryptMsgHandler(%s): header sync complete at "
                          "height %d\n",
                          m_params.name, heightAfter);
            }
            if (m_newTipCb) {
                m_newTipCb();
            }
        }

    } catch (const std::exception &e) {
        LogPrintf("ScryptMsgHandler(%s): error parsing headers from peer %d: "
                  "%s\n",
                  m_params.name, peerId, e.what());
    }
}

void ScryptMsgHandler::HandlePing(int peerId,
                                  const std::vector<uint8_t> &payload) {
    m_netMgr.SendMessage(peerId, "pong", payload);
}

void ScryptMsgHandler::HandlePong(int /*peerId*/,
                                  const std::vector<uint8_t> & /*payload*/) {
    // Just update activity (handled by connection layer)
}

void ScryptMsgHandler::HandleInv(int peerId,
                                 const std::vector<uint8_t> &payload) {
    if (payload.empty()) {
        return;
    }

    try {
        CDataStream ss(payload, SER_NETWORK, m_params.protocolVersion);

        uint64_t nInv;
        ss >> COMPACTSIZE(nInv);

        if (nInv > 50000) {
            return;
        }

        std::vector<std::pair<uint32_t, uint256>> blockInvs;
        std::vector<uint256> txInvs;

        for (uint64_t i = 0; i < nInv; ++i) {
            uint32_t invType;
            uint256 hash;
            ss >> invType >> hash;

            if (invType == MSG_BLOCK || invType == MSG_WITNESS_BLOCK) {
                // We only care about block announcements for header download
                blockInvs.emplace_back(invType, hash);
            } else if (m_txRelayEnabled &&
                       (invType == MSG_TX || invType == MSG_WITNESS_TX)) {
                txInvs.push_back(hash);
            }
        }

        // For block inv's, request headers instead
        if (!blockInvs.empty()) {
            RequestHeaders(peerId);
        }

        // For tx inv's, relay to mempool via callback
        if (!txInvs.empty() && m_txRelayCb) {
            // Request the full tx
            CDataStream getdata(SER_NETWORK, m_params.protocolVersion);
            uint64_t count = txInvs.size();
            getdata << COMPACTSIZE(count);
            for (const auto &txid : txInvs) {
                uint32_t type = m_params.hasSegWit ? MSG_WITNESS_TX : MSG_TX;
                getdata << type << txid;
            }
            m_netMgr.SendMessage(peerId, "getdata",
                                 {getdata.begin(), getdata.end()});

            // Track for mempool peer-confirmation
            if (m_txRelayCb) {
                for (const auto &txid : txInvs) {
                    m_txRelayCb(peerId, txid, {});
                }
            }
        }

    } catch (const std::exception &e) {
        LogPrintf("ScryptMsgHandler(%s): error parsing inv from peer %d: %s\n",
                  m_params.name, peerId, e.what());
    }
}

void ScryptMsgHandler::HandleTx(int peerId,
                                const std::vector<uint8_t> &payload) {
    if (!m_txRelayEnabled || !m_txRelayCb || payload.empty()) {
        return;
    }

    // Compute txid (double-SHA256 of payload)
    uint256 txid = Hash(payload);

    m_txRelayCb(peerId, txid, payload);
}

void ScryptMsgHandler::HandleSendHeaders(int peerId) {
    std::lock_guard<std::mutex> lock(m_peerMutex);
    m_peerStates[peerId].sendHeaders = true;
}

void ScryptMsgHandler::SendVersion(int peerId) {
    CDataStream ss(SER_NETWORK, m_params.protocolVersion);

    int32_t version = static_cast<int32_t>(m_params.protocolVersion);
    uint64_t services = 0;
    int64_t timestamp = GetTime();
    // addr_recv (26 bytes: 8 services + 16 IP + 2 port)
    uint64_t recvServices = 0;
    uint8_t recvAddr[16]{};
    uint16_t recvPort = 0;
    // addr_from (26 bytes)
    uint64_t fromServices = 0;
    uint8_t fromAddr[16]{};
    uint16_t fromPort = 0;

    std::random_device rd;
    std::mt19937_64 rng(rd());
    uint64_t nonce = rng();

    std::string userAgent = m_params.userAgent;
    int32_t startHeight = m_headerChain.GetHeight();
    uint8_t relay = 0; // don't relay tx initially

    ss << version;
    ss << services;
    ss << timestamp;
    ss << recvServices;
    ss.write(reinterpret_cast<const char *>(recvAddr), 16);
    ss << recvPort;
    ss << fromServices;
    ss.write(reinterpret_cast<const char *>(fromAddr), 16);
    ss << fromPort;
    ss << nonce;
    ss << COMPACTSIZE(static_cast<uint64_t>(userAgent.size()));
    ss.write(userAgent.data(), userAgent.size());
    ss << startHeight;
    ss << relay;

    m_netMgr.SendMessage(peerId, "version", {ss.begin(), ss.end()});

    std::lock_guard<std::mutex> lock(m_peerMutex);
    m_peerStates[peerId].versionSent = true;
}

void ScryptMsgHandler::SendVerack(int peerId) {
    m_netMgr.SendMessage(peerId, "verack", {});
}

void ScryptMsgHandler::SendGetHeaders(int peerId,
                                      const std::vector<BlockHash> &locator,
                                      const BlockHash &stopHash) {
    CDataStream ss(SER_NETWORK, m_params.protocolVersion);

    uint32_t version = m_params.protocolVersion;
    ss << version;

    uint64_t locatorCount = locator.size();
    ss << COMPACTSIZE(locatorCount);
    for (const auto &hash : locator) {
        ss << hash;
    }
    ss << stopHash;

    m_netMgr.SendMessage(peerId, "getheaders", {ss.begin(), ss.end()});
}

void ScryptMsgHandler::SendSendHeaders(int peerId) {
    m_netMgr.SendMessage(peerId, "sendheaders", {});
}

void ScryptMsgHandler::RequestHeaders(int peerId) {
    auto locator = m_headerChain.GetBlockLocator();
    BlockHash stopHash;
    SendGetHeaders(peerId, locator, stopHash);
}

} // namespace scryptchain
