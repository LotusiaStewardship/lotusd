#!/usr/bin/env python3
"""
Stratum v1 mining client for Lotus regtest.

Connects to the lotusd Stratum server, subscribes, authorizes, receives
jobs via mining.notify, and submits shares. This validates the full
Stratum server pipeline end-to-end.
"""

import hashlib
import json
import socket
import struct
import sys
import time

STRATUM_HOST = "lotusd-node1"
STRATUM_PORT = 3334
WORKER_NAME = "lotusR_regtest_worker.rig1"
WORKER_PASS = "x"


def sha256(data):
    return hashlib.sha256(data).digest()


def ser_uint256(u):
    rs = b""
    for _ in range(8):
        rs += struct.pack("<I", u & 0xFFFFFFFF)
        u >>= 32
    return rs


def uint256_from_compact(c):
    nbytes = (c >> 24) & 0xFF
    return (c & 0xFFFFFF) << (8 * (nbytes - 3))


class StratumClient:
    def __init__(self, host, port):
        self.host = host
        self.port = port
        self.sock = None
        self.buf = b""
        self._id = 0
        self.extranonce1 = None
        self.extranonce2_size = 8
        self.job = None
        self.target = None

    def connect(self):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.settimeout(30)
        self.sock.connect((self.host, self.port))

    def send_json(self, obj):
        line = json.dumps(obj) + "\n"
        self.sock.sendall(line.encode())

    def recv_line(self):
        while b"\n" not in self.buf:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise ConnectionError("Connection closed")
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return json.loads(line.decode())

    def call(self, method, params=None):
        self._id += 1
        self.send_json({
            "id": self._id,
            "method": method,
            "params": params or [],
        })
        return self._id

    def subscribe(self):
        self.call("mining.subscribe", ["stratum-test/1.0"])
        resp = self.recv_line()
        while resp.get("method"):
            resp = self.recv_line()
        if resp.get("error") and resp["error"] != [None]:
            raise RuntimeError(f"Subscribe failed: {resp['error']}")
        result = resp["result"]
        self.extranonce1 = result[1]
        self.extranonce2_size = result[2]
        print(f"[stratum-miner] Subscribed: extranonce1={self.extranonce1}, "
              f"en2_size={self.extranonce2_size}", flush=True)

    def authorize(self, worker, password):
        self.call("mining.authorize", [worker, password])
        resp = self.recv_line()
        while resp.get("method"):
            if resp["method"] == "mining.set_difficulty":
                diff = resp["params"][0]
                pdiff_target = int(0x00000000FFFF0000000000000000000000000000000000000000000000000000 / diff)
                self.target = pdiff_target
                print(f"[stratum-miner] Difficulty set: {diff}", flush=True)
            elif resp["method"] == "mining.notify":
                self._parse_job(resp["params"])
            resp = self.recv_line()
        if not resp.get("result"):
            raise RuntimeError(f"Authorize failed: {resp.get('error')}")
        print("[stratum-miner] Authorized", flush=True)

    def _parse_job(self, params):
        self.job = {
            "id": params[0],
            "prevhash": params[1],
            "coinbase1": params[2],
            "coinbase2": params[3],
            "merkle_branches": params[4],
            "layer3_hash": params[5] if len(params) > 5 else "",
            "nbits": params[6] if len(params) > 6 else "",
            "ntime": params[7] if len(params) > 7 else "",
            "reserved": params[8] if len(params) > 8 else "",
            "clean": params[9] if len(params) > 9 else True,
        }
        print(f"[stratum-miner] Job received: id={self.job['id']}, "
              f"clean={self.job['clean']}", flush=True)

    def mine_and_submit(self):
        """Build coinbase, compute share, submit."""
        if not self.job:
            return False

        extranonce2 = "00" * self.extranonce2_size

        coinbase_hex = (self.job["coinbase1"] + self.extranonce1 +
                        extranonce2 + self.job["coinbase2"])
        coinbase_bin = bytes.fromhex(coinbase_hex)
        coinbase_hash = sha256(sha256(coinbase_bin))

        merkle_root = coinbase_hash
        for branch in self.job["merkle_branches"]:
            branch_bin = bytes.fromhex(branch)
            merkle_root = sha256(sha256(merkle_root + branch_bin))

        ntime = self.job["ntime"]
        nonce = "00" * 8

        self.call("mining.submit", [
            WORKER_NAME,
            self.job["id"],
            extranonce2,
            ntime,
            nonce,
        ])

        resp = self.recv_line()
        while resp.get("method"):
            if resp["method"] == "mining.notify":
                self._parse_job(resp["params"])
            elif resp["method"] == "mining.set_difficulty":
                diff = resp["params"][0]
                print(f"[stratum-miner] Difficulty update: {diff}", flush=True)
            resp = self.recv_line()

        accepted = resp.get("result", False)
        error = resp.get("error")
        return accepted, error

    def wait_for_messages(self, timeout=5):
        """Poll for notifications."""
        self.sock.settimeout(timeout)
        try:
            chunk = self.sock.recv(4096)
            if chunk:
                self.buf += chunk
                while b"\n" in self.buf:
                    line, self.buf = self.buf.split(b"\n", 1)
                    msg = json.loads(line.decode())
                    if msg.get("method") == "mining.notify":
                        self._parse_job(msg["params"])
                    elif msg.get("method") == "mining.set_difficulty":
                        diff = msg["params"][0]
                        self.target = int(0x00000000FFFF0000000000000000000000000000000000000000000000000000 / diff)
                        print(f"[stratum-miner] Difficulty update: {diff}",
                              flush=True)
        except socket.timeout:
            pass
        finally:
            self.sock.settimeout(30)


def main():
    print("[stratum-miner] Waiting for stratum server...", flush=True)
    time.sleep(5)

    client = None
    for attempt in range(60):
        try:
            client = StratumClient(STRATUM_HOST, STRATUM_PORT)
            client.connect()
            print("[stratum-miner] Connected to stratum server", flush=True)
            break
        except Exception as e:
            if attempt % 10 == 0:
                print(f"[stratum-miner] Connecting... ({e})", flush=True)
            time.sleep(2)

    if not client:
        print("[stratum-miner] FAILED to connect", flush=True)
        sys.exit(1)

    client.subscribe()
    client.authorize(WORKER_NAME, WORKER_PASS)

    shares_accepted = 0
    shares_rejected = 0

    while True:
        try:
            client.wait_for_messages(timeout=3)

            if client.job:
                accepted, error = client.mine_and_submit()
                if accepted:
                    shares_accepted += 1
                    print(f"[stratum-miner] Share ACCEPTED "
                          f"(total: {shares_accepted})", flush=True)
                else:
                    shares_rejected += 1
                    err_msg = error[1] if error and len(error) > 1 else error
                    print(f"[stratum-miner] Share REJECTED: {err_msg} "
                          f"(total rejected: {shares_rejected})", flush=True)

        except ConnectionError:
            print("[stratum-miner] Connection lost, reconnecting...",
                  flush=True)
            time.sleep(5)
            try:
                client = StratumClient(STRATUM_HOST, STRATUM_PORT)
                client.connect()
                client.subscribe()
                client.authorize(WORKER_NAME, WORKER_PASS)
            except Exception as e:
                print(f"[stratum-miner] Reconnect failed: {e}", flush=True)
                time.sleep(5)
        except Exception as e:
            print(f"[stratum-miner] Error: {e}", flush=True)
            time.sleep(3)


if __name__ == "__main__":
    main()
