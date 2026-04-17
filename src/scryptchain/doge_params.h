// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRYPTCHAIN_DOGE_PARAMS_H
#define BITCOIN_SCRYPTCHAIN_DOGE_PARAMS_H

#include <scryptchain/chain_params.h>

namespace scryptchain {

inline const ScryptChainParams &DogecoinMainnetParams() {
    static const ScryptChainParams params = [] {
        ScryptChainParams p{};
        p.name = "dogecoin";
        p.netMagic[0] = 0xc0;
        p.netMagic[1] = 0xc0;
        p.netMagic[2] = 0xc0;
        p.netMagic[3] = 0xc0;
        p.defaultPort = 22556;
        p.protocolVersion = 70016;
        p.userAgent = "/lotusd-doge:0.1/";

        p.genesisHash = BlockHash(uint256S(
            "1a91e3dace36e2be3bf030a65679fe821aa1d6ef92e7c9902"
            "eb318182c355691"));
        p.genesisNBits = 0x1e0ffff0;
        p.genesisNTime = 1386325540;
        p.genesisNNonce = 99943;
        p.genesisNVersion = 1;

        p.dnsSeeds = {
            "seed.multidoge.org",
            "seed2.multidoge.org",
        };

        p.targetSpacing = 60;
        p.difficultyAdjustInterval = 1;
        p.digishield = true;
        p.digishieldHeight = 145000;
        p.powLimit = uint256S(
            "00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        p.isAuxPow = true;
        p.auxpowChainId = 0x62;
        p.auxpowActivationHeight = 371337;

        p.initialSubsidySatoshis = 500000'00000000LL;
        p.halvingInterval = 100000;
        p.hasSegWit = false;
        p.maxBlockWeight = 1000000;

        p.fixedRewardHeight = 600000;
        p.fixedRewardSatoshis = 10000'00000000LL;

        p.coinbaseMaturity = 240;

        p.checkpoints = {
            {0, BlockHash(uint256S(
                "1a91e3dace36e2be3bf030a65679fe"
                "821aa1d6ef92e7c9902eb318182c355691"))},
            {104679, BlockHash(uint256S(
                "35eb87ae90d44b98898fec8c39577b"
                "76cb1eb08e1261cfc10706c8ce9a1d01cf"))},
            {371337, BlockHash(uint256S(
                "60323982f9c5c1381bc48fa6619257"
                "e7e8ef9c2399b3b30e7d2840dcbc4dc40e"))},
            {700000, BlockHash(uint256S(
                "395883a87b53a861e6e8aaa0aadb01"
                "c7dba6c7b32b5fdd8f1fd7c2da56ab7f97"))},
            {2000000, BlockHash(uint256S(
                "ddf72de738a1568e64a94e5fcd79fa"
                "2f3f77e8fb97e56b15a0ee70e4debb3ec0"))},
            {4000000, BlockHash(uint256S(
                "14c4663e7cb3ffcce85ce3b75c0489"
                "6b7b5dce24c4c0bb98efe64cff57c98d07"))},
        };

        return p;
    }();
    return params;
}

inline const ScryptChainParams &DogecoinTestnetParams() {
    static const ScryptChainParams params = [] {
        ScryptChainParams p{};
        p.name = "dogecoin-testnet";
        p.netMagic[0] = 0xfc;
        p.netMagic[1] = 0xc1;
        p.netMagic[2] = 0xb7;
        p.netMagic[3] = 0xdc;
        p.defaultPort = 44556;
        p.protocolVersion = 70016;
        p.userAgent = "/lotusd-doge:0.1/";

        p.genesisHash = BlockHash(uint256S(
            "bb0a78264637406b6360aad926284d544d7049f45189db5664"
            "f3c4d07350559e"));
        p.genesisNBits = 0x1e0ffff0;
        p.genesisNTime = 1391503289;
        p.genesisNNonce = 997879;
        p.genesisNVersion = 1;

        p.dnsSeeds = {"testseed.jrn.me.uk"};

        p.targetSpacing = 60;
        p.difficultyAdjustInterval = 1;
        p.digishield = true;
        p.digishieldHeight = 145000;
        p.powLimit = uint256S(
            "00000fffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

        p.isAuxPow = true;
        p.auxpowChainId = 0x62;
        p.auxpowActivationHeight = 158100;

        p.initialSubsidySatoshis = 500000'00000000LL;
        p.halvingInterval = 100000;
        p.hasSegWit = false;
        p.maxBlockWeight = 1000000;
        p.fixedRewardHeight = 600000;
        p.fixedRewardSatoshis = 10000'00000000LL;
        p.coinbaseMaturity = 240;

        return p;
    }();
    return params;
}

} // namespace scryptchain

#endif // BITCOIN_SCRYPTCHAIN_DOGE_PARAMS_H
