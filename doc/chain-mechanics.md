# Chain mechanics

The consensus rules `lotusd` enforces, with the function or file that implements each one. Break
any of these and a block or transaction is invalid — not "non-standard", invalid — on every node
running the same rules.

Back to the [README](../README.md).

## Block header

Lotus does not use the Bitcoin 80-byte header. A Lotus header is a fixed **160 bytes** and carries
the block's height, its own serialized size, and an epoch anchor:

| Field | Size | Purpose |
| --- | --- | --- |
| `hashPrevBlock` | 32 | Parent block |
| `nBits` | 4 | Compact target |
| `vTime` | 6 | Little-endian timestamp |
| `nReserved` | 2 | Must be 0 |
| `nNonce` | 8 | Miner nonce |
| `nHeaderVersion` | 1 | Must be 1 |
| `vSize` | 7 | Committed block size (header + metadata + txs) |
| `nHeight` | 4 | Height, must equal parent + 1 |
| `hashEpochBlock` | 32 | Anchor of the block's epoch |
| `hashMerkleRoot` | 32 | Transaction merkle root |
| `hashExtendedMetadata` | 32 | Hash of the block's extended metadata |

The block hash is a nested SHA-256 construction, not double-SHA-256:

```
layer3 = SHA256(nHeaderVersion || vSize || nHeight || hashEpochBlock ||
                hashMerkleRoot || hashExtendedMetadata)
layer2 = SHA256(nBits || vTime || nReserved || nNonce || layer3)
hash   = SHA256(hashPrevBlock || layer2)
```

Implementation: `src/primitives/block.cpp`. The header is followed on the wire and on disk by the
extended-metadata vector and the transaction list (`src/primitives/block.h`).

Extended metadata is part of the header commitment but is required to be empty:
`hashExtendedMetadata` must equal the hash of the empty serialization and `vMetadata` must be
empty. The field exists so a future upgrade can commit data without changing the header layout.

The size commitment is checked in both directions: the claimed size may not exceed the node's
excessive-block-size setting, and it must equal the actual serialized size of the block
(`bad-blk-size`, `bad-blk-size-mismatch`).

## Epochs

Blocks are grouped into epochs of **5040 blocks** — roughly one week at 120 s spacing
(`EPOCH_NUM_BLOCKS`, `src/primitives/block.h`).

The first block of an epoch sets `hashEpochBlock` to its own parent hash; every later block in
that epoch repeats the same value. `ContextualCheckBlockHeader` enforces it (`src/validation.cpp`),
so the anchor is a consensus-visible grouping of the chain, independent of timestamps. Upgrades
and miner-fund address sets are keyed to MTP, not to epochs.

## Proof of work and difficulty

- **PoW hash:** the three-layer header hash above. A header is valid when that hash is at or below
  the target decoded from `nBits`, and `nBits` must exactly match the value the difficulty
  algorithm produces — non-canonical encodings are rejected.
- **Difficulty algorithm:** ASERT (`aserti32d`), with a **2-day half-life** and 120 s target
  spacing. Each half-life of drift doubles or halves the difficulty. It is absolute, anchored at
  the block after genesis, so there is no 2016-block window in the retarget path —
  `nMinerConfirmationWindow` is inherited from Bitcoin ABC and unused for retargeting.
- **Mainnet/testnet** retarget every block; **regtest** never retargets and accepts minimum
  difficulty. Testnet additionally mines a minimum-difficulty block when a block arrives more than
  two spacings after its parent.
- **Timestamps:** a block's timestamp must exceed the median time past of the previous 11 blocks
  and may not be more than two hours ahead of adjusted network time.

Implementation: `src/pow/pow.cpp`, `src/pow/aserti32d.cpp`.

## Block subsidy

Lotus issuance is not a halving schedule. `nSubsidyHalvingInterval` survives in the parameters but
is unused; the subsidy is a function of difficulty:

```
R = a * (1 + log2(D))          a = 260 XPI (base subsidy)
```

where `D` is difficulty against the chain's proof-of-work limit. The implementation works from the
compact target instead of the difficulty number, using a 27-bit fixed-point `log2`, which is why
the result is exact in satoshis rather than rounded from a floating-point difficulty
(`GetBlockSubsidy`, `src/validation.cpp`).

- Base: **260 XPI** per block at the proof-of-work limit.
- Upper bound: **221 × 260 = 57,460 XPI** at difficulty 2²²⁰; the formula is monotonic in
  difficulty, so the bound is mathematical, not a separate rule.
- Regtest pays a flat 260 XPI. Testnet uses the difficulty formula once its target is at least as
  hard as mainnet's proof-of-work limit.
- There is **no supply cap**. `MAX_MONEY` (2,100,000,000 XPI) is an overflow guard on individual
  amounts, not a monetary limit.

The intent is issuance that scales with the chain's difficulty rather than a fixed halving
schedule. That is a design choice of this fork, not an inherited Bitcoin ABC rule.

## Miner fund

**Half of every block reward** must be paid to the miner-fund address set of the epoch in force at
that height. Half of each block's fees is burned before the reward is computed, so the reward is the
subsidy plus the remaining half of the fees. The value is split in one of two modes:

- **cycling** — one output, to `addresses[height % n]`, worth 50% of the block reward;
- **fan-out** — one output per address, each worth `blockReward / (2 * n)`.

`GetMinerFundRequiredOutputs()` dispatches by epoch: 2nd Samuel used cycling, 1st Kings and 2nd
Kings use fan-out (2nd Kings has a single address, so fan-out and cycling are byte-identical
there). Address sets are consensus data in `src/consensus/addresses.h` and change only at an
upgrade.

Enforcement happens in `ConnectBlock`: the required outputs must appear in the coinbase, in order,
exactly matching script and amount, with unrelated outputs permitted between them. Failure is
`bad-cb-minerfund`. The other half is what the miner may claim; anything left unclaimed is
unspendable. A miner that builds its own coinbase — rather than using `getblocktemplate` — must
reproduce these outputs exactly or its blocks are invalid.

## Coinbase and block contents

- The first output of the coinbase must be `OP_RETURN <"logos"> <height>` (`COINBASE_PREFIX` is the
  five bytes of `logos`). This applies to every block above height 0. Trailing data after the
  height is allowed.
- That commitment is the only thing keeping coinbase transaction IDs unique: the coinbase input
  script is not part of the txid, and Lotus has no BIP30 check. Without it, a coinbase could repeat
  an earlier txid and overwrite unspent outputs (CVE-2012-1909 class). Blocks that omit or mangle
  the commitment are rejected with `bad-cb-height`.
- The coinbase input script must be between 2 and 100 bytes (`MAX_COINBASE_SCRIPTSIG_SIZE`).
- Coinbase outputs mature after **100 blocks** (`COINBASE_MATURITY`).
- The coinbase may not claim more than the subsidy plus the unburned half of the fees
  (`bad-cb-amount`); the other half of the fees is burned. The miner fund comes out of that same
  budget, not on top of it.
- Every transaction after the coinbase must be strictly ordered by transaction ID, ascending, with
  no duplicates (`tx-ordering`, `tx-duplicate`). Block templates built by `getblocktemplate` and
  `generatetoaddress` already satisfy this; hand-built blocks must sort.

## Scripts, signatures, replay protection

- **`SIGHASH_FORKID`** (BIP143-style) is enforced on every input; `CLEANSTACK` is always enforced.
- **`SIGHASH_LOTUS`** (`0x60`) is Lotus's own sighash algorithm. Instead of serializing the whole
  transaction, it commits to the input and output merkle roots, the spent-output set, and the
  summed input/output amounts, plus the taproot execution context (spend type, code-separator
  position, executed script hash). Sighash cost therefore stays flat as a transaction grows, and
  it is the only algorithm accepted for taproot spends.
- **Taproot** outputs carry a 33-byte commitment and support key-path spends and tapscript leaf
  spends. Taproot and `SIGHASH_LOTUS` are gated on the 2nd Samuel activation (2025-12-21).
- **Replay protection** changes the fork-id value (`^0xdead`, `0xff0000`) when MTP reaches the
  activation point given by `-replayprotectionactivationtime`, which defaults to
  `firstChroniclesActivationTime`. Signatures made for the old value are invalid once it is in
  force.
- **SigChecks:** signature-check operations are metered. A block may contain at most one sigcheck
  per 141 bytes of the configured maximum block size (`BLOCK_MAXBYTES_MAXSIGCHECKS_RATIO`), and
  sigchecks are counted at execution, not estimated from script bytes.
- Script flags are assembled per block in `GetNextBlockScriptFlags` (`src/validation.cpp`) from the
  epoch activation times; a new upgrade adds a branch there rather than editing the interpreter's
  default behaviour.

## Block size and transaction size

There is no fixed consensus block-size constant. A node accepts blocks up to its configured
`-excessiveblocksize` (default **32 MiB**), and the header's size commitment must match the
serialized block exactly. Individual transactions are capped at **1 MiB** (`MAX_TX_SIZE`).

This is the Bitcoin ABC model: the block-size ceiling is a node configuration choice, and
sigchecks — not bytes — are the scarce resource that keeps validation cost predictable.

## Address encoding

Addresses are encoded natively as **XAddress** strings (`src/addresses/xaddress.cpp`):

```
lotus_<base58(type || payload || 4-byte SHA-256 checksum)>    mainnet
lotusT...                                                     testnet
lotusR...                                                     regtest
```

- `type 0` carries a full `scriptPubKey` (P2PKH or P2SH), so the address commits to the spending
  script rather than only to a key hash.
- `type 2` carries a 33-byte taproot commitment.
- The checksum is the first four bytes of SHA-256 over `token || network || type || payload`.
- Decoding still accepts CashAddr and legacy base58 addresses, so tooling written for Bitcoin
  ABC-era formats keeps working; encoding always produces XAddress.

## Validation order

A block moves through these stages, and a failure at any of them is final for that block:

1. `CheckBlockHeader` — PoW, reserved field, header version, committed size.
2. `CheckBlock` — merkle root, extended-metadata hash, metadata emptiness, coinbase presence,
   transaction regularity, size commitment.
3. `ContextualCheckBlockHeader` — expected `nBits`, checkpoints, median-time-past, height
   continuity, epoch anchor.
4. `ContextualCheckBlock` — canonical transaction order, transaction finality against MTP, and the
   coinbase height commitment.
5. `ConnectBlock` — subsidy and miner-fund accounting, script execution against the UTXO set, the
   block sigcheck budget, and UTXO updates.

Reading those five functions in `src/validation.cpp` is the shortest path to understanding what
Lotus enforces. Checkpoints, `nMinimumChainWork` and `defaultAssumeValid` are bootstrapping aids on
top of these rules — they let a node skip work on buried history, they do not change what the rules
are.

## Consensus versus policy

Everything above is **consensus**: a block or transaction that violates it is invalid, and every
node must agree. Mempool admission is **policy**: minimum relay fee, dust limits, standardness and
mempool size rules live in `src/policy/` and can change without a fork. A node may refuse to relay
a transaction that it would still accept inside a block.

## Protocol upgrades

Upgrades activate by **median time past**, on a solstice cadence, and are named for biblical books.
Each one is a hard fork: after its activation point, a node running the old rules is on a different
chain from a node running the new ones. Testnet activates 21 days before mainnet; regtest uses
mainnet's times.

| Epoch | Mainnet MTP activation (UTC) | Timestamp |
| --- | --- | --- |
| Genesis | 2021-06-21 03:31 | — |
| Exodus | 2021-12-21 15:59 | `1640102340` |
| Leviticus | 2022-06-21 09:14 | `1655802840` |
| Numbers | 2022-12-21 21:48 | `1671659280` |
| Deuteronomy | 2023-06-21 14:58 | `1687359480` |
| Joshua | 2023-12-22 03:27 | `1703215620` |
| Judges | 2024-06-20 20:51 | `1718916660` |
| Ruth | 2024-12-21 09:20 | `1734772800` |
| 1st Samuel | 2025-06-21 02:42 | `1750473720` |
| 2nd Samuel | 2025-12-21 15:03 | `1766329380` |
| 1st Kings | 2026-06-21 08:24 | `1782030240` |
| 2nd Kings | 2026-12-21 20:50 | `1797886200` |
| 1st Chronicles | 2027-06-21 14:10 | `1813587000` |

What an epoch changes is the miner-fund address set and its payout mode. The table above is the
activation schedule and `doc/upgrades/` carries a write-up per epoch. The coinbase height commitment
is not epoch-scoped: it applies from height 1. Activation predicates live in
`src/consensus/activation.cpp`. Adding an upgrade means adding an activation time, an
`Is...Enabled` predicate, the rules it gates, and functional tests on both sides of the boundary.

## Avalanche

The tree contains Bitcoin ABC's Avalanche post-consensus implementation (`src/avalanche/`): stake
proofs, polling and finality voting. It is **disabled by default** (`AVALANCHE_DEFAULT_ENABLED =
false`) and is enabled with `-enableavalanche`. Lotus consensus rules do not depend on it; treat it
as available infrastructure rather than part of the enforced rule set.

## Network parameters

| Parameter | Mainnet | Testnet | Regtest |
| --- | --- | --- | --- |
| `-chain=` value | `main` | `test` | `regtest` |
| Default data directory | `~/.lotus` | `~/.lotus/testnet3` | `~/.lotus/regtest` |
| P2P port | `10605` | `11605` | `12605` |
| RPC port | `10604` | `11604` | `12604` |
| Genesis hash | `000000000abc0cde58ee7e919d3d4de183e6844add1fd5d14b4eac89d958f470` | `00000000080a6c9633aae9d24b9acda10d7e6b028e7aa714069798d18ca7bad1` | `106050de32db2a668422cc34aa0f96d739d4189b8e5d6e763deeca527bba9c9f` |
| Genesis time | `2021-06-21 03:31:00Z` | `2021-06-05 19:00:00Z` | `2020-09-13 12:26:40Z` |
| Target block spacing | 120 s | 120 s | 120 s |
| Difficulty adjustment | ASERT, 2-day half-life | ASERT, 2-day half-life + min-difficulty rule | no retargeting |
| Proof-of-work limit | `0x1c100000` | `0x1d00ffff` | `0x207fffff` |
| Block subsidy | difficulty-based | difficulty-based | fixed 260 XPI |

Values live in `src/chainparams.cpp`; derived constants are in `src/consensus/params.h` and
`src/chainparamsconstants.h`.
