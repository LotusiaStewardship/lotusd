# Live Dual-PoW Integration Test

End-to-end test of merged mining (Scrypt AuxPoW) alongside native triple-SHA-256
mining on a Lotus regtest chain.

## Architecture

```
┌──────────────────────────────────────────────────┐
│  docker compose                                  │
│                                                  │
│  lotusd-node1 ◄──P2P──► lotusd-node2             │
│       ▲                                          │
│       │ RPC (port 12604)                         │
│       ├───── native-miner  (triple-SHA-256)      │
│       ├───── scrypt-miner  (Scrypt AuxPoW)       │
│       └───── monitor       (chain status)        │
└──────────────────────────────────────────────────┘
```

- **node1**: Primary regtest node, miners connect here via RPC.
- **node2**: Peer-only node — satisfies the `GetNodeCount > 0` check that
  `getrawunsolvedblock` and `createauxblock` require.
- **bootstrap**: Generates 200 blocks to reach AuxPoW activation height, then exits.
- **native-miner**: Mines blocks using `getrawunsolvedblock` + `submitblock`.
- **scrypt-miner**: Mines blocks using `createauxblock` + `submitauxblock`.
- **monitor**: Polls the chain and displays recent blocks with their PoW type.

## Prerequisites

- Docker and Docker Compose v2+

## How to Run

```bash
cd test/regtest-live
docker compose up --build
```

## Expected Output

1. Both nodes start and peer with each other (~1–2 seconds).
2. Bootstrap generates 200 blocks via `generatetoaddress` (~2 seconds at min difficulty).
3. Both miners start simultaneously.
4. Blocks are found instantly (nonce 0 — regtest difficulty is locked at maximum target).
5. Every ~2 seconds a new block appears, alternating between native and AuxPoW.
6. The monitor prints a rolling table showing block heights and PoW types.

Example monitor output:

```
=== Lotus Dual-PoW Monitor (regtest) ===
Chain height: 210

  Height              Hash  PoW Type
--------------------------------------------
     196  0f3a8e2c1b9d7f4a..  Native
     197  a7c2e1d4f6b830e9..  AuxPoW
     198  e5d1c8b3a9f74062..  Native
     199  3b8f2a6d0e7c5194..  AuxPoW
     200  c4a91e3d7f2b6058..  Native
     201  8d6f4b2e1a0c9753..  Native
     202  f2e7d8c3b1a40596..  AuxPoW
     203  1a4b9e7d3f2c8065..  Native
     204  6c3d2f8a1e7b4059..  AuxPoW
     205  b5e8d1c4a3f70296..  Native
     206  2f7a9e3d1b4c8065..  AuxPoW
     207  9d1c4b8e3a7f2056..  Native
     208  4e8f2d7a1c3b6095..  AuxPoW
     209  7b3c9e1d4a2f8065..  Native
     210  d2a4f8e7c1b30596..  AuxPoW
--------------------------------------------
Shown: 15 blocks  (Native: 8, AuxPoW: 7)
```

## Difficulty

Both `powLimit` and `auxpowPowLimit` are `0x7fff...` (max 256-bit target) and
`fPowNoRetargeting = true`. This means nonce 0 always produces a valid hash.
No real mining computation occurs — the test validates the block construction,
submission, and consensus validation pipeline.

## Stopping

```bash
docker compose down
```
