// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRYPTCHAIN_CHAIN_PARAMS_H
#define BITCOIN_SCRYPTCHAIN_CHAIN_PARAMS_H

#include <primitives/blockhash.h>
#include <uint256.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace scryptchain {

struct ScryptChainParams {
    std::string name;
    uint8_t netMagic[4];
    uint16_t defaultPort;
    uint32_t protocolVersion;
    std::string userAgent;
    BlockHash genesisHash;
    uint32_t genesisNBits;
    uint32_t genesisNTime;
    uint32_t genesisNNonce;
    int32_t genesisNVersion;
    std::vector<std::string> dnsSeeds;

    int64_t targetSpacing;
    int difficultyAdjustInterval;
    bool digishield;
    uint256 powLimit;

    bool isAuxPow;
    uint32_t auxpowChainId;
    int auxpowActivationHeight;

    int64_t initialSubsidySatoshis;
    int halvingInterval;
    bool hasSegWit;
    int maxBlockWeight;

    /**
     * Height at which the block reward becomes fixed (DOGE: 600,000).
     * Set to 0 to disable (use halving schedule only).
     */
    int fixedRewardHeight;
    int64_t fixedRewardSatoshis;

    /**
     * DigiShield parameters (DOGE only).
     * After digishieldHeight, difficulty retargets every block with
     * smoothing: newTimespan = target + (actual - target) / 8
     * clamped to [target*3/4, target*3/2].
     */
    int digishieldHeight;

    int coinbaseMaturity;

    std::map<int, BlockHash> checkpoints;
};

const ScryptChainParams &LitecoinMainnetParams();
const ScryptChainParams &LitecoinTestnetParams();
const ScryptChainParams &DogecoinMainnetParams();
const ScryptChainParams &DogecoinTestnetParams();

} // namespace scryptchain

#endif // BITCOIN_SCRYPTCHAIN_CHAIN_PARAMS_H
