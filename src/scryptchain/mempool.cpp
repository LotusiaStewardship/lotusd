// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <scryptchain/mempool.h>

#include <logging.h>
#include <serialize.h>
#include <util/time.h>
#include <version.h>

#include <algorithm>

namespace scryptchain {

ScryptMemPool::ScryptMemPool(int minPeerConfirmations, int maxStageSec,
                             size_t maxMemoryBytes)
    : m_minPeerConfirmations(minPeerConfirmations),
      m_maxStageSec(maxStageSec), m_maxMemoryBytes(maxMemoryBytes) {}

ScryptMemPool::~ScryptMemPool() = default;

void ScryptMemPool::OnTxInv(const uint256 &txid, int peerId) {
    std::lock_guard<std::mutex> lock(m_cs);

    if (m_blacklist.count(txid)) {
        return;
    }

    auto it = m_pool.find(txid);
    if (it != m_pool.end()) {
        it->second.peersSeen.insert(peerId);
        CheckConfirmation(it->second);
        return;
    }

    // Unknown tx: record the peer, but don't create an entry yet.
    // The entry is created when AcceptTransaction arrives.
    // We still need to track that this peer has seen it, so create a
    // lightweight entry.
    MempoolEntry entry;
    entry.firstSeen = GetTime();
    entry.peersSeen.insert(peerId);
    m_pool.emplace(txid, std::move(entry));
}

bool ScryptMemPool::AcceptTransaction(const CTransaction &tx, int peerId,
                                      int maxBlockWeight,
                                      std::string &error) {
    uint256 txid = tx.GetId();

    std::lock_guard<std::mutex> lock(m_cs);

    if (m_blacklist.count(txid)) {
        error = "blacklisted";
        return false;
    }

    auto it = m_pool.find(txid);
    if (it != m_pool.end() && it->second.txSize > 0) {
        // Already have the full tx, just add this peer
        it->second.peersSeen.insert(peerId);
        CheckConfirmation(it->second);
        return true;
    }

    if (!StructuralCheck(tx, maxBlockWeight, error)) {
        return false;
    }

    size_t txBytes = GetSerializeSize(tx, PROTOCOL_VERSION);

    if (it != m_pool.end()) {
        it->second.tx = MakeTransactionRef(tx);
        it->second.txSize = txBytes;
        it->second.peersSeen.insert(peerId);
        it->second.feeRate = 1;
        m_currentMemory += txBytes;
        CheckConfirmation(it->second);
    } else {
        MempoolEntry entry;
        entry.tx = MakeTransactionRef(tx);
        entry.firstSeen = GetTime();
        entry.peersSeen.insert(peerId);
        entry.feeRate = 1;
        entry.txSize = txBytes;
        m_pool.emplace(txid, std::move(entry));
        m_currentMemory += txBytes;
    }

    EnforceMemoryLimit();
    return true;
}

bool ScryptMemPool::StructuralCheck(const CTransaction &tx, int maxBlockWeight,
                                    std::string &error) const {
    if (tx.vin.empty()) {
        error = "no inputs";
        return false;
    }
    if (tx.vout.empty()) {
        error = "no outputs";
        return false;
    }

    size_t txSize = GetSerializeSize(tx, PROTOCOL_VERSION);
    if (static_cast<int>(txSize) > maxBlockWeight) {
        error = "tx too large";
        return false;
    }

    for (const auto &out : tx.vout) {
        if (out.nValue < 0 * SATOSHI) {
            error = "negative output value";
            return false;
        }
    }

    return true;
}

void ScryptMemPool::CheckConfirmation(MempoolEntry &entry) {
    if (!entry.confirmed &&
        static_cast<int>(entry.peersSeen.size()) >= m_minPeerConfirmations) {
        entry.confirmed = true;
    }
}

std::vector<CTransaction>
ScryptMemPool::GetForTemplate(int maxWeight) const {
    std::lock_guard<std::mutex> lock(m_cs);

    // Collect confirmed-only entries
    struct SortEntry {
        const CTransaction *tx;
        int64_t feeRate;
        size_t txSize;
    };
    std::vector<SortEntry> candidates;

    for (const auto &[_, entry] : m_pool) {
        if (entry.confirmed && entry.txSize > 0 && entry.tx) {
            candidates.push_back({entry.tx.get(), entry.feeRate, entry.txSize});
        }
    }

    // Sort by fee rate descending
    std::sort(candidates.begin(), candidates.end(),
              [](const SortEntry &a, const SortEntry &b) {
                  return a.feeRate > b.feeRate;
              });

    std::vector<CTransaction> result;
    int totalWeight = 0;
    for (const auto &c : candidates) {
        int weight = static_cast<int>(c.txSize);
        if (totalWeight + weight > maxWeight) {
            continue;
        }
        result.push_back(*c.tx);
        totalWeight += weight;
    }

    return result;
}

void ScryptMemPool::PruneConfirmed(const std::set<uint256> &blockTxIds) {
    std::lock_guard<std::mutex> lock(m_cs);

    for (const auto &txid : blockTxIds) {
        auto it = m_pool.find(txid);
        if (it != m_pool.end()) {
            m_currentMemory -=
                (it->second.txSize > 0) ? it->second.txSize : 0;
            m_pool.erase(it);
        }
    }
}

void ScryptMemPool::PruneStale() {
    std::lock_guard<std::mutex> lock(m_cs);

    int64_t now = GetTime();
    std::vector<uint256> toRemove;

    for (auto &[txid, entry] : m_pool) {
        if (!entry.confirmed &&
            (now - entry.firstSeen) > m_maxStageSec) {
            toRemove.push_back(txid);
        }
    }

    for (const auto &txid : toRemove) {
        auto it = m_pool.find(txid);
        if (it != m_pool.end()) {
            m_currentMemory -=
                (it->second.txSize > 0) ? it->second.txSize : 0;
            m_pool.erase(it);
        }
    }

    if (!toRemove.empty()) {
        LogPrint(BCLog::MEMPOOL, "ScryptMemPool: pruned %d stale staging txs\n",
                 toRemove.size());
    }
}

void ScryptMemPool::Blacklist(const std::set<uint256> &txids) {
    std::lock_guard<std::mutex> lock(m_cs);

    for (const auto &txid : txids) {
        m_blacklist.insert(txid);
        auto it = m_pool.find(txid);
        if (it != m_pool.end()) {
            m_currentMemory -=
                (it->second.txSize > 0) ? it->second.txSize : 0;
            m_pool.erase(it);
        }
    }
}

void ScryptMemPool::EnforceMemoryLimit() {
    if (m_currentMemory <= m_maxMemoryBytes) {
        return;
    }

    // Evict lowest-fee-rate confirmed txs first, then staging
    // Build a sorted list of all entries by fee rate (ascending)
    struct EvictEntry {
        uint256 txid;
        int64_t feeRate;
        bool confirmed;
        size_t txSize;
    };
    std::vector<EvictEntry> evictable;

    for (const auto &[txid, entry] : m_pool) {
        if (entry.txSize > 0) {
            evictable.push_back(
                {txid, entry.feeRate, entry.confirmed, entry.txSize});
        }
    }

    // Sort: unconfirmed first, then by fee rate ascending
    std::sort(evictable.begin(), evictable.end(),
              [](const EvictEntry &a, const EvictEntry &b) {
                  if (a.confirmed != b.confirmed) {
                      return !a.confirmed;
                  }
                  return a.feeRate < b.feeRate;
              });

    for (const auto &e : evictable) {
        if (m_currentMemory <= m_maxMemoryBytes) {
            break;
        }
        auto it = m_pool.find(e.txid);
        if (it != m_pool.end()) {
            m_currentMemory -= e.txSize;
            m_pool.erase(it);
        }
    }
}

size_t ScryptMemPool::GetSize() const {
    std::lock_guard<std::mutex> lock(m_cs);
    return m_pool.size();
}

size_t ScryptMemPool::GetStagingSize() const {
    std::lock_guard<std::mutex> lock(m_cs);
    size_t count = 0;
    for (const auto &[_, entry] : m_pool) {
        if (!entry.confirmed) {
            ++count;
        }
    }
    return count;
}

size_t ScryptMemPool::GetConfirmedSize() const {
    std::lock_guard<std::mutex> lock(m_cs);
    size_t count = 0;
    for (const auto &[_, entry] : m_pool) {
        if (entry.confirmed) {
            ++count;
        }
    }
    return count;
}

size_t ScryptMemPool::GetMemoryUsage() const {
    std::lock_guard<std::mutex> lock(m_cs);
    return m_currentMemory;
}

} // namespace scryptchain
