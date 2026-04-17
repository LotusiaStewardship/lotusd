// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRYPTCHAIN_MEMPOOL_H
#define BITCOIN_SCRYPTCHAIN_MEMPOOL_H

#include <primitives/transaction.h>
#include <uint256.h>

#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace scryptchain {

/**
 * Lightweight mempool for Scrypt chains (LTC/DOGE) with peer-confirmation
 * gating. A transaction is only eligible for block templates after at least
 * N distinct peers have independently relayed it.
 */
class ScryptMemPool {
public:
    /**
     * @param minPeerConfirmations Minimum distinct peers that must relay a tx
     *        before it becomes eligible for templates (default 5).
     * @param maxStageSec Maximum seconds an unconfirmed tx stays in staging
     *        before eviction (default 600).
     * @param maxMemoryBytes Maximum memory for this mempool (default 50MB).
     */
    ScryptMemPool(int minPeerConfirmations = 5, int maxStageSec = 600,
                  size_t maxMemoryBytes = 50 * 1024 * 1024);
    ~ScryptMemPool();

    /**
     * Called when a peer sends inv(TX, txid). Tracks which peers have
     * independently seen this transaction.
     */
    void OnTxInv(const uint256 &txid, int peerId);

    /**
     * Called when the full transaction data arrives from a peer.
     * Performs structural validation and adds to staging area.
     * @return true if accepted (may be already known)
     */
    bool AcceptTransaction(const CTransaction &tx, int peerId,
                           int maxBlockWeight, std::string &error);

    /**
     * Return confirmed-only transactions sorted by fee rate,
     * up to maxWeight total.
     */
    std::vector<CTransaction> GetForTemplate(int maxWeight) const;

    /**
     * Remove transactions that were confirmed in a block.
     */
    void PruneConfirmed(const std::set<uint256> &blockTxIds);

    /**
     * Remove stale staging entries and enforce memory limits.
     */
    void PruneStale();

    /**
     * Blacklist txids (e.g., from a rejected block) and remove them.
     */
    void Blacklist(const std::set<uint256> &txids);

    size_t GetSize() const;
    size_t GetStagingSize() const;
    size_t GetConfirmedSize() const;
    size_t GetMemoryUsage() const;

private:
    struct MempoolEntry {
        CTransactionRef tx;
        int64_t firstSeen{0};
        std::set<int> peersSeen;
        bool confirmed{false};
        int64_t feeRate{0};
        size_t txSize{0};
    };

    int m_minPeerConfirmations;
    int m_maxStageSec;
    size_t m_maxMemoryBytes;

    mutable std::mutex m_cs;
    std::map<uint256, MempoolEntry> m_pool;
    std::set<uint256> m_blacklist;

    size_t m_currentMemory{0};

    bool StructuralCheck(const CTransaction &tx, int maxBlockWeight,
                         std::string &error) const;
    void CheckConfirmation(MempoolEntry &entry);
    void EnforceMemoryLimit();
};

} // namespace scryptchain

#endif // BITCOIN_SCRYPTCHAIN_MEMPOOL_H
