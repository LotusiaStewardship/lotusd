// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_STRATUM_STRATUMWORKER_H
#define BITCOIN_STRATUM_STRATUMWORKER_H

#include <script/script.h>
#include <stratum/stratumconfig.h>
#include <univalue.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

class CChainParams;

namespace stratum {

enum class MiningAlgorithm { NATIVE, AUXPOW };

class StratumWorker {
public:
    enum class State { CONNECTED, SUBSCRIBED, AUTHORIZED, MINING };

    struct Stats {
        uint64_t sharesAccepted = 0;
        uint64_t sharesRejected = 0;
        uint64_t sharesStale = 0;
        double currentDifficulty = 0;
        int64_t lastShareTime = 0;
        double estimatedHashrate = 0;
        std::string workerName;
        std::string payoutAddress;
        MiningAlgorithm algorithm = MiningAlgorithm::NATIVE;
        State state = State::CONNECTED;
    };

    StratumWorker(uint32_t sessionId, const std::string &extranonce1,
                  double initialDifficulty);

    State GetState() const { return m_state; }
    uint32_t GetSessionId() const { return m_sessionId; }
    const std::string &GetExtranonce1() const { return m_extranonce1; }
    const std::string &GetWorkerName() const { return m_workerName; }
    const std::string &GetPayoutAddress() const { return m_payoutAddress; }
    const CScript &GetPayoutScript() const { return m_payoutScript; }
    MiningAlgorithm GetAlgorithm() const { return m_algorithm; }

    /**
     * Handle mining.subscribe -- transitions CONNECTED -> SUBSCRIBED.
     * Returns true on success and fills resultOut.
     * The userAgent hint may contain "lotus-auxpow" to select AuxPoW mode.
     */
    bool HandleSubscribe(const UniValue &params, UniValue &resultOut,
                         std::string &error);

    /**
     * Handle mining.authorize -- transitions SUBSCRIBED -> AUTHORIZED.
     * Extracts payout address from the worker name (before the dot).
     * Validates the address against chainParams. Returns true on success.
     */
    bool HandleAuthorize(const UniValue &params,
                         const CChainParams &chainParams,
                         std::string &error);

    void RecordShareAccepted(double difficulty);
    void RecordShareRejected();
    void RecordShareStale();

    double GetCurrentDifficulty() const { return m_difficulty; }
    void SetDifficulty(double diff);

    bool ShouldRetargetDifficulty(const StratumConfig &config,
                                  int64_t now) const;
    double CalcNewDifficulty(const StratumConfig &config, int64_t now) const;

    Stats GetStats() const;
    bool IsTimedOut(int timeoutSec, int64_t now) const;
    void Touch(int64_t now);

    static constexpr size_t EXTRANONCE2_SIZE = 8;

private:
    State m_state;
    uint32_t m_sessionId;
    std::string m_extranonce1;
    std::string m_workerName;
    std::string m_payoutAddress;
    CScript m_payoutScript;
    MiningAlgorithm m_algorithm = MiningAlgorithm::NATIVE;
    double m_difficulty;
    uint64_t m_sharesAccepted = 0;
    uint64_t m_sharesRejected = 0;
    uint64_t m_sharesStale = 0;
    int64_t m_connectTime;
    int64_t m_lastActivityTime;
    int64_t m_lastRetargetTime;
    std::vector<int64_t> m_shareTimestamps;
};

class ExtranonceMgr {
public:
    std::string Next();
    void Reset() { m_counter.store(0); }

private:
    std::atomic<uint32_t> m_counter{0};
};

} // namespace stratum

#endif // BITCOIN_STRATUM_STRATUMWORKER_H
