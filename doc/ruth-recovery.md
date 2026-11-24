# Ruth Epoch Recovery (December 2024)

Record of the Ruth upgrade defect, its hotfix, and the on-chain evidence that
the recovery put the chain on the correct fork. Written so the facts are
verifiable from a synced node or the public explorer.

## What happened

The Ruth upgrade (`v8.3.3`, upgrade commit `6f488b931f`, "Winter Solstice 2024
Upgrade (Ruth epoch) #5") shipped missing two consensus-critical steps. (The
`v8.3.3` tag itself points at `23eca5b81`, "Update Ruth Epoch IFP address
(#7)", which sets the Ruth IFP address.)

1. **No miner-fund dispatch branch.** `GetMinerFundRequiredOutputs` had no
   `IsRuthEnabled` case, so at the Ruth activation it fell through to the
   Judges case and continued paying the Judges address.
2. **Replay protection not advanced.** `IsReplayProtectionEnabled` still
   defaulted to `ruthActivationTime` instead of `firstSamuelActivationTime`,
   activating the signature change one epoch early.

Activation time: `ruthActivationTime = 1734772800`
(2024-12-21 09:20:00 UTC — the December 2024 solstice instant).

## The hotfix

`v8.3.3-hotfix`, commit `2e59ea5cc` ("Fix broken replay protection and
minerfund outputs (#8)"). Five insertions, one deletion across
`src/minerfund.cpp` and `src/validation.cpp`:

- Added the `IsRuthEnabled` branch before `IsJudgesEnabled` in
  `src/minerfund.cpp`.
- Moved the replay-protection default from `ruthActivationTime` to
  `firstSamuelActivationTime` in `src/validation.cpp`.

The hotfix is in the master lineage:

```
git merge-base --is-ancestor 2e59ea5cc master   # → true
```

## The two blocks at the fork point

At height 927,382 two blocks compete. Only one is on the active chain.

| Role | Hash | Coinbase txid | Miner-fund address | Confirmations |
|---|---|---|---|---|
| Invalidated | `00000000004ff64abb5f7f1dec49f11761c618ddc278016b188b43406d95ba09` | `6a6877bd851874e94dd30f0d408f35d1ef0f5c02d3e356eedeefb533760c04ce` | Judges `lotus_16PSJMaps9sQg7aBQgyY1RdHb2fZYdmWhQPbgus75` | −1 |
| Reconsidered | `00000000002cd5182ef955934c035d2659655bb590f9cc251418496178d4c8c0` | `ded5d8aaa3dc4048e9572c16a87a8157b6664d9560784842f7f47400a2920f45` | Ruth `lotus_16PSJPi88MtH34Ti3dZza4MFRF9XUVd3fKc6Ec3TV` | active |

The hotfix instructions were to `invalidateblock` the first and
`reconsiderblock` the second. `getblockhash?index=927382` returns the second
hash, confirming the chain is on the Ruth block.

## Epoch progression (sampled, verified 2026-11-06)

| Height | Date | Miner-fund epoch |
|---|---|---|
| 927,381 | 2024-12-21 | Judges (last pre-Ruth) |
| 927,382 | 2024-12-21 | Ruth |
| 950,000 | 2025-01-23 | Ruth |
| 1,018,000 | 2025-04-27 | Ruth |
| 1,080,000 | 2025-07-22 | 1st Samuel |
| 1,200,000 | 2026-01-04 | 2nd Samuel |
| 1,400,000 | 2026-10-13 | 1st Kings |

Every transition matches the epoch activation schedule.

## Verifying independently

Explorer endpoints (read-only):

```
curl 'https://explorer.lotusia.org/api/getblockhash?index=927382'
curl 'https://explorer.lotusia.org/api/getblock?hash=00000000002cd5182ef955934c035d2659655bb590f9cc251418496178d4c8c0'
curl 'https://explorer.lotusia.org/api/getrawtransaction?txid=ded5d8aaa3dc4048e9572c16a87a8157b6664d9560784842f7f47400a2920f45&decrypt=1'
```

Or, on a synced node:

```
lotus-cli getblockhash 927382
lotus-cli getblock 00000000002cd5182ef955934c035d2659655bb590f9cc251418496178d4c8c0 1
```

> Historical recovery note (do not re-run): the December 2024 recovery used
> `invalidateblock 00000000004ff64abb5f7f1dec49f11761c618ddc278016b188b43406d95ba09`
> followed by `reconsiderblock 00000000002cd5182ef955934c035d2659655bb590f9cc251418496178d4c8c0`.
> Re-running these against a current, synced node is unnecessary and would
> orphan or re-adopt blocks on that node.

## Related

- `src/consensus/addresses.h` — the epoch payout address sets (`.judges`,
  `.ruth`, …).
- `src/minerfund.cpp` — `GetMinerFundRequiredOutputs` dispatch.
- `src/validation.cpp` — `IsReplayProtectionEnabled` default.
