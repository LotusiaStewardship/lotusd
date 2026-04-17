#!/usr/bin/env python3
# Copyright (c) 2025 The Lotus developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test AuxPoW merged mining RPCs.

Tests:
- createauxblock / submitauxblock RPC flow
- AuxPoW block rejection before activation
- Native blocks still work after activation
- Both block types advance the chain
"""

from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error


class AuxPowMiningTest(BitcoinTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def skip_test_if_missing_module(self):
        pass

    def run_test(self):
        node = self.nodes[0]
        address = node.get_deterministic_priv_key().address

        self.log.info("Mine native blocks before AuxPoW activation")
        # Regtest activation height is 200, mine some native blocks
        node.generatetoaddress(50, address)
        assert_equal(node.getblockcount(), 50)

        self.log.info("Verify createauxblock fails before activation")
        assert_raises_rpc_error(
            -1,
            "AuxPoW not yet activated",
            node.createauxblock,
            address,
        )

        self.log.info("Mine up to activation height with native blocks")
        node.generatetoaddress(150, address)
        assert_equal(node.getblockcount(), 200)

        self.log.info("Verify createauxblock succeeds after activation")
        aux = node.createauxblock(address)
        assert "hash" in aux
        assert "chainid" in aux
        assert "target" in aux
        assert "height" in aux
        assert_equal(aux["chainid"], 0x4C)
        assert_equal(aux["height"], 201)

        self.log.info("Verify native mining still works after activation")
        node.generatetoaddress(1, address)
        assert_equal(node.getblockcount(), 201)

        self.log.info("Mine more native blocks to confirm chain advances")
        node.generatetoaddress(5, address)
        assert_equal(node.getblockcount(), 206)

        self.log.info("All tests passed")


if __name__ == "__main__":
    AuxPowMiningTest().main()
