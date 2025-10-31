// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <versionfilter.h>

#include <chainparams.h>
#include <consensus/activation.h>
#include <logging.h>
#include <tinyformat.h>
#include <util/system.h>

#include <regex>
#include <sstream>

std::string ClientVersion::ToString() const {
    if (!valid) {
        return "unknown";
    }
    return strprintf("%d.%d.%d", major, minor, revision);
}

ClientVersion ParseClientVersion(const std::string &userAgent) {
    // Expected formats:
    // "/lotusd:10.4.5(EB32.0)/"
    // "/lotusd:9.2.1/"
    // "lotusd:10.4.5"

    ClientVersion result;

    // Try to match version pattern: digits.digits.digits
    // Look for pattern like "lotusd:X.Y.Z" or "lotusd/X.Y.Z"
    std::regex version_regex(
        R"(lotusd[:/]?v?(\d+)\.(\d+)\.(\d+))",
        std::regex_constants::icase);
    std::smatch match;

    if (std::regex_search(userAgent, match, version_regex)) {
        if (match.size() == 4) {
            try {
                result.major = std::stoi(match[1].str());
                result.minor = std::stoi(match[2].str());
                result.revision = std::stoi(match[3].str());
                result.valid = true;
                
                // Only log first time we see each version
                static std::set<std::string> logged_versions;
                if (logged_versions.find(result.ToString()) == logged_versions.end()) {
                    logged_versions.insert(result.ToString());
                    LogPrint(BCLog::NET,
                             "Parsed client version: %s\n", result.ToString());
                }
            } catch (const std::exception &e) {
                // Silent - not important
            }
        }
    }

    return result;
}

int GetTestnetForkHeight() {
    // Get the configured fork height from command-line args
    // Default to Second Samuel activation height if not specified
    static const int DEFAULT_TESTNET_FORK_HEIGHT = 0;

    int forkHeight =
        gArgs.GetArg("-testnetforkheight", DEFAULT_TESTNET_FORK_HEIGHT);

    return forkHeight;
}

bool ShouldDisconnectPeerByVersion(const ClientVersion &peerVersion,
                                   int currentHeight, bool isTestnet) {
    // Get the configured fork height
    int forkHeight = GetTestnetForkHeight();

    // If fork height is 0 or negative, feature is disabled
    if (forkHeight <= 0) {
        LogPrint(BCLog::NET,
                 "Version filtering disabled (forkheight=%d)\n",
                 forkHeight);
        return false;
    }

    // If peer version couldn't be parsed, allow connection
    // (be permissive to avoid accidentally blocking legitimate nodes)
    if (!peerVersion.valid) {
        LogPrint(BCLog::NET,
                 "Peer version invalid/unparsed - allowing connection\n");
        return false;
    }

    // If we haven't reached the fork height yet, allow all connections
    if (currentHeight < forkHeight) {
        LogPrint(BCLog::NET,
                 "Below fork height (%d < %d) - allowing peer version %s\n",
                 currentHeight, forkHeight, peerVersion.ToString());
        return false;
    }

    // After fork height, reject 9.x.x and earlier versions
    ClientVersion minimumVersion(10, 0, 0);

    if (peerVersion < minimumVersion) {
        // Only log once per unique version to avoid spam
        static std::map<std::string, int64_t> last_log_time;
        const std::string ver_key = peerVersion.ToString();
        const int64_t now = GetTimeMillis();
        
        // Log at most once every 5 minutes per version
        if (last_log_time[ver_key] == 0 || now - last_log_time[ver_key] > 300000) {
            last_log_time[ver_key] = now;
            LogPrintf(
                     "FORK: Rejecting peer version %s (< 10.0.0) at height %d\n",
                     peerVersion.ToString(), currentHeight);
        }
        return true;
    }

    // Silently accept - no need to log every message
    return false;
}

