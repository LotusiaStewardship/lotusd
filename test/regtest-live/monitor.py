#!/usr/bin/env python3
"""
Live chain monitor for the dual-PoW regtest harness.
Polls getblockchaininfo and prints a rolling table of recent blocks,
showing PoW type (native vs AuxPoW) for each.
"""

import sys
import time

from lotus_rpc import LotusRPC

POLL_INTERVAL = 3
DISPLAY_BLOCKS = 15
EMPTY_METADATA_HASH = "9a538906e6466ebd2617d321f71bc94e56056ce213d366773699e28158e00614"


def get_pow_type(rpc, blockhash):
    """Determine if a block is native or AuxPoW by checking extendedmetadatahash."""
    block = rpc.call("getblock", [blockhash, 1])
    emh = block.get("extendedmetadatahash", EMPTY_METADATA_HASH)
    if emh != EMPTY_METADATA_HASH:
        return "AuxPoW"
    return "Native"


def main():
    rpc = LotusRPC()
    print("[monitor] Waiting for node RPC...", flush=True)
    rpc.wait_ready()
    print("[monitor] Connected. Monitoring chain...\n", flush=True)

    seen_height = -1
    block_cache = {}

    while True:
        try:
            info = rpc.call("getblockchaininfo")
            height = info["blocks"]
            chain = info.get("chain", "regtest")

            if height != seen_height:
                seen_height = height

                start = max(1, height - DISPLAY_BLOCKS + 1)
                rows = []

                for h in range(start, height + 1):
                    if h in block_cache:
                        rows.append(block_cache[h])
                        continue

                    bh = rpc.call("getblockhash", [h])
                    try:
                        pow_type = get_pow_type(rpc, bh)
                    except Exception:
                        pow_type = "???"
                    row = (h, bh[:16], pow_type)
                    block_cache[h] = row
                    rows.append(row)

                # Prune cache
                for k in list(block_cache.keys()):
                    if k < start:
                        del block_cache[k]

                # Print
                print("\033[2J\033[H", end="")  # clear screen
                print(f"=== Lotus Dual-PoW Monitor ({chain}) ===")
                print(f"Chain height: {height}\n")
                print(f"{'Height':>8}  {'Hash':>18}  {'PoW Type'}")
                print("-" * 44)
                native_count = 0
                auxpow_count = 0
                for h, hash_prefix, pow_type in rows:
                    marker = "\033[32m" if pow_type == "Native" else "\033[36m"
                    print(f"{h:>8}  {hash_prefix}..  {marker}{pow_type}\033[0m")
                    if pow_type == "Native":
                        native_count += 1
                    elif pow_type == "AuxPoW":
                        auxpow_count += 1
                print("-" * 44)
                print(f"Shown: {len(rows)} blocks  "
                      f"(Native: {native_count}, AuxPoW: {auxpow_count})")
                print(flush=True)

        except Exception as e:
            print(f"[monitor] Error: {e}", flush=True)

        time.sleep(POLL_INTERVAL)


if __name__ == "__main__":
    main()
