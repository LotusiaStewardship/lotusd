// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRYPTCHAIN_LTC_PARAMS_H
#define BITCOIN_SCRYPTCHAIN_LTC_PARAMS_H

#include <scryptchain/chain_params.h>

namespace scryptchain {

inline const ScryptChainParams &LitecoinMainnetParams() {
    static const ScryptChainParams params = [] {
        ScryptChainParams p;
        p.name = "litecoin";
        p.netMagic[0] = 0xfb;
        p.netMagic[1] = 0xc0;
        p.netMagic[2] = 0xb6;
        p.netMagic[3] = 0xdb;
        p.defaultPort = 9333;
        p.protocolVersion = 70015;
        p.userAgent = "/lotusd-ltc:0.1/";

        p.genesisHash = BlockHash(uint256S(
            "12a765e31ffd4059bada1e25190f6e98c99d9714d334efa41a195a7e7e04bfe2"));
        p.genesisNBits = 0x1e0ffff0;
        p.genesisNTime = 1317972665;
        p.genesisNNonce = 2084524493;
        p.genesisNVersion = 1;

        p.dnsSeeds = {
            "seed-a.litecoin.loshan.co.uk",
            "dnsseed.thrasher.io",
            "dnsseed.litecointools.com",
            "dnsseed.litecoinpool.org",
        };

        p.targetSpacing = 150;
        p.difficultyAdjustInterval = 2016;
        p.digishield = false;
        p.digishieldHeight = 0;
        p.powLimit = uint256S(
            "00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        p.isAuxPow = false;
        p.auxpowChainId = 0;
        p.auxpowActivationHeight = 0;

        p.initialSubsidySatoshis = 50'00000000LL;
        p.halvingInterval = 840000;
        p.hasSegWit = true;
        p.maxBlockWeight = 4000000;
        p.fixedRewardHeight = 0;
        p.fixedRewardSatoshis = 0;
        p.coinbaseMaturity = 100;

        p.checkpoints = {
            {1500, BlockHash(uint256S(
                "841a2965955dd288cfa707a755f02a"
                "bbe210f51f7f829f138c5c4e0ab2e61f00"))},
            {4032, BlockHash(uint256S(
                "9ce90e427198fc0ef05e5905ce3c1"
                "b3c5530a5931215cee37340b41d86737e51"))},
            {100000, BlockHash(uint256S(
                "1b66eac50f74aee3ba109fd2a3112"
                "ad3b3b455c6fd91e65e6d7e6aee1f1684b8"))},
            {250000, BlockHash(uint256S(
                "c8bc59c9fb3fdc08ecf9acd8e40f9"
                "a2f05aab0c22afac35195b8a73c7d8c7ae9"))},
        };

        return p;
    }();
    return params;
}

inline const ScryptChainParams &LitecoinTestnetParams() {
    static const ScryptChainParams params = [] {
        ScryptChainParams p;
        p.name = "litecoin-testnet";
        p.netMagic[0] = 0xfd;
        p.netMagic[1] = 0xd2;
        p.netMagic[2] = 0xc8;
        p.netMagic[3] = 0xf1;
        p.defaultPort = 19335;
        p.protocolVersion = 70015;
        p.userAgent = "/lotusd-ltc:0.1/";

        p.genesisHash = BlockHash(uint256S(
            "4966625a4b2851d9fdee139e56211a0d88575"
            "f59ed816ff5e6a63deb4e3e29a0"));
        p.genesisNBits = 0x1e0ffff0;
        p.genesisNTime = 1486949366;
        p.genesisNNonce = 293345;
        p.genesisNVersion = 1;

        p.dnsSeeds = {"testnet-seed.litecointools.com"};

        p.targetSpacing = 150;
        p.difficultyAdjustInterval = 2016;
        p.digishield = false;
        p.digishieldHeight = 0;
        p.powLimit = uint256S(
            "00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        p.isAuxPow = false;
        p.auxpowChainId = 0;
        p.auxpowActivationHeight = 0;

        p.initialSubsidySatoshis = 50'00000000LL;
        p.halvingInterval = 840000;
        p.hasSegWit = true;
        p.maxBlockWeight = 4000000;
        p.fixedRewardHeight = 0;
        p.fixedRewardSatoshis = 0;
        p.coinbaseMaturity = 100;

        return p;
    }();
    return params;
}

} // namespace scryptchain

#endif // BITCOIN_SCRYPTCHAIN_LTC_PARAMS_H
