#!/usr/bin/env python3
"""
Scrypt AuxPoW CPU miner for Lotus regtest.

Loops forever:
  1. createauxblock -> block hash + target
  2. Build a fake Dogecoin parent coinbase containing the merge-mine commitment
  3. Build a parent block header, Scrypt-solve against AuxPoW target
  4. Serialize CAuxPow and submitauxblock
"""

import hashlib
import struct
import sys
import time

from lotus_rpc import LotusRPC

POLL_INTERVAL = 2  # seconds between mining attempts
AUXPOW_CHAIN_ID = 0x4C


# ---------------------------------------------------------------------------
# Serialization helpers
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

def ser_compact_size(size):
    if size < 253:
        return struct.pack("B", size)
    elif size < 0x10000:
        return struct.pack("<BH", 253, size)
    elif size < 0x100000000:
        return struct.pack("<BI", 254, size)
    else:
        return struct.pack("<BQ", 255, size)

def scrypt_hash(data):
    """Scrypt 1024,1,1,256 as used by Dogecoin/Litecoin."""
    return hashlib.scrypt(data, salt=data, n=1024, r=1, p=1, dklen=32)

def uint256_from_hex(hex_str):
    """Parse a uint256 hex string (big-endian display) to int."""
    return int(hex_str, 16)

def hex_to_le_bytes(hex_str, nbytes=32):
    """Convert a big-endian hex hash to little-endian bytes."""
    raw = bytes.fromhex(hex_str)
    if len(raw) < nbytes:
        raw = b'\x00' * (nbytes - len(raw)) + raw
    return raw[::-1]


# ---------------------------------------------------------------------------
# Build the merge-mine commitment payload
# ---------------------------------------------------------------------------

MERGE_MINE_PREFIX = bytes([0xfa, 0xbe, 0x6d, 0x6d])

def build_commitment_payload(aux_block_hash_hex):
    """
    Build the coinbase scriptSig payload for merged mining.
    Format: FABE6D6D + root_hash_big_endian(32) + tree_size_LE(4) + merge_nonce_LE(4)
    For single chain: tree_size=1, merge_nonce=0.
    """
    root_hash_be = bytes.fromhex(aux_block_hash_hex)
    if len(root_hash_be) < 32:
        root_hash_be = b'\x00' * (32 - len(root_hash_be)) + root_hash_be

    payload = bytearray()
    payload += MERGE_MINE_PREFIX
    payload += root_hash_be                         # 32 bytes big-endian
    payload += struct.pack("<I", 1)                  # treeSize = 1
    payload += struct.pack("<I", 0)                  # mergeNonce = 0
    return bytes(payload)


# ---------------------------------------------------------------------------
# Build a minimal coinbase transaction containing the commitment
# ---------------------------------------------------------------------------

def build_coinbase_tx(commitment_payload):
    """
    Build a minimal CTransaction (Lotus serialization format):
      nVersion(int32) || vin(vector<CTxIn>) || vout(vector<CTxOut>) || nLockTime(uint32)
    """
    r = bytearray()

    # nVersion
    r += struct.pack("<i", 1)

    # vin: 1 input (coinbase)
    r += ser_compact_size(1)

    # CTxIn: COutPoint(TxId=0, n=0xFFFFFFFF) || scriptSig || nSequence
    r += b'\x00' * 32                               # TxId (null)
    r += struct.pack("<I", 0xFFFFFFFF)               # n (coinbase marker)
    r += ser_compact_size(len(commitment_payload))   # scriptSig length
    r += commitment_payload                          # scriptSig data
    r += struct.pack("<I", 0xFFFFFFFF)               # nSequence

    # vout: 1 output (empty, value=0)
    r += ser_compact_size(1)
    r += struct.pack("<q", 0)                        # nValue
    r += ser_compact_size(0)                         # scriptPubKey (empty)

    # nLockTime
    r += struct.pack("<I", 0)

    return bytes(r)


# ---------------------------------------------------------------------------
# Build and solve the parent block header
# ---------------------------------------------------------------------------

def build_parent_header(coinbase_tx_bytes, ntime):
    """
    Build an 80-byte parent block header.
    hashMerkleRoot = hash256(coinbase_tx) for a single-tx block.
    nVersion must NOT have Lotus chain ID in upper bits.
    """
    coinbase_txhash = hash256(coinbase_tx_bytes)

    r = bytearray()
    r += struct.pack("<i", 1)                        # nVersion (no chain ID)
    r += b'\x00' * 32                                # hashPrevBlock (null)
    r += coinbase_txhash                             # hashMerkleRoot
    r += struct.pack("<I", ntime)                    # nTime
    r += struct.pack("<I", 0x207fffff)               # nBits (regtest max)
    r += struct.pack("<I", 0)                        # nNonce (placeholder)
    return bytes(r)


def solve_parent_header(header_bytes, target_int):
    """Increment nNonce in the 80-byte header until scrypt < target."""
    header = bytearray(header_bytes)
    nonce = 0
    while True:
        struct.pack_into("<I", header, 76, nonce)
        h = scrypt_hash(bytes(header))
        hash_int = int.from_bytes(h, 'little')
        if hash_int <= target_int:
            return bytes(header), nonce
        nonce += 1
        if nonce > 1_000_000:
            return None, nonce


# ---------------------------------------------------------------------------
# Serialize CAuxPow for submitauxblock
# ---------------------------------------------------------------------------

def serialize_auxpow(coinbase_tx_bytes, parent_header_bytes):
    """
    Serialize a CAuxPow:
      coinbaseTx || hashBlock(=0) || vMerkleBranch(=[]) || nIndex(=0)
      || vChainMerkleBranch(=[]) || nChainIndex(=0) || parentBlock(80 bytes)
    """
    r = bytearray()
    r += coinbase_tx_bytes                           # coinbaseTx (raw serialized)
    r += b'\x00' * 32                                # hashBlock
    r += ser_compact_size(0)                         # vMerkleBranch (empty)
    r += struct.pack("<I", 0)                        # nIndex
    r += ser_compact_size(0)                         # vChainMerkleBranch (empty)
    r += struct.pack("<I", 0)                        # nChainIndex
    r += parent_header_bytes                         # parentBlock (80 bytes)
    return bytes(r)


# ---------------------------------------------------------------------------
# Main mining loop
# ---------------------------------------------------------------------------

def main():
    rpc = LotusRPC()
    print("[scrypt-miner] Waiting for node RPC...", flush=True)
    rpc.wait_ready()
    print("[scrypt-miner] Waiting for peers...", flush=True)
    rpc.wait_peers()

    # Wait for bootstrap to finish (activation height 200)
    while True:
        info = rpc.call("getblockchaininfo")
        if info["blocks"] >= 200:
            break
        print(f"[scrypt-miner] Waiting for activation... height={info['blocks']}/200",
              flush=True)
        time.sleep(1)

    # Ensure wallet exists and get a valid address
    try:
        rpc.call("createwallet", ["miner"])
    except Exception:
        pass
    miner_address = rpc.call("getnewaddress")
    print(f"[scrypt-miner] Using address: {miner_address}", flush=True)

    print("[scrypt-miner] Starting mining loop", flush=True)
    blocks_found = 0

    while True:
        try:
            aux = rpc.call("createauxblock", [miner_address])
            aux_hash = aux["hash"]
            target_hex = aux["target"]
            height = aux["height"]
            target_int = uint256_from_hex(target_hex)

            t0 = time.time()

            # 1. Build merge-mine commitment
            commitment = build_commitment_payload(aux_hash)

            # 2. Build coinbase tx
            coinbase_tx = build_coinbase_tx(commitment)

            # 3. Build and solve parent header
            ntime = int(time.time())
            parent_header = build_parent_header(coinbase_tx, ntime)
            solved_header, nonce = solve_parent_header(parent_header, target_int)

            elapsed = time.time() - t0

            if solved_header is None:
                print(f"[scrypt-miner] Failed to solve parent at height {height} "
                      f"after {nonce} attempts", flush=True)
                time.sleep(POLL_INTERVAL)
                continue

            # 4. Serialize CAuxPow
            auxpow_data = serialize_auxpow(coinbase_tx, solved_header)
            auxpow_hex = auxpow_data.hex()

            # 5. Submit
            result = rpc.call("submitauxblock", [aux_hash, auxpow_hex])
            blocks_found += 1
            parent_hash = hash256(solved_header)[::-1].hex()
            print(f"[scrypt-miner] BLOCK #{blocks_found} height={height} "
                  f"aux_hash={aux_hash[:16]}... parent_hash={parent_hash[:16]}... "
                  f"nonce={nonce} time={elapsed:.3f}s result={result}", flush=True)

        except Exception as e:
            print(f"[scrypt-miner] Error: {e}", flush=True)

        time.sleep(POLL_INTERVAL)


if __name__ == "__main__":
    main()
