#!/usr/bin/env python3
"""
Share chain and stratum monitor for the P2Pool regtest harness.

Polls the API endpoints:
  - /api/v1/stratum    (stratum server stats)
  - /api/v1/sharechain (share chain stats)
  - /api/v1/stratum/workers (connected workers)

Displays a live dashboard of the P2Pool state.
"""

import json
import sys
import time
import urllib.request
import urllib.error

API_BASE = "http://lotusd-node1:12604/api/v1"
POLL_INTERVAL = 5


def api_get(path):
    url = f"{API_BASE}/{path}"
    try:
        req = urllib.request.Request(url)
        resp = urllib.request.urlopen(req, timeout=10)
        return json.loads(resp.read().decode())
    except Exception:
        return None


def format_hashrate(h):
    if h >= 1e12:
        return f"{h/1e12:.2f} TH/s"
    elif h >= 1e9:
        return f"{h/1e9:.2f} GH/s"
    elif h >= 1e6:
        return f"{h/1e6:.2f} MH/s"
    elif h >= 1e3:
        return f"{h/1e3:.2f} KH/s"
    return f"{h:.2f} H/s"


def main():
    print("[sharechain-monitor] Waiting for API...", flush=True)
    time.sleep(8)

    for attempt in range(60):
        stratum = api_get("stratum")
        if stratum is not None:
            break
        time.sleep(2)
    else:
        print("[sharechain-monitor] API not available", flush=True)
        sys.exit(1)

    print("[sharechain-monitor] Connected. Monitoring...\n", flush=True)

    while True:
        try:
            stratum = api_get("stratum")
            sharechain = api_get("sharechain")
            workers = api_get("stratum/workers")

            print("\033[2J\033[H", end="")
            print("=" * 60)
            print("  LOTUS P2Pool / Stratum Monitor (regtest)")
            print("=" * 60)

            if stratum:
                enabled = stratum.get("enabled", False)
                print(f"\n  Stratum Server: {'ENABLED' if enabled else 'DISABLED'}")
                if enabled:
                    print(f"  Active Workers: {stratum.get('activeWorkers', 0)}")
                    print(f"  Shares Accepted: {stratum.get('totalSharesAccepted', 0)}")
                    print(f"  Shares Rejected: {stratum.get('totalSharesRejected', 0)}")
                    print(f"  Shares Stale: {stratum.get('totalSharesStale', 0)}")
                    print(f"  Blocks Found: {stratum.get('blocksFound', 0)}")
                    print(f"  Chain Height: {stratum.get('chainHeight', '?')}")
                    print(f"  Routing Tier: {stratum.get('activeTier', '?')}")

            if sharechain:
                enabled = sharechain.get("enabled", False)
                print(f"\n  Share Chain: {'ENABLED' if enabled else 'DISABLED'}")
                if enabled:
                    print(f"  Share Height: {sharechain.get('height', 0)}")
                    print(f"  Window: {sharechain.get('sharesInWindow', 0)}"
                          f"/{sharechain.get('windowSize', 0)}")
                    print(f"  Share Rate: {sharechain.get('shareRate', 0):.2f} shares/min")
                    print(f"  Share Difficulty: {sharechain.get('shareDifficulty', 0)}")

                    payouts = sharechain.get("payouts", [])
                    if payouts:
                        print(f"\n  Payout Window ({len(payouts)} miners):")
                        print(f"  {'Address':>20}  {'Shares':>7}  {'%':>6}  {'Est. Amount':>12}")
                        print("  " + "-" * 52)
                        for p in payouts[:10]:
                            addr = p.get("address", "?")
                            if len(addr) > 18:
                                addr = addr[:8] + ".." + addr[-8:]
                            print(f"  {addr:>20}  {p.get('shares', 0):>7}  "
                                  f"{p.get('percentage', 0):>5.1f}%  "
                                  f"{p.get('estimatedAmount', 0):>12}")

                    recent = sharechain.get("recentShares", [])
                    if recent:
                        print(f"\n  Recent Shares (last {len(recent)}):")
                        print(f"  {'Hash':>18}  {'Height':>7}  {'Miner':>20}  {'Algo':>7}")
                        print("  " + "-" * 58)
                        for s in recent:
                            miner = s.get("miner", "?")
                            if len(miner) > 18:
                                miner = miner[:8] + ".." + miner[-8:]
                            print(f"  {s.get('hash', '?'):>18}  "
                                  f"{s.get('height', 0):>7}  "
                                  f"{miner:>20}  "
                                  f"{s.get('algorithm', '?'):>7}")

            if workers:
                print(f"\n  Connected Workers ({len(workers)}):")
                if workers:
                    print(f"  {'Name':>20}  {'State':>12}  {'Algo':>7}  "
                          f"{'Diff':>8}  {'Accepted':>8}")
                    print("  " + "-" * 62)
                    for w in workers:
                        name = w.get("name", "?")
                        if len(name) > 18:
                            name = name[:8] + ".." + name[-8:]
                        print(f"  {name:>20}  {w.get('state', '?'):>12}  "
                              f"{w.get('algorithm', '?'):>7}  "
                              f"{w.get('difficulty', 0):>8.4f}  "
                              f"{w.get('accepted', 0):>8}")

            print(f"\n  Last update: {time.strftime('%H:%M:%S')}")
            print("=" * 60, flush=True)

        except Exception as e:
            print(f"[sharechain-monitor] Error: {e}", flush=True)

        time.sleep(POLL_INTERVAL)


if __name__ == "__main__":
    main()
