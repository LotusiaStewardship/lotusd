#!/usr/bin/env python3
# Copyright (c) 2026 The Dogecoin developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""
Live merged mining console monitor -- multi-chain edition.

Starts a doged regtest node with stratum and a real litecoind regtest node,
configures merge mining between them, then continuously mines AuxPoW blocks
and displays a rich terminal dashboard showing per-chain target hits.

Each share's PoW hash is compared independently against every external
chain's nBits target. The dashboard shows which chains each block satisfies:
  - DOGE only:  share meets DOGE target but no external chain
  - DOGE + LTC: share meets both DOGE and LTC targets

In regtest both chains have the same trivial minimum difficulty, so we
simulate realistic difficulty ratios: by default, ~1 in 8 blocks also
hits the (simulated) LTC target.

Usage:
    ./test/functional/mining_mergemine_monitor.py --configfile=build/test/config.ini
    ./test/functional/mining_mergemine_monitor.py --configfile=build/test/config.ini --blocks=20
    ./test/functional/mining_mergemine_monitor.py --configfile=build/test/config.ini --interval=3
    ./test/functional/mining_mergemine_monitor.py --configfile=build/test/config.ini --ltc-ratio=4
"""

import random
import time

from test_framework.litecoin_node import LitecoinTestNode
from test_framework.messages import (
    CAuxPow,
    COutPoint,
    CTxIn,
    uint256_from_compact,
)
from test_framework.script import CScript
from test_framework.test_framework import BitcoinTestFramework

LTC_RPC_USER = "ltctest"
LTC_RPC_PASS = "ltcpass"
LTC_RPC_PORT = 19556
LTC_CHAIN_ID = 2


class C:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    DIM = "\033[2m"
    GREEN = "\033[32m"
    YELLOW = "\033[33m"
    CYAN = "\033[36m"
    WHITE = "\033[97m"
    RED = "\033[31m"
    MAGENTA = "\033[35m"
    BLUE = "\033[34m"
    BOLD_GREEN = "\033[1;32m"
    BOLD_YELLOW = "\033[1;33m"
    BOLD_CYAN = "\033[1;36m"
    BOLD_WHITE = "\033[1;97m"
    BOLD_RED = "\033[1;31m"
    BOLD_MAGENTA = "\033[1;35m"
    BOLD_BLUE = "\033[1;34m"
    BG_GREEN = "\033[42;97m"
    BG_YELLOW = "\033[43;30m"


BOX_H = "\u2550"
BOX_V = "\u2551"
BOX_TL = "\u2554"
BOX_TR = "\u2557"
BOX_BL = "\u255a"
BOX_BR = "\u255d"
BOX_LT = "\u2560"
BOX_RT = "\u2563"
LINE_H = "\u2500"
LINE_V = "\u2502"
LINE_LT = "\u251c"

W = 86


def box_top():
    return f"{BOX_TL}{BOX_H * (W - 2)}{BOX_TR}"


def box_bot():
    return f"{BOX_BL}{BOX_H * (W - 2)}{BOX_BR}"


def box_mid():
    return f"{BOX_LT}{BOX_H * (W - 2)}{BOX_RT}"


def box_line(text, color=""):
    inner = W - 4
    reset = C.RESET if color else ""
    return f"{BOX_V} {color}{text:<{inner}}{reset} {BOX_V}"


def box_line_raw(content):
    return f"{BOX_V} {content} {BOX_V}"


def trunc(h, n=16):
    if not h:
        return "?" * n
    s = str(h)
    return s[:n - 2] + ".." if len(s) > n else s


class MergeMineMonitor(BitcoinTestFramework):
    def set_test_params(self):
        self.setup_clean_chain = True
        self.num_nodes = 1
        self.extra_args = [[
            "-stratum",
            "-stratumport=23341",
            "-stratumbind=127.0.0.1",
        ]]
        self.rpc_timeout = 120
        self.block_log = []
        self.doge_only_count = 0
        self.doge_ltc_count = 0
        self.total_time = 0.0
        self.start_time = 0

    def add_options(self, parser):
        parser.add_argument(
            "--blocks", type=int, default=20,
            help="Number of AuxPoW blocks to mine (default: 20)",
        )
        parser.add_argument(
            "--interval", type=float, default=2.0,
            help="Delay between blocks in seconds (default: 2.0)",
        )
        parser.add_argument(
            "--ltc-ratio", type=int, default=8,
            help="Simulated LTC difficulty ratio: 1 in N blocks also "
                 "hits LTC target (default: 8)",
        )

    def setup_network(self):
        self.ltc = LitecoinTestNode(
            self.options.tmpdir,
            rpc_port=LTC_RPC_PORT,
            rpc_user=LTC_RPC_USER,
            rpc_password=LTC_RPC_PASS,
        )
        self.ltc.start()

        self.setup_nodes()
        addr0 = self.nodes[0].get_deterministic_priv_key().address

        mergemine_arg = (
            f"-mergemine=ltc:127.0.0.1:{LTC_RPC_PORT}"
            f":{LTC_RPC_USER}:{LTC_RPC_PASS}:{LTC_CHAIN_ID}"
        )
        self.restart_node(0, self.extra_args[0] + [
            mergemine_arg,
            f"-stratumcoinbase={addr0}",
            "-debug=mergemine",
        ])

    def shutdown(self):
        self.ltc.stop()
        super().shutdown()

    def run_test(self):
        node = self.nodes[0]
        addr0 = node.get_deterministic_priv_key().address

        num_blocks = self.options.blocks
        interval = self.options.interval
        ltc_ratio = self.options.ltc_ratio

        print(f"\n{C.BOLD_CYAN}Bootstrapping DOGE + LTC regtest chains...{C.RESET}")
        self.generatetoaddress(node, 110, addr0, sync_fun=self.no_op)
        self.ltc.generate(110)

        print(f"{C.BOLD_CYAN}Waiting for merge mining to discover LTC chain...{C.RESET}")
        for _ in range(60):
            try:
                info = node.getmergemineinfo()
                if info["enabled"] and len(info["work"]) > 0:
                    break
            except Exception:
                pass
            time.sleep(0.5)

        self.start_time = time.time()
        print(f"{C.BOLD_GREEN}Ready! Mining {num_blocks} blocks "
              f"(~1 in {ltc_ratio} hits LTC target)...{C.RESET}\n")
        time.sleep(1.0)

        for i in range(num_blocks):
            if i > 0 and i % 5 == 0:
                self.ltc.generate(1)
                time.sleep(0.5)

            hits_ltc = (random.randint(1, ltc_ratio) == 1)

            t0 = time.time()
            entry = self._mine_auxpow_block(node, addr0, hits_ltc)
            elapsed = time.time() - t0
            self.total_time += elapsed

            if entry:
                entry["solve_ms"] = elapsed * 1000
                entry["block_num"] = i + 1
                self.block_log.append(entry)

                if "error" not in entry:
                    chains_hit = entry.get("chains_hit", [])
                    if chains_hit:
                        self.doge_ltc_count += 1
                        if hits_ltc:
                            self.ltc.generate(1)
                    else:
                        self.doge_only_count += 1

            self._draw_dashboard(node)

            if interval > 0 and i < num_blocks - 1:
                time.sleep(interval)

        self._draw_dashboard(node)
        total = self.doge_only_count + self.doge_ltc_count
        print(f"\n{C.BOLD_GREEN}Mining complete! {total} blocks mined.")
        print(f"  {C.BOLD_YELLOW}DOGE only : {self.doge_only_count}{C.RESET}")
        print(f"  {C.BOLD_MAGENTA}DOGE + ext: {self.doge_ltc_count}{C.RESET}\n")

    def _mine_auxpow_block(self, node, addr, hits_ltc):
        try:
            result = node.createauxblock(addr)
        except Exception as e:
            return {"error": str(e)}

        aux_hash = result["hash"]

        try:
            mm_info = node.getmergemineinfo()
            ext_chains = mm_info.get("work", [])
        except Exception:
            ext_chains = []

        auxpow = self._build_auxpow(result)
        auxpow_hex = auxpow.serialize().hex()

        try:
            node.submitauxblock(aux_hash, auxpow_hex)
        except Exception as e:
            return {"error": str(e), "aux_hash": aux_hash}

        new_tip = node.getbestblockhash()
        height = node.getblockcount()

        parent_hash = (
            f"{auxpow.parentBlock.sha256:064x}"
            if auxpow.parentBlock.sha256 else "?"
        )

        chains_hit = []
        if hits_ltc:
            chains_hit.append("LTC")

        entry = {
            "height": height,
            "doge_hash": new_tip,
            "parent_hash": parent_hash,
            "chains_hit": chains_hit,
        }

        for ew in ext_chains:
            entry[f"ext_{ew['chain']}_height"] = ew["height"]
            entry[f"ext_{ew['chain']}_hash"] = ew["auxhash"]

        return entry

    def _build_auxpow(self, result):
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

    def _draw_dashboard(self, node):
        try:
            doge_info = node.getblockchaininfo()
            ltc_info = self.ltc.getblockchaininfo()
            mm_info = node.getmergemineinfo()
        except Exception:
            return

        inner = W - 4
        half = (inner - 3) // 2

        uptime = time.time() - self.start_time
        total = self.doge_only_count + self.doge_ltc_count
        avg = (self.total_time / total * 1000) if total else 0

        print("\033[2J\033[H", end="")

        lines = []
        lines.append(box_top())

        title = "DOGED x LITECOIN  MERGED MINING MONITOR"
        pad = (inner - len(title)) // 2
        lines.append(box_line(f"{' ' * pad}{C.BOLD_CYAN}{title}{C.RESET}"))
        lines.append(box_mid())

        # ---- Chain comparison ----
        left_title = "DOGE chain (child / AuxPoW)"
        right_title = "LTC chain (parent / PoW)"
        lines.append(box_line_raw(
            f"{C.BOLD_YELLOW}{left_title:<{half}}{C.RESET} {LINE_V} "
            f"{C.BOLD_MAGENTA}{right_title:<{half}}{C.RESET}"
        ))

        dh = doge_info.get("blocks", 0)
        eh = ltc_info.get("blocks", 0)
        dt = trunc(doge_info.get("bestblockhash", ""), 22)
        et = trunc(ltc_info.get("bestblockhash", ""), 22)
        dd = f"{doge_info.get('difficulty', 0):.2e}"
        ed = f"{ltc_info.get('difficulty', 0):.2e}"

        def two_col(label, lval, rval):
            left = f"  {label}: {lval}"
            right = f"  {label}: {rval}"
            return box_line_raw(
                f"{C.WHITE}{left:<{half}}{C.RESET} {LINE_V} "
                f"{C.WHITE}{right:<{half}}{C.RESET}"
            )

        lines.append(two_col("Height", dh, eh))
        lines.append(two_col("Tip   ", dt, et))
        lines.append(two_col("Diff  ", dd, ed))
        lines.append(box_mid())

        # ---- Merge mining status ----
        mm_enabled = "YES" if mm_info.get("enabled") else "NO"
        mm_chains = mm_info.get("chains", 0)
        lines.append(box_line(f"{C.BOLD_WHITE}MERGE MINING STATUS  "
                              f"{C.DIM}(per-chain independent target check){C.RESET}"))
        status = (
            f"  Enabled: {C.BOLD_GREEN}{mm_enabled}{C.RESET}   "
            f"Ext chains: {C.BOLD_CYAN}{mm_chains}{C.RESET}   "
            f"Mined: {C.BOLD_WHITE}{total}{C.RESET}  "
            f"({C.BOLD_YELLOW}DOGE-only:{self.doge_only_count}{C.RESET}  "
            f"{C.BOLD_MAGENTA}DOGE+ext:{self.doge_ltc_count}{C.RESET})"
        )
        lines.append(box_line_raw(f"{status:<{inner}}"))

        stats = (
            f"  Avg solve: {C.CYAN}{avg:.1f}ms{C.RESET}   "
            f"Uptime: {C.CYAN}{uptime:.0f}s{C.RESET}   "
            f"LTC hit rate: {C.CYAN}"
            f"{self.doge_ltc_count}/{total if total else 1}"
            f"{C.RESET}"
        )
        lines.append(box_line_raw(f"{stats:<{inner}}"))
        lines.append(box_mid())

        # ---- Block log ----
        lines.append(box_line(
            f"{C.BOLD_WHITE}MINED BLOCKS{C.RESET}  "
            f"({C.BOLD_YELLOW}\u25cf{C.RESET} = DOGE only   "
            f"{C.BOLD_MAGENTA}\u25cf{C.RESET} = DOGE + ext chain(s))"
        ))

        hdr = (
            f" {C.DIM}{'#':>3} {'Ht':>5} {LINE_V} "
            f"{'DOGE block hash':<22} {LINE_V} "
            f"{'Parent PoW hash':<22} {LINE_V} "
            f"{'Result':<15}{C.RESET}"
        )
        lines.append(box_line_raw(hdr))

        sep = (
            f" {LINE_H * 3} {LINE_H * 5}{LINE_LT}"
            f"{LINE_H * 22}{LINE_LT}"
            f"{LINE_H * 22}{LINE_LT}"
            f"{LINE_H * 15}"
        )
        lines.append(box_line_raw(f"{C.DIM}{sep}{C.RESET}"))

        display = list(reversed(self.block_log[-12:]))
        for idx, entry in enumerate(display):
            if "error" in entry:
                err = (
                    f" {'':>3} {'ERR':>5} {LINE_V} "
                    f"{trunc(entry.get('error', ''), 62)}"
                )
                lines.append(box_line_raw(f"{C.RED}{err}{C.RESET}"))
                continue

            num = entry.get("block_num", "?")
            ht = entry.get("height", "?")
            dh = trunc(entry.get("doge_hash", ""), 22)
            ph = trunc(entry.get("parent_hash", ""), 22)

            chains_hit = entry.get("chains_hit", [])
            if chains_hit:
                chain_str = "+".join(chains_hit)
                result_str = (
                    f"{C.BOLD_MAGENTA}\u25cf DOGE+{chain_str}{C.RESET}"
                )
            else:
                result_str = (
                    f"{C.BOLD_YELLOW}\u25cf DOGE only{C.RESET}   "
                )

            if idx == 0:
                color = C.BOLD_WHITE
            elif idx < 4:
                color = C.WHITE
            else:
                color = C.DIM

            row = (
                f" {color}{num:>3} {ht:>5}{C.RESET} {LINE_V} "
                f"{color}{dh:<22}{C.RESET} {LINE_V} "
                f"{color}{ph:<22}{C.RESET} {LINE_V} "
                f"{result_str}"
            )
            lines.append(box_line_raw(row))

        for _ in range(max(0, 3 - len(display))):
            empty = (
                f" {'':>3} {'':>5} {LINE_V} {'':22} {LINE_V} "
                f"{'':22} {LINE_V} {'':15}"
            )
            lines.append(box_line_raw(f"{C.DIM}{empty}{C.RESET}"))

        lines.append(box_mid())

        # ---- Legend / footer ----
        legend = (
            f"  {C.DIM}Each share is checked against every chain's nBits "
            f"independently.{C.RESET}"
        )
        lines.append(box_line_raw(f"{legend:<{inner}}"))
        footer = (
            f"  {C.DIM}[Ctrl+C to stop]{C.RESET}   "
            f"Uptime: {C.CYAN}{uptime:.0f}s{C.RESET}"
        )
        lines.append(box_line_raw(f"{footer:<{inner}}"))
        lines.append(box_bot())

        print("\n".join(lines))


if __name__ == "__main__":
    MergeMineMonitor().main()
