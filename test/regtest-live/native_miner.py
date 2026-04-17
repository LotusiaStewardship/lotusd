#!/usr/bin/env python3
"""
Native triple-SHA-256 CPU miner for Lotus regtest.

Loops forever:
  1. getrawunsolvedblock -> block template
  2. Solve by incrementing nNonce until triple-SHA-256 < target
  3. submitblock
"""

import hashlib
import io
import struct
import sys
import time

from lotus_rpc import LotusRPC

POLL_INTERVAL = 2  # seconds between mining attempts


# ---------------------------------------------------------------------------
# Serialization helpers (same as test_framework/messages.py)
# ---------------------------------------------------------------------------

def sha256(s):
    return hashlib.sha256(s).digest()

def hash256(s):
    return sha256(sha256(s))

def ser_uint256(u):
    rs = b""
    for _ in range(8):
        rs += struct.pack("<I", u & 0xFFFFFFFF)
        u >>= 32
    return rs

def deser_uint256(f):
    r = 0
    for i in range(8):
        t = struct.unpack("<I", f.read(4))[0]
        r += t << (i * 32)
    return r

def uint256_from_compact(c):
    nbytes = (c >> 24) & 0xFF
    return (c & 0xFFFFFF) << (8 * (nbytes - 3))

def deser_compact_size(f):
    nit = struct.unpack("<B", f.read(1))[0]
    if nit == 253:
        nit = struct.unpack("<H", f.read(2))[0]
    elif nit == 254:
        nit = struct.unpack("<I", f.read(4))[0]
    elif nit == 255:
        nit = struct.unpack("<Q", f.read(8))[0]
    return nit

def ser_compact_size(size):
    if size < 253:
        return struct.pack("B", size)
    elif size < 0x10000:
        return struct.pack("<BH", 253, size)
    elif size < 0x100000000:
        return struct.pack("<BI", 254, size)
    else:
        return struct.pack("<BQ", 255, size)


# ---------------------------------------------------------------------------
# Lotus block header: 160 bytes
# ---------------------------------------------------------------------------

class LotusBlockHeader:
    def __init__(self):
        self.hashPrevBlock = 0
        self.nBits = 0
        self.nTime = 0
        self.nReserved = 0
        self.nNonce = 0
        self.nHeaderVersion = 1
        self.nSize = 0
        self.nHeight = 0
        self.hashEpochBlock = 0
        self.hashMerkleRoot = 0
        self.hashExtendedMetadata = 0

    def deserialize(self, f):
        self.hashPrevBlock = deser_uint256(f)
        self.nBits = int.from_bytes(f.read(4), 'little')
        self.nTime = int.from_bytes(f.read(6), 'little')
        self.nReserved = int.from_bytes(f.read(2), 'little')
        self.nNonce = int.from_bytes(f.read(8), 'little')
        self.nHeaderVersion = int.from_bytes(f.read(1), 'little')
        self.nSize = int.from_bytes(f.read(7), 'little')
        self.nHeight = int.from_bytes(f.read(4), 'little')
        self.hashEpochBlock = deser_uint256(f)
        self.hashMerkleRoot = deser_uint256(f)
        self.hashExtendedMetadata = deser_uint256(f)

    def serialize(self):
        r = bytearray()
        r += ser_uint256(self.hashPrevBlock)
        r += self.nBits.to_bytes(4, 'little')
        r += self.nTime.to_bytes(6, 'little')
        r += self.nReserved.to_bytes(2, 'little')
        r += self.nNonce.to_bytes(8, 'little')
        r += self.nHeaderVersion.to_bytes(1, 'little')
        r += self.nSize.to_bytes(7, 'little')
        r += self.nHeight.to_bytes(4, 'little')
        r += ser_uint256(self.hashEpochBlock)
        r += ser_uint256(self.hashMerkleRoot)
        r += ser_uint256(self.hashExtendedMetadata)
        return bytes(r)

    def calc_hash(self):
        layer3 = bytearray()
        layer3 += self.nHeaderVersion.to_bytes(1, 'little')
        layer3 += self.nSize.to_bytes(7, 'little')
        layer3 += self.nHeight.to_bytes(4, 'little')
        layer3 += ser_uint256(self.hashEpochBlock)
        layer3 += ser_uint256(self.hashMerkleRoot)
        layer3 += ser_uint256(self.hashExtendedMetadata)
        layer2 = bytearray()
        layer2 += self.nBits.to_bytes(4, 'little')
        layer2 += self.nTime.to_bytes(6, 'little')
        layer2 += self.nReserved.to_bytes(2, 'little')
        layer2 += self.nNonce.to_bytes(8, 'little')
        layer2 += hashlib.sha256(layer3).digest()
        layer1 = bytearray()
        layer1 += ser_uint256(self.hashPrevBlock)
        layer1 += hashlib.sha256(layer2).digest()
        return sha256(layer1)


def solve_block(block_hex):
    """Parse block, solve PoW, return solved block hex."""
    raw = bytes.fromhex(block_hex)
    f = io.BytesIO(raw)

    header = LotusBlockHeader()
    header.deserialize(f)
    remaining = f.read()

    target = uint256_from_compact(header.nBits)

    attempts = 0
    while True:
        hash_bytes = header.calc_hash()
        hash_int = int.from_bytes(hash_bytes, 'little')
        if hash_int <= target:
            solved = header.serialize() + remaining
            block_hash = hash_bytes[::-1].hex()
            return solved.hex(), block_hash, header.nHeight, attempts
        header.nNonce += 1
        attempts += 1
        if attempts > 1_000_000:
            return None, None, header.nHeight, attempts


def main():
    rpc = LotusRPC()
    print("[native-miner] Waiting for node RPC...", flush=True)
    rpc.wait_ready()
    print("[native-miner] Waiting for peers...", flush=True)
    rpc.wait_peers()

    # Wait for bootstrap to finish (activation height 200)
    while True:
        info = rpc.call("getblockchaininfo")
        if info["blocks"] >= 200:
            break
        print(f"[native-miner] Waiting for activation... height={info['blocks']}/200",
              flush=True)
        time.sleep(1)

    # Ensure wallet exists and get a valid address
    try:
        rpc.call("createwallet", ["miner"])
    except Exception:
        pass
    miner_address = rpc.call("getnewaddress")
    print(f"[native-miner] Using address: {miner_address}", flush=True)

    print("[native-miner] Starting mining loop", flush=True)
    blocks_found = 0

    while True:
        try:
            template = rpc.call("getrawunsolvedblock", [miner_address])
            block_hex = template["blockhex"]

            t0 = time.time()
            solved_hex, block_hash, height, attempts = solve_block(block_hex)
            elapsed = time.time() - t0

            if solved_hex is None:
                print(f"[native-miner] Failed to solve block at height {height} "
                      f"after {attempts} attempts", flush=True)
                time.sleep(POLL_INTERVAL)
                continue

            result = rpc.call("submitblock", [solved_hex])
            blocks_found += 1
            print(f"[native-miner] BLOCK #{blocks_found} height={height} "
                  f"hash={block_hash[:16]}... nonce_attempts={attempts} "
                  f"time={elapsed:.3f}s result={result}", flush=True)

        except Exception as e:
            print(f"[native-miner] Error: {e}", flush=True)

        time.sleep(POLL_INTERVAL)


if __name__ == "__main__":
    main()
