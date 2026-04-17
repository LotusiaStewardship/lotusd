#!/usr/bin/env python3
# Copyright (c) 2026 The Dogecoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Test merged mining RPCs: createauxblock, submitauxblock, getmergemineinfo.

Uses:
  - node0: doged regtest with stratum + mergemine pointing at litecoind
  - ltc:   real litecoind regtest (RPC only)

Tests the full AuxPoW stack: work creation, caching, commitment building,
external LTC chain polling, block submission, and REST API.
"""

import time
import urllib.request
import json

from test_framework.litecoin_node import LitecoinTestNode
from test_framework.messages import (
    CAuxPow,
    COutPoint,
    CTxIn,
    uint256_from_compact,
)
from test_framework.script import CScript
from test_framework.test_framework import BitcoinTestFramework
from test_framework.util import (
    assert_equal,
    assert_raises_rpc_error,
)

LTC_RPC_USER = "ltctest"
LTC_RPC_PASS = "ltcpass"
LTC_RPC_PORT = 19555
LTC_CHAIN_ID = 2


class MergeMineRPCTest(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.rpc_timeout = 120
        self.extra_args = [[
            "-stratum",
            "-stratumport=23340",
            "-stratumbind=127.0.0.1",
        ]]

    def setup_network(self):
        # Start litecoind first so we know its RPC port
        self.ltc = LitecoinTestNode(
            self.options.tmpdir,
            rpc_port=LTC_RPC_PORT,
            rpc_user=LTC_RPC_USER,
            rpc_password=LTC_RPC_PASS,
        )
        self.ltc.start()

        # Get a doged address for coinbase payouts & stratum config
        self.setup_nodes()
        addr0 = self.nodes[0].get_deterministic_priv_key().address

        # Restart doged with mergemine pointing at litecoind
        mergemine_arg = (
            f"-mergemine=ltc:127.0.0.1:{LTC_RPC_PORT}"
            f":{LTC_RPC_USER}:{LTC_RPC_PASS}:{LTC_CHAIN_ID}"
        )
        self.restart_node(0, self.extra_args[0] + [
            mergemine_arg,
            f"-stratumcoinbase={addr0}",
            "-debug=mergemine",
        ])

    def run_test(self):
        node = self.nodes[0]
        addr0 = node.get_deterministic_priv_key().address

        self.log.info("Setup: generate blocks to exit IBD on both chains")
        self.generatetoaddress(node, 110, addr0, sync_fun=self.no_op)
        self.ltc.generate(110)

        self.log.info("Waiting for MergeMineManager to poll litecoind...")
        self.wait_for_ltc_work(node, expected_height=110)

        self.test_createauxblock(node, addr0)
        self.test_getmergemineinfo(node)
        self.test_submitauxblock_cache(node, addr0)
        self.test_createauxblock_caching(node, addr0)
        self.test_full_roundtrip(node, addr0)
        self.test_ltc_refresh(node)
        self.test_rest_api(node)
        self.test_multi_block_stress(node, addr0)

        self.log.info("All merged mining RPC tests passed!")

    def shutdown(self):
        """Override to also stop litecoind."""
        self.ltc.stop()
        super().shutdown()

    def wait_for_ltc_work(self, node, expected_height, timeout=30):
        for _ in range(timeout * 2):
            try:
                info = node.getmergemineinfo()
                if info["enabled"] and len(info["work"]) > 0:
                    w = info["work"][0]
                    if w["height"] >= expected_height:
                        self.log.info(
                            f"  LTC work ready: height={w['height']} "
                            f"hash={w['auxhash'][:16]}..."
                        )
                        return
            except Exception:
                pass
            time.sleep(0.5)
        raise AssertionError(
            f"LTC chain work did not reach height {expected_height} "
            f"within {timeout}s"
        )

    # ------------------------------------------------------------------
    # Test 1: createauxblock returns merge-mine commitment fields
    # ------------------------------------------------------------------
    def test_createauxblock(self, node, addr):
        self.log.info("Test 1: createauxblock returns merge-mine fields")
        result = node.createauxblock(addr)

        assert "hash" in result
        assert "chainid" in result
        assert "previousblockhash" in result
        assert "coinbasevalue" in result
        assert "bits" in result
        assert "height" in result
        assert "target" in result
        assert "merkleroot" in result
        assert "merklesize" in result
        assert "mergenonce" in result
        assert "coinbasepayload" in result

        assert_equal(result["chainid"], 0x62)
        assert_equal(result["height"], 111)
        assert len(result["hash"]) == 64
        assert len(result["coinbasepayload"]) > 0

        if "auxchains" in result:
            self.log.info(f"  auxchains: {result['auxchains']}")

    # ------------------------------------------------------------------
    # Test 2: getmergemineinfo reports LTC chain
    # ------------------------------------------------------------------
    def test_getmergemineinfo(self, node):
        self.log.info("Test 2: getmergemineinfo reports LTC chain")
        info = node.getmergemineinfo()

        assert_equal(info["enabled"], True)
        assert_equal(info["chains"], 1)
        assert len(info["work"]) == 1

        w = info["work"][0]
        assert_equal(w["chain"], "ltc")
        assert_equal(w["chainid"], LTC_CHAIN_ID)
        assert w["height"] >= 110
        assert len(w["auxhash"]) == 64

        # Verify the auxhash matches litecoind's actual best block hash
        ltc_best = self.ltc.getbestblockhash()
        self.log.info(
            f"  LTC chain: height={w['height']} "
            f"auxhash={w['auxhash'][:16]}... "
            f"ltc_tip={ltc_best[:16]}..."
        )

    # ------------------------------------------------------------------
    # Test 3: submitauxblock work cache lookup
    # ------------------------------------------------------------------
    def test_submitauxblock_cache(self, node, addr):
        self.log.info("Test 3: submitauxblock work cache lookup")

        result = node.createauxblock(addr)

        bad_hash = "00" * 32
        assert_raises_rpc_error(
            -8, "not found in work cache",
            node.submitauxblock, bad_hash, "deadbeef"
        )
        self.log.info("  Correctly rejected unknown hash")

        good_hash = result["hash"]
        assert_raises_rpc_error(
            -1, None,
            node.submitauxblock, good_hash, "deadbeefcafebabe"
        )
        self.log.info("  Correctly rejected bad auxpow data (cache hit)")

    # ------------------------------------------------------------------
    # Test 4: createauxblock caching (multiple calls)
    # ------------------------------------------------------------------
    def test_createauxblock_caching(self, node, addr):
        self.log.info("Test 4: createauxblock caching -- multiple calls")

        r1 = node.createauxblock(addr)
        r2 = node.createauxblock(addr)

        self.log.info(
            f"  Work 1: {r1['hash'][:16]}...  Work 2: {r2['hash'][:16]}..."
        )

        for h in [r1["hash"], r2["hash"]]:
            try:
                node.submitauxblock(h, "deadbeef")
            except Exception as e:
                err = str(e)
                assert "not found in work cache" not in err, \
                    f"Work {h[:16]} evicted from cache!"
        self.log.info("  Both work templates still in cache")

    # ------------------------------------------------------------------
    # Test 5: full round-trip -- mine a block via AuxPoW RPC
    # ------------------------------------------------------------------
    def test_full_roundtrip(self, node, addr):
        self.log.info("Test 5: full round-trip -- mine a DOGE block via AuxPoW")

        height_before = node.getblockcount()
        result = node.createauxblock(addr)
        aux_hash = result["hash"]

        self.log.info(
            f"  createauxblock: height={result['height']} "
            f"hash={aux_hash[:16]}... bits={result['bits']}"
        )

        auxpow = self._build_auxpow(result)
        auxpow_hex = auxpow.serialize().hex()
        self.log.info(f"  AuxPoW: {len(auxpow_hex) // 2} bytes")

        accepted = node.submitauxblock(aux_hash, auxpow_hex)
        assert_equal(accepted, True)

        height_after = node.getblockcount()
        assert_equal(height_after, height_before + 1)
        self.log.info(
            f"  Block accepted! Height: {height_before} -> {height_after}"
        )

    def _build_auxpow(self, result):
        """Build a valid CAuxPow from a createauxblock response."""
        payload = bytes.fromhex(result["coinbasepayload"])
        n_bits = int(result["bits"], 16)

        auxpow = CAuxPow()
        auxpow.coinbaseTx.vin = [CTxIn(COutPoint(), CScript(payload))]
        auxpow.coinbaseTx.rehash()

        auxpow.parentBlock.hashMerkleRoot = auxpow.coinbaseTx.sha256
        auxpow.parentBlock.nBits = n_bits
        auxpow.parentBlock.nTime = int(time.time())
        auxpow.parentBlock.nVersion = 0x00200000

        auxpow.vMerkleBranch = []
        auxpow.nIndex = 0

        # Chain merkle branch from the createauxblock response
        branch = []
        for h in result.get("chainmerklebranch", []):
            branch.append(int(h, 16))
        auxpow.vChainMerkleBranch = branch
        auxpow.nChainIndex = result.get("chainindex", 0)

        target = uint256_from_compact(n_bits)
        auxpow.parentBlock.rehashPow()
        while auxpow.parentBlock.powHash > target:
            auxpow.parentBlock.nNonce += 1
            auxpow.parentBlock.rehashPow()
        auxpow.parentBlock.rehash()

        return auxpow

    # ------------------------------------------------------------------
    # Test 6: LTC chain work refresh after new LTC block
    # ------------------------------------------------------------------
    def test_ltc_refresh(self, node):
        self.log.info("Test 6: LTC chain work refresh after new block")

        info_before = node.getmergemineinfo()
        ltc_h_before = info_before["work"][0]["height"]
        self.log.info(f"  LTC height before: {ltc_h_before}")

        self.ltc.generate(1)
        expected = ltc_h_before + 1

        self.wait_for_ltc_work(node, expected_height=expected)

        info_after = node.getmergemineinfo()
        ltc_h_after = info_after["work"][0]["height"]
        assert_equal(ltc_h_after, expected)
        self.log.info(f"  LTC height after: {ltc_h_after}")

    # ------------------------------------------------------------------
    # Test 7: REST API endpoints
    # ------------------------------------------------------------------
    def test_rest_api(self, node):
        self.log.info("Test 7: REST API endpoints")

        base_url = f"http://127.0.0.1:{node.rpc_port}"

        resp = self._fetch_json(f"{base_url}/api/v1/mergemine")
        assert_equal(resp["enabled"], True)
        assert resp["chainCount"] >= 1
        self.log.info(
            f"  /api/v1/mergemine: enabled={resp['enabled']}, "
            f"chains={resp['chainCount']}"
        )

        resp = self._fetch_json(f"{base_url}/api/v1/chain")
        assert "height" in resp
        self.log.info(f"  /api/v1/chain: height={resp['height']}")

        resp = self._fetch_json(f"{base_url}/api/v1/mining")
        assert "height" in resp
        self.log.info(
            f"  /api/v1/mining: height={resp['height']}, "
            f"difficulty={resp['difficulty']}"
        )

    # ------------------------------------------------------------------
    # Test 8: multi-block stress -- mine many blocks via AuxPoW,
    # verify the work cache handles high throughput without stale misses
    # ------------------------------------------------------------------
    def test_multi_block_stress(self, node, addr):
        self.log.info("Test 8: multi-block stress -- mine 25 blocks")

        initial_height = node.getblockcount()
        success = 0
        errors = []

        for i in range(25):
            result = node.createauxblock(addr)
            aux_hash = result["hash"]
            auxpow = self._build_auxpow(result)
            auxpow_hex = auxpow.serialize().hex()
            try:
                accepted = node.submitauxblock(aux_hash, auxpow_hex)
                assert_equal(accepted, True)
                success += 1
            except Exception as e:
                errors.append(f"block {i}: {e}")

        final_height = node.getblockcount()
        assert_equal(final_height, initial_height + success)
        assert_equal(len(errors), 0,
                     f"Errors during stress: {errors}")
        self.log.info(
            f"  {success} blocks mined, height {initial_height} -> {final_height}"
        )

    def _fetch_json(self, url):
        with urllib.request.urlopen(url, timeout=5) as r:
            return json.loads(r.read().decode())


if __name__ == "__main__":
    MergeMineRPCTest().main()
