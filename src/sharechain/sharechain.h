// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SHARECHAIN_SHARECHAIN_H
#define BITCOIN_SHARECHAIN_SHARECHAIN_H

#include <amount.h>
#include <script/script.h>
#include <sharechain/share.h>
#include <validationinterface.h>

#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <vector>

class CBlockIndex;

namespace sharechain {

struct PayoutEntry {
    CScript scriptPubKey;
    uint64_t shareCount = 0;
    double shareWork = 0;
    double percentage = 0;
    Amount amount = Amount::zero();
};

struct ShareChainStats {
    bool enabled = false;
    uint32_t height = 0;
    int windowSize = 0;
    uint32_t sharesInWindow = 0;
    double shareDifficulty = 0;
    double shareRate = 0;
    std::vector<PayoutEntry> payouts;
    std::vector<CShare> recentShares;
};

class ShareChain : public CValidationInterface {
public:
    ShareChain(int windowSize, double initialDifficulty);
    ~ShareChain();

    bool AddShare(const CShare &share, std::string &error);

    /**
     * Called by the local stratum server when a miner submits a valid share.
     * Creates a CShare and appends it to the chain.
     */
    void RecordLocalShare(const CScript &scriptPubKey, double difficulty);

    uint256 GetBestShareHash() const;
    uint32_t GetShareHeight() const;

    bool GetShare(const uint256 &hash, CShare &shareOut) const;
    bool HasShare(const uint256 &hash) const;

    /**
     * Compute the payout window: returns the proportional breakdown
     * of shares in the current window.
     */
    std::vector<PayoutEntry>
    GetPayoutWindow(Amount totalReward = Amount::zero()) const;

    /**
     * Build deterministic coinbase outputs from the share chain payout window.
     * Sub-dust amounts are redirected to the miner fund script.
     */
    std::vector<CTxOut> BuildPayoutOutputs(Amount totalReward,
                                           const CScript &minerFund) const;

    ShareChainStats GetStats() const;

    /**
     * Get shares for P2P sync: returns up to maxCount shares starting
     * from afterHash. If afterHash is null, starts from the beginning
     * of the window.
     */
    std::vector<CShare> GetSharesForSync(const uint256 &afterHash,
                                         size_t maxCount) const;

    int GetWindowSize() const { return m_windowSize; }

protected:
    void UpdatedBlockTip(const CBlockIndex *pindexNew,
                         const CBlockIndex *pindexFork,
                         bool fInitialDownload) override;

private:
    int m_windowSize;
    double m_shareDifficulty;

    mutable std::mutex m_mutex;

    std::deque<CShare> m_shares;
    std::map<uint256, size_t> m_shareIndex;
    uint256 m_bestShareHash;
    uint32_t m_shareHeight = 0;

    int64_t m_firstShareTime = 0;
    int64_t m_lastShareTime = 0;

    void PruneToWindow();
    void RebuildIndex();
    uint256 ComputePayoutRoot() const;
};

} // namespace sharechain

#endif // BITCOIN_SHARECHAIN_SHARECHAIN_H
