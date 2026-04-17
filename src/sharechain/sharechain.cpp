// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sharechain/sharechain.h>

#include <hash.h>
#include <logging.h>
#include <util/time.h>

namespace sharechain {

ShareChain::ShareChain(int windowSize, double initialDifficulty)
    : m_windowSize(windowSize), m_shareDifficulty(initialDifficulty) {
    if (m_shareDifficulty <= 0) {
        m_shareDifficulty = 1.0;
    }
}

ShareChain::~ShareChain() = default;

bool ShareChain::AddShare(const CShare &share, std::string &error) {
    std::lock_guard<std::mutex> lock(m_mutex);

    uint256 shareHash = share.GetHash();

    if (m_shareIndex.count(shareHash)) {
        error = "Duplicate share";
        return false;
    }

    if (!m_shares.empty()) {
        if (share.hashPrevShare != m_bestShareHash) {
            // For now, simple longest-chain: reject shares not building
            // on our tip. A real implementation would handle forks.
            error = "Share does not build on current tip";
            return false;
        }
    }

    m_shares.push_back(share);
    m_shareIndex[shareHash] = m_shares.size() - 1;
    m_bestShareHash = shareHash;
    m_shareHeight = share.nShareHeight;

    if (m_firstShareTime == 0) {
        m_firstShareTime = share.nTime;
    }
    m_lastShareTime = share.nTime;

    PruneToWindow();

    return true;
}

void ShareChain::RecordLocalShare(const CScript &scriptPubKey,
                                  double difficulty) {
    CShare share;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        share.hashPrevShare = m_bestShareHash;
        share.scriptPubKey = scriptPubKey;
        share.nShareBits = 0;
        share.nShareHeight = m_shareHeight + 1;
        share.nTime = GetTime();
        share.nDifficulty = difficulty;
    }

    share.hashPayoutRoot = ComputePayoutRoot();

    std::string error;
    if (!AddShare(share, error)) {
        LogPrint(BCLog::MINING,
                 "ShareChain: failed to record local share: %s\n", error);
    }
}

uint256 ShareChain::GetBestShareHash() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_bestShareHash;
}

uint32_t ShareChain::GetShareHeight() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_shareHeight;
}

bool ShareChain::GetShare(const uint256 &hash, CShare &shareOut) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_shareIndex.find(hash);
    if (it == m_shareIndex.end() || it->second >= m_shares.size()) {
        return false;
    }
    shareOut = m_shares[it->second];
    return true;
}

bool ShareChain::HasShare(const uint256 &hash) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_shareIndex.count(hash) > 0;
}

std::vector<PayoutEntry>
ShareChain::GetPayoutWindow(Amount totalReward) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::map<CScript, double> workByScript;
    double totalWork = 0;

    for (const auto &share : m_shares) {
        double work = share.nDifficulty > 0 ? share.nDifficulty : 1.0;
        workByScript[share.scriptPubKey] += work;
        totalWork += work;
    }

    std::vector<PayoutEntry> entries;
    if (totalWork <= 0) {
        return entries;
    }

    for (const auto &[script, work] : workByScript) {
        PayoutEntry entry;
        entry.scriptPubKey = script;
        entry.shareWork = work;
        entry.percentage = (work / totalWork) * 100.0;

        if (totalReward > Amount::zero()) {
            int64_t rewardSatoshis = totalReward / SATOSHI;
            entry.amount =
                static_cast<int64_t>((work / totalWork) *
                                     rewardSatoshis) * SATOSHI;
        }

        // Count shares for this script
        for (const auto &s : m_shares) {
            if (s.scriptPubKey == script) {
                entry.shareCount++;
            }
        }

        entries.push_back(entry);
    }

    // Sort by percentage descending
    std::sort(entries.begin(), entries.end(),
              [](const PayoutEntry &a, const PayoutEntry &b) {
                  return a.percentage > b.percentage;
              });

    return entries;
}

std::vector<CTxOut> ShareChain::BuildPayoutOutputs(
    Amount totalReward, const CScript &minerFund) const {

    auto payouts = GetPayoutWindow(totalReward);
    std::vector<CTxOut> outputs;

    // Dust threshold: outputs below this amount get redirected to miner fund
    constexpr Amount DUST_THRESHOLD = 546 * SATOSHI;

    Amount dustRedirect = Amount::zero();

    for (const auto &entry : payouts) {
        if (entry.amount < DUST_THRESHOLD) {
            dustRedirect += entry.amount;
        } else {
            outputs.emplace_back(entry.amount, entry.scriptPubKey);
        }
    }

    // Add dust redirect to miner fund if any
    if (dustRedirect > Amount::zero() && !minerFund.empty()) {
        bool found = false;
        for (auto &out : outputs) {
            if (out.scriptPubKey == minerFund) {
                out.nValue += dustRedirect;
                found = true;
                break;
            }
        }
        if (!found) {
            outputs.emplace_back(dustRedirect, minerFund);
        }
    }

    return outputs;
}

ShareChainStats ShareChain::GetStats() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    ShareChainStats stats;
    stats.enabled = true;
    stats.height = m_shareHeight;
    stats.windowSize = m_windowSize;
    stats.sharesInWindow = m_shares.size();
    stats.shareDifficulty = m_shareDifficulty;

    // Compute share rate (shares per minute)
    if (m_shares.size() >= 2 && m_lastShareTime > m_firstShareTime) {
        double elapsed =
            static_cast<double>(m_lastShareTime - m_firstShareTime);
        if (elapsed > 0) {
            stats.shareRate =
                (static_cast<double>(m_shares.size()) / elapsed) * 60.0;
        }
    }

    // Payouts (no reward amount for stats)
    stats.payouts = const_cast<ShareChain *>(this)->GetPayoutWindow();

    // Recent shares (last 10)
    size_t recentCount = std::min(m_shares.size(), (size_t)10);
    for (size_t i = m_shares.size() - recentCount; i < m_shares.size(); ++i) {
        stats.recentShares.push_back(m_shares[i]);
    }

    return stats;
}

std::vector<CShare>
ShareChain::GetSharesForSync(const uint256 &afterHash,
                              size_t maxCount) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<CShare> result;
    bool found = afterHash.IsNull();

    for (const auto &share : m_shares) {
        if (!found) {
            if (share.GetHash() == afterHash) {
                found = true;
            }
            continue;
        }
        result.push_back(share);
        if (result.size() >= maxCount) {
            break;
        }
    }

    return result;
}

void ShareChain::UpdatedBlockTip(const CBlockIndex *pindexNew,
                                 const CBlockIndex *, bool fInitialDownload) {
    if (fInitialDownload) {
        return;
    }
    // The share chain doesn't need to react to block tip changes directly.
    // The stratum server handles creating new jobs when the tip changes.
}

void ShareChain::PruneToWindow() {
    while ((int)m_shares.size() > m_windowSize) {
        uint256 removedHash = m_shares.front().GetHash();
        m_shareIndex.erase(removedHash);
        m_shares.pop_front();
    }
    if (!m_shares.empty()) {
        m_firstShareTime = m_shares.front().nTime;
    }
    RebuildIndex();
}

void ShareChain::RebuildIndex() {
    m_shareIndex.clear();
    for (size_t i = 0; i < m_shares.size(); ++i) {
        m_shareIndex[m_shares[i].GetHash()] = i;
    }
}

uint256 ShareChain::ComputePayoutRoot() const {
    // Merkle root of all scriptPubKeys in the current window
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_shares.empty()) {
        return uint256();
    }

    std::vector<uint256> leaves;
    for (const auto &share : m_shares) {
        leaves.push_back(Hash(share.scriptPubKey));
    }

    while (leaves.size() > 1) {
        std::vector<uint256> nextLevel;
        for (size_t i = 0; i < leaves.size(); i += 2) {
            uint256 left = leaves[i];
            uint256 right = (i + 1 < leaves.size()) ? leaves[i + 1] : left;
            nextLevel.push_back(Hash(left, right));
        }
        leaves = nextLevel;
    }

    return leaves[0];
}

} // namespace sharechain
