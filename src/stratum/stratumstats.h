// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_STRATUM_STRATUMSTATS_H
#define BITCOIN_STRATUM_STRATUMSTATS_H

#include <httpserver.h>
#include <stratum/stratumworker.h>
#include <univalue.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Config;

namespace stratum {

struct UpstreamPoolStats {
    std::string label;
    bool connected = false;
    bool healthy = false;
    int priority = 0;
    int consecutiveErrors = 0;
    std::string lastError;
};

struct StratumServerStats {
    int64_t startTime = 0;
    uint64_t totalConnections = 0;
    uint32_t activeWorkers = 0;
    uint64_t totalSharesAccepted = 0;
    uint64_t totalSharesRejected = 0;
    uint64_t totalSharesStale = 0;
    uint64_t blocksFound = 0;
    double networkDifficulty = 0;
    int chainHeight = 0;
    std::vector<StratumWorker::Stats> workers;

    std::string activeTier;
    int activeProxyIndex = -1;
    std::vector<UpstreamPoolStats> upstreamPools;
};

using StatsProvider = std::function<StratumServerStats()>;

UniValue FormatStatsJson(const StratumServerStats &stats);

void RegisterStratumHTTPHandlers(StatsProvider provider);
void UnregisterStratumHTTPHandlers();

} // namespace stratum

#endif // BITCOIN_STRATUM_STRATUMSTATS_H
