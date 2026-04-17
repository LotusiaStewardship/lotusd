// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRYPTCHAIN_MSG_HANDLER_H
#define BITCOIN_SCRYPTCHAIN_MSG_HANDLER_H

#include <primitives/blockhash.h>
#include <scryptchain/chain_params.h>
#include <scryptchain/peer_connection.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace scryptchain {

class ScryptHeaderChain;
class ScryptNetworkManager;
class ScryptMemPool;

using NewTipCallback = std::function<void()>;
using TxRelayCallback =
    std::function<void(int peerId, const uint256 &txid,
                       const std::vector<uint8_t> &txData)>;

class ScryptMsgHandler {
public:
    ScryptMsgHandler(const ScryptChainParams &params,
                     ScryptHeaderChain &headerChain,
                     ScryptNetworkManager &netMgr);
    ~ScryptMsgHandler();

    void ProcessMessage(int peerId, const PeerMessage &msg);

    void SetNewTipCallback(NewTipCallback cb) { m_newTipCb = std::move(cb); }
    void SetTxRelayCallback(TxRelayCallback cb) { m_txRelayCb = std::move(cb); }

    void EnableTxRelay(bool enable) { m_txRelayEnabled = enable; }
    bool IsSyncing() const { return m_syncing; }

private:
    void HandleVersion(int peerId, const std::vector<uint8_t> &payload);
    void HandleVerack(int peerId);
    void HandleHeaders(int peerId, const std::vector<uint8_t> &payload);
    void HandlePing(int peerId, const std::vector<uint8_t> &payload);
    void HandlePong(int peerId, const std::vector<uint8_t> &payload);
    void HandleInv(int peerId, const std::vector<uint8_t> &payload);
    void HandleTx(int peerId, const std::vector<uint8_t> &payload);
    void HandleSendHeaders(int peerId);

    void SendVersion(int peerId);
    void SendVerack(int peerId);
    void SendGetHeaders(int peerId, const std::vector<BlockHash> &locator,
                        const BlockHash &stopHash);
    void SendSendHeaders(int peerId);

    void RequestHeaders(int peerId);

    const ScryptChainParams &m_params;
    ScryptHeaderChain &m_headerChain;
    ScryptNetworkManager &m_netMgr;

    NewTipCallback m_newTipCb;
    TxRelayCallback m_txRelayCb;

    std::atomic<bool> m_syncing{false};
    std::atomic<bool> m_txRelayEnabled{false};

    struct PeerSyncState {
        bool versionSent{false};
        bool verackReceived{false};
        bool sendHeaders{false};
        int startingHeight{0};
        int headersReceived{0};
    };

    std::mutex m_peerMutex;
    std::map<int, PeerSyncState> m_peerStates;

    std::mutex m_txRequestMutex;
    std::set<uint256> m_pendingTxRequests;
};

} // namespace scryptchain

#endif // BITCOIN_SCRYPTCHAIN_MSG_HANDLER_H
