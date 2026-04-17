// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stratum/stratumworker.h>

#include <chainparams.h>
#include <key_io.h>
#include <script/standard.h>
#include <util/strencodings.h>
#include <util/time.h>

#include <algorithm>
#include <cmath>

namespace stratum {

StratumWorker::StratumWorker(uint32_t sessionId,
                             const std::string &extranonce1,
                             double initialDifficulty)
    : m_state(State::CONNECTED), m_sessionId(sessionId),
      m_extranonce1(extranonce1), m_difficulty(initialDifficulty) {
    m_connectTime = GetTime();
    m_lastActivityTime = m_connectTime;
    m_lastRetargetTime = m_connectTime;
}

bool StratumWorker::HandleSubscribe(const UniValue &params,
                                    UniValue &resultOut,
                                    std::string &error) {
    if (m_state != State::CONNECTED) {
        error = "Already subscribed";
        return false;
    }

    // Check for algorithm hint in user agent
    if (params.isArray() && params.size() > 0) {
        std::string userAgent = params[0].get_str();
        if (userAgent.find("lotus-auxpow") != std::string::npos ||
            userAgent.find("auxpow") != std::string::npos) {
            m_algorithm = MiningAlgorithm::AUXPOW;
        }
    }

    m_state = State::SUBSCRIBED;

    UniValue subscriptions(UniValue::VARR);

    UniValue diffSub(UniValue::VARR);
    diffSub.push_back("mining.set_difficulty");
    diffSub.push_back(m_extranonce1.substr(0, 8));
    subscriptions.push_back(diffSub);

    UniValue notifySub(UniValue::VARR);
    notifySub.push_back("mining.notify");
    notifySub.push_back(m_extranonce1.substr(0, 8));
    subscriptions.push_back(notifySub);

    UniValue result(UniValue::VARR);
    result.push_back(subscriptions);
    result.push_back(m_extranonce1);
    result.push_back(static_cast<int>(EXTRANONCE2_SIZE));

    resultOut = result;
    return true;
}

bool StratumWorker::HandleAuthorize(const UniValue &params,
                                    const CChainParams &chainParams,
                                    std::string &error) {
    if (m_state == State::CONNECTED) {
        error = "Must subscribe before authorizing";
        return false;
    }
    if (m_state == State::AUTHORIZED || m_state == State::MINING) {
        error = "Already authorized";
        return false;
    }

    if (!params.isArray() || params.size() < 1) {
        error = "mining.authorize requires at least a username";
        return false;
    }

    m_workerName = params[0].get_str();

    // Extract payout address: everything before the first dot
    std::string addressStr = m_workerName;
    auto dotPos = addressStr.find('.');
    if (dotPos != std::string::npos) {
        addressStr = addressStr.substr(0, dotPos);
    }

    CTxDestination dest = DecodeDestination(addressStr, chainParams);
    if (!IsValidDestination(dest)) {
        error = strprintf("Invalid payout address in worker name: %s",
                          addressStr);
        return false;
    }

    m_payoutAddress = addressStr;
    m_payoutScript = GetScriptForDestination(dest);
    m_state = State::AUTHORIZED;

    return true;
}

void StratumWorker::RecordShareAccepted(double difficulty) {
    m_sharesAccepted++;
    int64_t now = GetTime();
    m_lastActivityTime = now;
    m_shareTimestamps.push_back(now);

    if (m_shareTimestamps.size() > 100) {
        m_shareTimestamps.erase(m_shareTimestamps.begin());
    }
}

void StratumWorker::RecordShareRejected() {
    m_sharesRejected++;
    m_lastActivityTime = GetTime();
}

void StratumWorker::RecordShareStale() {
    m_sharesStale++;
    m_lastActivityTime = GetTime();
}

void StratumWorker::SetDifficulty(double diff) {
    if (diff < MIN_DIFFICULTY) {
        diff = MIN_DIFFICULTY;
    }
    if (diff > MAX_DIFFICULTY) {
        diff = MAX_DIFFICULTY;
    }
    m_difficulty = diff;
}

bool StratumWorker::ShouldRetargetDifficulty(const StratumConfig &config,
                                              int64_t now) const {
    if (!config.useVarDiff) {
        return false;
    }
    if (m_shareTimestamps.size() < 3) {
        return false;
    }
    return (now - m_lastRetargetTime) >= (int64_t)config.varDiffRetargetTime;
}

double StratumWorker::CalcNewDifficulty(const StratumConfig &config,
                                        int64_t now) const {
    if (m_shareTimestamps.size() < 2) {
        return m_difficulty;
    }

    int64_t windowStart = m_shareTimestamps.front();
    int64_t windowEnd = m_shareTimestamps.back();
    double elapsed = static_cast<double>(windowEnd - windowStart);
    if (elapsed <= 0) {
        return m_difficulty;
    }

    double avgShareTime = elapsed / (m_shareTimestamps.size() - 1);
    double ratio = config.varDiffTargetTime / avgShareTime;
    double newDiff = m_difficulty * ratio;

    if (newDiff < MIN_DIFFICULTY) {
        newDiff = MIN_DIFFICULTY;
    }
    if (newDiff > MAX_DIFFICULTY) {
        newDiff = MAX_DIFFICULTY;
    }

    return newDiff;
}

StratumWorker::Stats StratumWorker::GetStats() const {
    Stats s;
    s.sharesAccepted = m_sharesAccepted;
    s.sharesRejected = m_sharesRejected;
    s.sharesStale = m_sharesStale;
    s.currentDifficulty = m_difficulty;
    s.lastShareTime = m_shareTimestamps.empty() ? 0 : m_shareTimestamps.back();
    s.workerName = m_workerName;
    s.payoutAddress = m_payoutAddress;
    s.algorithm = m_algorithm;
    s.state = m_state;

    if (m_sharesAccepted > 0 && m_shareTimestamps.size() >= 2) {
        int64_t span = m_shareTimestamps.back() - m_shareTimestamps.front();
        if (span > 0) {
            double sharesPerSec =
                static_cast<double>(m_shareTimestamps.size() - 1) / span;
            s.estimatedHashrate = sharesPerSec * m_difficulty * 4294967296.0;
        }
    }

    return s;
}

bool StratumWorker::IsTimedOut(int timeoutSec, int64_t now) const {
    return (now - m_lastActivityTime) > timeoutSec;
}

void StratumWorker::Touch(int64_t now) {
    m_lastActivityTime = now;
}

// --- ExtranonceMgr ---

std::string ExtranonceMgr::Next() {
    uint32_t val = m_counter.fetch_add(1);
    uint8_t bytes[4];
    bytes[0] = (val >> 24) & 0xff;
    bytes[1] = (val >> 16) & 0xff;
    bytes[2] = (val >> 8) & 0xff;
    bytes[3] = val & 0xff;
    return HexStr(Span<const uint8_t>(bytes, 4));
}

} // namespace stratum
