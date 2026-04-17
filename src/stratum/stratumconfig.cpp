// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stratum/stratumconfig.h>

#include <tinyformat.h>
#include <util/system.h>

#include <cstdlib>

namespace stratum {

void RegisterStratumArgs(ArgsManager &args) {
    args.AddArg("-stratum",
                "Enable the built-in Stratum mining server (default: 0)",
                ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    args.AddArg("-stratumbind=<addr>",
                "Bind Stratum server to this address (default: 0.0.0.0)",
                ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    args.AddArg(
        "-stratumport=<port>",
        strprintf("Listen for Stratum connections on <port> (default: %u)",
                  DEFAULT_STRATUM_PORT),
        ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    args.AddArg(
        "-stratumdifficulty=<n>",
        strprintf("Initial share difficulty for Stratum workers (default: %g)",
                  DEFAULT_STRATUM_DIFFICULTY),
        ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    args.AddArg("-stratumvardiff",
                "Enable variable difficulty for Stratum workers (default: 1)",
                ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    args.AddArg(
        "-stratumvardifftarget=<n>",
        strprintf("Target seconds between shares for vardiff (default: %g)",
                  DEFAULT_VARDIFF_TARGET_TIME),
        ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    args.AddArg(
        "-stratumvardiffretarget=<n>",
        strprintf("Seconds between vardiff retarget checks (default: %g)",
                  DEFAULT_VARDIFF_RETARGET_TIME),
        ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    args.AddArg("-stratummaxworkers=<n>",
                strprintf("Maximum concurrent Stratum workers (default: %d)",
                          DEFAULT_MAX_WORKERS),
                ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    args.AddArg(
        "-stratumworkertimeout=<n>",
        strprintf("Worker idle timeout in seconds (default: %d)",
                  DEFAULT_WORKER_TIMEOUT_SEC),
        ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    args.AddArg("-stratumcoinbase=<address>",
                "Lotus address for Stratum coinbase fallback payouts",
                ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    args.AddArg("-stratumproxy=<spec>",
                "Add an upstream pool for proxy/failover routing. Format: "
                "host:port:user[:pass[:priority]]. Can be specified multiple "
                "times. Lower priority = preferred. (default: none)",
                ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    args.AddArg("-stratumpreferlocal",
                "Prefer local node mining when synced over proxy "
                "(default: 1). Set to 0 to always prefer upstream pools.",
                ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    args.AddArg("-stratumwarnsolo",
                "Log a warning when solo-mining locally with no upstream "
                "pool available (default: 1)",
                ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    args.AddArg("-sharechain",
                "Enable P2Pool-style share chain for decentralized mining "
                "(default: 0)",
                ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    args.AddArg(
        "-sharechainwindow=<n>",
        strprintf("Share chain payout window size (default: %d, ~1h at "
                  "10s/share)",
                  DEFAULT_SHARECHAIN_WINDOW),
        ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
    args.AddArg("-sharedifficulty=<n>",
                "Initial share difficulty (0 = auto-adjust) (default: 0)",
                ArgsManager::ALLOW_ANY, OptionsCategory::BLOCK_CREATION);
}

bool ParseStratumConfig(const ArgsManager &args, StratumConfig &config,
                        std::string &error) {
    config.enabled = args.GetBoolArg("-stratum", false);
    if (!config.enabled) {
        return true;
    }

    config.bind = args.GetArg("-stratumbind", "0.0.0.0");

    int64_t port = args.GetArg("-stratumport", DEFAULT_STRATUM_PORT);
    if (port < 1 || port > 65535) {
        error = "Stratum port out of range (1-65535)";
        return false;
    }
    config.port = static_cast<uint16_t>(port);

    config.defaultDifficulty =
        atof(args.GetArg("-stratumdifficulty",
                         strprintf("%g", DEFAULT_STRATUM_DIFFICULTY))
                 .c_str());
    if (config.defaultDifficulty < MIN_DIFFICULTY) {
        error = strprintf("Stratum difficulty too low (minimum %g)",
                          MIN_DIFFICULTY);
        return false;
    }
    if (config.defaultDifficulty > MAX_DIFFICULTY) {
        error = strprintf("Stratum difficulty too high (maximum %g)",
                          MAX_DIFFICULTY);
        return false;
    }

    config.useVarDiff =
        args.GetBoolArg("-stratumvardiff", DEFAULT_STRATUM_VARDIFF);

    config.varDiffTargetTime =
        atof(args.GetArg("-stratumvardifftarget",
                         strprintf("%g", DEFAULT_VARDIFF_TARGET_TIME))
                 .c_str());
    if (config.varDiffTargetTime <= 0) {
        error = "Stratum vardiff target time must be positive";
        return false;
    }

    config.varDiffRetargetTime =
        atof(args.GetArg("-stratumvardiffretarget",
                         strprintf("%g", DEFAULT_VARDIFF_RETARGET_TIME))
                 .c_str());
    if (config.varDiffRetargetTime <= 0) {
        error = "Stratum vardiff retarget time must be positive";
        return false;
    }

    int64_t maxWorkers =
        args.GetArg("-stratummaxworkers", DEFAULT_MAX_WORKERS);
    if (maxWorkers < 1) {
        error = "Stratum max workers must be at least 1";
        return false;
    }
    config.maxWorkers = static_cast<int>(maxWorkers);

    int64_t timeout =
        args.GetArg("-stratumworkertimeout", DEFAULT_WORKER_TIMEOUT_SEC);
    if (timeout < 1) {
        error = "Stratum worker timeout must be at least 1 second";
        return false;
    }
    config.workerTimeoutSec = static_cast<int>(timeout);

    config.coinbaseAddress = args.GetArg("-stratumcoinbase", "");

    config.preferLocal = args.GetBoolArg("-stratumpreferlocal", true);
    config.warnSoloMining = args.GetBoolArg("-stratumwarnsolo", true);

    // Parse upstream pool specs: host:port:user[:pass[:priority]]
    for (const auto &spec : args.GetArgs("-stratumproxy")) {
        StratumPoolEntry entry;
        std::vector<std::string> parts;
        std::string token;
        for (char c : spec) {
            if (c == ':' && parts.size() < 4) {
                parts.push_back(token);
                token.clear();
            } else {
                token += c;
            }
        }
        parts.push_back(token);

        if (parts.size() < 3) {
            error = strprintf("-stratumproxy format: "
                              "host:port:user[:pass[:priority]], got '%s'",
                              spec);
            return false;
        }

        entry.host = parts[0];
        entry.port = static_cast<uint16_t>(atoi(parts[1].c_str()));
        if (entry.port == 0) {
            error = strprintf("Invalid port in -stratumproxy '%s'", spec);
            return false;
        }
        entry.username = parts[2];
        if (parts.size() > 3 && !parts[3].empty()) {
            entry.password = parts[3];
        }
        if (parts.size() > 4) {
            entry.priority = atoi(parts[4].c_str());
        } else {
            entry.priority = static_cast<int>(config.upstreamPools.size());
        }

        config.upstreamPools.push_back(entry);
    }

    // Share chain options
    config.sharechainEnabled = args.GetBoolArg("-sharechain", false);
    config.sharechainWindow =
        args.GetArg("-sharechainwindow", DEFAULT_SHARECHAIN_WINDOW);
    if (config.sharechainWindow < 10) {
        error = "Share chain window must be at least 10";
        return false;
    }
    config.shareDifficulty =
        atof(args.GetArg("-sharedifficulty", "0").c_str());

    return true;
}

} // namespace stratum
