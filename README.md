<p align="center">
  <a href="https://lotusia.org">
    <img src="share/pixmaps/lotuslogo.png" alt="Lotus" width="340">
  </a>
</p>

<p align="center">
  <a href="https://github.com/LotusiaStewardship/lotusd/actions/workflows/build.yml"><img src="https://github.com/LotusiaStewardship/lotusd/actions/workflows/build.yml/badge.svg" alt="Build status"></a>
  <a href="https://github.com/LotusiaStewardship/lotusd/releases/latest"><img src="https://img.shields.io/github/v/release/LotusiaStewardship/lotusd" alt="Latest release"></a>
  <a href="https://github.com/LotusiaStewardship/lotusd/tags"><img src="https://img.shields.io/github/v/tag/LotusiaStewardship/lotusd?label=version" alt="Version of this tree"></a>
  <a href="COPYING"><img src="https://img.shields.io/github/license/LotusiaStewardship/lotusd" alt="License: MIT"></a>
</p>

`lotusd` is the core full node for the Lotusia network: the reference implementation of Lotus
consensus. It validates and relays blocks, maintains the UTXO set, serves RPC, REST, ZMQ and NNG
interfaces, builds block templates for miners, and ships a wallet and desktop GUI.

Lotus (ticker **XPI**) is a proof-of-work currency whose issuance follows difficulty rather than a
halving schedule, and whose rules change only at scheduled upgrades. This repository is where
those rules are defined.

`lotusd` is a fork of [Bitcoin ABC](https://github.com/Bitcoin-ABC/bitcoin-abc), which descends
from Bitcoin Core. The transaction, script, mempool, P2P, wallet and RPC layers are ABC's work,
carried forward and modified; the consensus layer — 160-byte header, week-long epochs, ASERT
difficulty, difficulty-based subsidy, miner fund, `SIGHASH_LOTUS`, `lotus_` addresses — is Lotus's
own. [`doc/chain-mechanics.md`](doc/chain-mechanics.md) documents each rule against the source.

**Mainnet genesis:** 2021-06-21. Consensus changes activate by median time past on a solstice
cadence; the [activation table](doc/chain-mechanics.md#protocol-upgrades) lists every epoch with
its timestamp.

## Binaries

| Binary | Role |
| --- | --- |
| `lotusd` | Full node daemon |
| `lotus-cli` | RPC client |
| `lotus-tx` | Offline transaction utility |
| `lotus-wallet` | Wallet tool |
| `lotus-qt` | Desktop wallet |
| `lotus-seeder` | DNS seeder |

## Install

**Release binaries:** Linux `x86_64`, `arm-linux-gnueabihf` and `aarch64` tarballs with debug
symbols, a macOS `.dmg` and tarball, a Windows zip and installer, the source tarball, and
`SHA256SUMS`. Take them from the
[releases page](https://github.com/LotusiaStewardship/lotusd/releases) and verify:

```bash
sha256sum -c SHA256SUMS
```

**From source:** a C++17 compiler, CMake ≥ 3.16, Python ≥ 3.6, plus Boost, libevent and OpenSSL.

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j"$(nproc)"
cmake --install build --prefix "$HOME/.local"   # optional
```

A system-library build is the fast path, but the **supported** build is the pinned dependency tree.
Known failures on distributions that ship newer libraries than this codebase targets:

- **GCC 16 + binutils 2.42** — the assembler rejects the `.base64` directive; the build dies in
  `src/crypto/sha256.cpp`.
- **Boost 1.92** — `src/wallet` uses Filesystem APIs that release removed (`fs::copy_option`,
  `recursive_directory_iterator::level()`).
- **Qt 6** — the GUI requires Qt 5.

Use `depends/` for the pinned toolchain, or disable the affected component:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14 \
      -DBUILD_BITCOIN_WALLET=OFF -DBUILD_BITCOIN_QT=OFF
```

Platform notes: [unix](doc/build-unix.md) · [macOS](doc/build-osx.md) ·
[Windows](doc/build-windows.md) · [containers](dockerfiles).

## Run a node

```bash
lotusd -daemon
lotus-cli -rpcwait getblockchaininfo
lotus-cli stop
```

The data directory is `~/.lotus` on Linux (`-datadir` to change it); RPC authenticates with the
cookie file there unless you set `-rpcuser` / `-rpcpassword`. A minimal `~/.lotus/lotus.conf`:

```ini
listen=1
maxconnections=64
# prune=550        # keep ~550 MiB of blocks instead of the full chain
```

`-testnet` and `-regtest` select the other chains. **Do not expose the RPC port (10604) to the
internet** — it can spend wallet funds.

## Consensus at a glance

- **Header:** 160 bytes, not Bitcoin's 80. It commits to height, its own size, an epoch anchor and
  a metadata hash, and hashes in three SHA-256 layers.
- **Epochs:** 5040 blocks (~1 week); every block commits to the last block of the previous epoch.
- **Proof of work:** ASERT difficulty, 120 s spacing, 2-day half-life, retargeted every block.
- **Issuance:** `R = 260 XPI × (1 + log₂ D)`. No halving, no supply cap; bounded at 57,460 XPI per
  block by construction.
- **Miner fund:** half of every block reward must pay the epoch's consensus address set, either to
  one address per block (cycling) or split across all of them (fan-out).
- **Coinbase:** first output must be `OP_RETURN "logos" <height>` from height 1 up; that commitment
  keeps coinbase txids unique.
- **Scripts:** `SIGHASH_FORKID` on every input; `SIGHASH_LOTUS` for Taproot spends, enabled at the
  2nd Samuel activation.
- **Upgrades:** MTP-gated hard forks on a solstice cadence, each behind an `Is...Enabled`
  predicate that switches the rule set at its activation time.

Details, edge cases and source references: [`doc/chain-mechanics.md`](doc/chain-mechanics.md).

## Network

| | Mainnet | Testnet | Regtest |
| --- | --- | --- | --- |
| Data directory | `~/.lotus` | `~/.lotus/testnet3` | `~/.lotus/regtest` |
| P2P / RPC port | 10605 / 10604 | 11605 / 11604 | 12605 / 12604 |
| Genesis | `000000000abc0cde…89d958f470` | `00000000080a6c96…d18ca7bad1` | `106050de…7bba9c9f` |

Indexers, pools and other out-of-process consumers read blocks, undo data, mempool contents and
mining templates over the NNG interface (`src/nng_interface/nng_interface.fbs`), or subscribe to
ZMQ topics.

## Documentation

| Document | Contents |
| --- | --- |
| [Chain mechanics](doc/chain-mechanics.md) | Every consensus rule, with file references |
| [Upgrades](doc/upgrades/) | Per-epoch consensus change notes |
| [Configuration](doc/lotus-conf.md) | `lotus.conf` format and options |
| [RPC](doc/JSON-RPC-interface.md) | RPC and REST interfaces; per-command docs in [doc/rpc](doc/rpc) |
| [Data directory](doc/files.md) | Block files, chainstate, wallets, logs |
| [Release process](RELEASE.md) | How a version is cut, tagged and published |

`lotus-cli help` lists every command a running node exposes.

## Testing

Unit tests (not part of the default build) and the functional suite that drives real nodes over RPC
and P2P:

```bash
cmake --build build --target check
./build/test/functional/test_runner.py
./build/test/functional/test_runner.py --extended
```

`check-upgrade-activated` runs the same suite with activation timestamps forced into the past, so
post-upgrade code paths are exercised. CI runs lint on every pull request and the functional suite
for every tagged release. See [doc/unit-tests.md](doc/unit-tests.md) and
[doc/functional-tests.md](doc/functional-tests.md).

## Contributing

[`CONTRIBUTING.md`](CONTRIBUTING.md) has the long version. In short: branch from `master`, keep
diffs small, do not mix renames with behaviour changes, and add tests with the change. Consensus
changes must state their activation point and cover both sides of the boundary. Contributions are
MIT-licensed.

Discussion: [Telegram](https://t.me/givelotus).

## Security

Do not open a public issue for a vulnerability. See [`DISCLOSURE_POLICY.md`](DISCLOSURE_POLICY.md):
report privately, do not exploit on mainnet or testnet, and allow time for a coordinated fix.
Regtest is the only acceptable place to build a proof of concept.

## Attribution

Three layers of work are in this tree:

- **[Bitcoin Core](https://github.com/bitcoin/bitcoin)** — the original Bitcoin node.
- **[Bitcoin ABC](https://github.com/Bitcoin-ABC/bitcoin-abc)** — the Bitcoin Cash implementation
  this fork is based on.
- **[GiveLotus](https://github.com/givelotus/lotusd)** (2021–2024) — Shammah Chancellor
  ([@schancel](https://github.com/schancel)) and Tobias Ruck
  ([@EyeOfPython](https://github.com/EyeOfPython)) wrote the Lotus consensus layer: the 160-byte
  header and its three-layer hash, 5040-block epochs, ASERT difficulty, difficulty-based subsidy,
  the miner fund, `SIGHASH_LOTUS` and the `lotus_` address format.

The copyright headers in the source record this history and are kept intact.

## License

MIT — see [`COPYING`](COPYING), which carries the Bitcoin Core and Bitcoin ABC copyright notices.
Those attributions must be preserved in redistributions.

---

<p align="center">
  <a href="https://lotusia.org">Website</a> ·
  <a href="https://lotusia.org/blog">Blog</a> ·
  <a href="https://explorer.lotusia.org">Block explorer</a> ·
  <a href="https://app.lotusia.org">Web wallet</a> ·
  <a href="https://t.me/givelotus">Telegram</a>
</p>
