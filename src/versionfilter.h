// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_VERSIONFILTER_H
#define BITCOIN_VERSIONFILTER_H

#include <string>

/**
 * Parsed client version structure
 */
struct ClientVersion {
    int major{0};
    int minor{0};
    int revision{0};
    bool valid{false};

    ClientVersion() = default;
    ClientVersion(int maj, int min, int rev)
        : major(maj), minor(min), revision(rev), valid(true) {}

    /**
     * Compare versions: returns true if this < other
     */
    bool operator<(const ClientVersion &other) const {
        if (major != other.major) {
            return major < other.major;
        }
        if (minor != other.minor) {
            return minor < other.minor;
        }
        return revision < other.revision;
    }

    bool operator==(const ClientVersion &other) const {
        return major == other.major && minor == other.minor &&
               revision == other.revision;
    }

    bool operator<=(const ClientVersion &other) const {
        return (*this < other) || (*this == other);
    }

    bool operator>(const ClientVersion &other) const {
        return !(*this <= other);
    }

    bool operator>=(const ClientVersion &other) const {
        return !(*this < other);
    }

    std::string ToString() const;
};

/**
 * Parse client version from user agent string
 * Expected format: "/lotusd:10.4.5(EB32.0)/" or similar
 *
 * @param userAgent The user agent string from the VERSION message
 * @return Parsed client version, or invalid version if parsing fails
 */
ClientVersion ParseClientVersion(const std::string &userAgent);

/**
 * Check if a peer should be disconnected based on version filtering rules
 *
 * @param peerVersion The peer's client version
 * @param currentHeight Current blockchain height
 * @param isTestnet Whether we're running on testnet
 * @return true if the peer should be disconnected
 */
bool ShouldDisconnectPeerByVersion(const ClientVersion &peerVersion,
                                   int currentHeight, bool isTestnet);

/**
 * Get the configured testnet fork height from args
 * After this height, testnet nodes will reject connections from 9.x.x and
 * earlier
 *
 * @return The fork height, or -1 if not configured/not applicable
 */
int GetTestnetForkHeight();

#endif // BITCOIN_VERSIONFILTER_H

