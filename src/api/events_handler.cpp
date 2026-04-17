// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/events_handler.h>

#include <chain.h>
#include <logging.h>
#include <primitives/block.h>
#include <rpc/protocol.h>
#include <sync.h>
#include <validation.h>

#include <condition_variable>
#include <deque>
#include <thread>

namespace api {

static constexpr size_t MAX_EVENT_BUFFER = 200;

struct SSEEvent {
    std::string type;
    std::string data;
};

static std::mutex g_events_mutex;
static std::deque<SSEEvent> g_event_buffer GUARDED_BY(g_events_mutex);
static std::condition_variable g_events_cv;
static uint64_t g_event_seq GUARDED_BY(g_events_mutex) = 0;

class EventsSubscriber final : public CValidationInterface {
protected:
    void UpdatedBlockTip(const CBlockIndex *pindexNew,
                         const CBlockIndex *pindexFork,
                         bool fInitialDownload) override {
        if (fInitialDownload) {
            return;
        }
        UniValue data(UniValue::VOBJ);
        data.pushKV("hash", pindexNew->GetBlockHash().GetHex());
        data.pushKV("height", pindexNew->nHeight);
        data.pushKV("time", int64_t(pindexNew->GetBlockTime()));
        data.pushKV("n_tx", int64_t(pindexNew->nTx));

        PushEvent("block", data.write());
    }

    void TransactionAddedToMempool(const CTransactionRef &tx,
                                   const std::vector<Coin> &,
                                   uint64_t) override {
        UniValue data(UniValue::VOBJ);
        data.pushKV("txid", tx->GetId().GetHex());
        data.pushKV("size", int64_t(tx->GetTotalSize()));

        PushEvent("mempool_tx", data.write());
    }

    void BlockConnected(const std::shared_ptr<const CBlock> &block,
                        const CBlockIndex *pindex) override {
        UniValue data(UniValue::VOBJ);
        data.pushKV("hash", pindex->GetBlockHash().GetHex());
        data.pushKV("height", pindex->nHeight);
        data.pushKV("n_tx", int64_t(block->vtx.size()));

        PushEvent("block_connected", data.write());
    }

    void BlockDisconnected(const std::shared_ptr<const CBlock> &block,
                           const CBlockIndex *pindex) override {
        UniValue data(UniValue::VOBJ);
        data.pushKV("hash", pindex->GetBlockHash().GetHex());
        data.pushKV("height", pindex->nHeight);

        PushEvent("block_disconnected", data.write());
    }

private:
    static void PushEvent(const std::string &type, const std::string &data) {
        std::lock_guard<std::mutex> lock(g_events_mutex);
        g_event_seq++;
        g_event_buffer.push_back({type, data});
        while (g_event_buffer.size() > MAX_EVENT_BUFFER) {
            g_event_buffer.pop_front();
        }
        g_events_cv.notify_all();
    }
};

static std::shared_ptr<EventsSubscriber> g_subscriber;

void StartEvents() {
    g_subscriber = std::make_shared<EventsSubscriber>();
    RegisterSharedValidationInterface(g_subscriber);
    LogPrintf("SSE events subscriber registered\n");
}

void StopEvents() {
    if (g_subscriber) {
        UnregisterSharedValidationInterface(g_subscriber);
        g_subscriber.reset();
    }
    g_events_cv.notify_all();
}

bool HandleGetEvents(const util::Ref &, HTTPRequest *req,
                     const std::vector<std::string> &,
                     const QueryParams &qp) {
    // SSE can't be done properly with libevent's single WriteReply model.
    // Instead, return the buffered events as a JSON array (long-poll style).
    // Clients can poll this endpoint periodically.

    int64_t since = qp.GetInt64("since_seq", 0);
    int limit = qp.GetInt("limit", 50);
    limit = std::max(1, std::min(limit, 200));

    UniValue events(UniValue::VARR);
    uint64_t latestSeq = 0;
    {
        std::lock_guard<std::mutex> lock(g_events_mutex);
        latestSeq = g_event_seq;

        uint64_t bufStartSeq =
            g_event_seq >= g_event_buffer.size()
                ? g_event_seq - g_event_buffer.size() + 1
                : 1;

        int count = 0;
        for (size_t i = 0; i < g_event_buffer.size() && count < limit; i++) {
            uint64_t seq = bufStartSeq + i;
            if (int64_t(seq) <= since) {
                continue;
            }
            UniValue ev(UniValue::VOBJ);
            ev.pushKV("seq", int64_t(seq));
            ev.pushKV("type", g_event_buffer[i].type);

            UniValue parsed;
            if (parsed.read(g_event_buffer[i].data)) {
                ev.pushKV("data", parsed);
            } else {
                ev.pushKV("data", g_event_buffer[i].data);
            }
            events.push_back(ev);
            count++;
        }
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("events", events);
    result.pushKV("latest_seq", int64_t(latestSeq));
    WriteSuccess(req, result);
    return true;
}

} // namespace api
