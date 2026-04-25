# Summer Solstice 2026 Network Upgrade

**Version:** 11.4.13  
**Activation Date:** June 21, 2026 at 08:24:00 UTC  
**Epoch Name:** 1st Kings  
**Commit Range:** 2b31a3b32..9f4fd1698

## Overview

The Summer Solstice 2026 upgrade continues Lotus's biannual protocol
upgrade cadence by introducing the **1st Kings** epoch.

This branch implements the 1st Kings activation on mainnet, testnet,
and regtest; updates the miner fund payout rules and required addresses
for the new epoch; and prepares the next scheduled epoch by adding a
`secondKingsActivationTime` consensus parameter. It also defers replay
protection activation from 1st Kings to 2nd Kings and extends the
functional miner fund test coverage to verify the Summer 2026 boundary.

## Key Changes

### 1. 1st Kings Epoch Activation

**Technical Details:**

- Adds `firstKingsActivationTime` consensus support across all chains.
- Mainnet activation timestamp: `1782030240`.
- Testnet activation timestamp: mainnet minus 21 days.
- Regtest activation timestamp: same as mainnet.
- Activation is determined by Median Time Past (MTP), consistent with
  prior Lotus upgrades.

**Benefits:**

- Preserves the regular solstice-based upgrade schedule.
- Ensures deterministic activation behavior across nodes.
- Provides explicit network-specific activation values for testing and
  deployment.

### 2. Miner Fund Address Set Update for 1st Kings

**Technical Details:**

- Registers the 1st Kings miner fund address set in
  `src/consensus/addresses.h`.
- Final 1st Kings addresses are:
  - `lotus_16PSJQXrnTrUbhnxPpPgf1jd16JFkWk8TdbpALVME`
  - `lotus_16PSJMAVPBd6g7rYz9d3fw8BbENqNaPdU6wSi7eC8`
- The second branch commit in the upgrade range completes this address
  set by adding the `hash turtle` address.

**Benefits:**

- Establishes the consensus-required payout destinations for the First
  Kings epoch.
- Ensures miners and validating nodes enforce the same coinbase outputs.
- Completes the address set before activation.

### 3. Miner Fund Payout Mode Change

**Technical Details:**

- Changes 1st Kings miner fund payouts from the prior **cycling** mode
  back to **fan-out** mode.
- Under Second Samuel, the miner fund cycled a single 50 percent payout
  output across the epoch's addresses.
- Under 1st Kings, the miner fund requires outputs to **both** First
  Kings addresses each block.
- With two 1st Kings addresses, the required miner fund share is split
  equally across those outputs, producing two outputs of 25 percent of
  the total block reward each.
- This behavior is implemented in `GetMinerFundRequiredOutputs()` by
  using `BuildOutputsFanOut()` for `firstKings`.

**Benefits:**

- Restores deterministic per-block distribution to the full 1st Kings
  address set.
- Simplifies miner fund verification at the 1st Kings boundary.
- Makes the required coinbase structure explicit for the new epoch.

### 4. 2nd Kings Pre-Scheduling and Replay Protection Deferral

**Technical Details:**

- Adds a new consensus parameter: `secondKingsActivationTime`.
- Mainnet value: `1798074180`.
- Testnet value: mainnet minus 21 days.
- Regtest value: same as mainnet.
- Extends `Consensus::CoinbaseAddresses` with a `secondKings` field for
  future epoch support.
- Adds `-secondkingsactivationtime` as a hidden command-line override.
- Changes replay protection's default activation point from
  `firstKingsActivationTime` to `secondKingsActivationTime`.
- `validation.cpp` now enables replay protection by default only once
  MTP reaches `secondKingsActivationTime`, unless overridden by
  `-replayprotectionactivationtime`.

**Benefits:**

- Prepares the next biannual upgrade in the same branch that activates
  1st Kings.
- Preserves the existing replay protection mechanism while delaying its
  default activation to the next epoch.
- Keeps node behavior configurable for testing while maintaining a
  consensus-defined default.

### 5. Functional Test Coverage for the Upgrade Boundary

**Technical Details:**

- Extends `test/functional/logos_feature_minerfund_activation.py` to
  cover the Summer Solstice 2026 transition.
- Adds `SECOND_KINGS_ACTIVATION_TIME` to the functional test scaffold.
- Moves the test's replay protection activation constant to
  `SECOND_KINGS_ACTIVATION_TIME`.
- Verifies that pre-1st-Kings address sets fail once 1st Kings has
  activated.
- Verifies that 1st Kings coinbase outputs succeed after activation.
- Confirms replay protection is still not enabled at the 1st Kings
  boundary.

**Benefits:**

- Validates miner fund consensus behavior before and after activation.
- Confirms the replay protection deferral is enforced by node behavior.
- Reduces regression risk around solstice boundary logic.

## Implementation Details

### Consensus Changes

1. **Activation Parameters**

   - `firstKingsActivationTime` added to `Consensus::Params`
   - `secondKingsActivationTime` added to `Consensus::Params`
   - Mainnet, testnet, and regtest values assigned in
     `src/chainparams.cpp`

2. **Miner Fund Addressing**

   - `Consensus::CoinbaseAddresses` now includes `firstKings` and
     `secondKings` fields
   - 1st Kings uses a two-address miner fund set
   - 1st Kings address set is finalized by the addition of the
     `hash turtle` address in the tip commit of the branch

3. **Miner Fund Output Selection**

   - `GetMinerFundRequiredOutputs()` switches to fan-out outputs when
     `IsFirstKingsEnabled()` becomes true
   - Previous `secondSamuel` logic remains cycling-based until the First
     Kings activation point

4. **Replay Protection Timing**

   - Default replay protection activation now references
     `params.secondKingsActivationTime`
   - Command-line override remains available through
     `-replayprotectionactivationtime`

### Node Configuration Updates

Hidden activation-time arguments now include:

- `-firstkingsactivationtime`
- `-secondkingsactivationtime`

This keeps local testing and controlled activation simulations aligned
with earlier upgrade patterns.

### Network Parameters

**Mainnet:**

- 1st Kings: `1782030240`
- 2nd Kings (pre-scheduled): `1798074180`

**Testnet:**

- 1st Kings: mainnet minus 21 days
- 2nd Kings: mainnet minus 21 days

**Regtest:**

- 1st Kings: same as mainnet
- 2nd Kings: same as mainnet

## Testing

Upgrade-specific verification in this branch includes:

- miner fund activation checks across the 1st Kings boundary
- rejection of all prior epoch address sets after 1st Kings activates
- acceptance of 1st Kings-required coinbase outputs after activation
- confirmation that replay protection remains disabled at 1st Kings
  and is deferred to 2nd Kings by default

## Upgrade Instructions

### For Node Operators

1. Upgrade to a release containing the Summer Solstice 2026 changes.
2. Restart nodes before 1st Kings activation.
3. If using custom activation overrides in testing environments, review
   both `-firstkingsactivationtime` and `-secondkingsactivationtime`.

### For Miners

1. Update mining software before 1st Kings activation.
2. Ensure coinbase construction produces the 1st Kings miner fund
   outputs.
3. Do not continue using the Second Samuel cycling output pattern after
   1st Kings activates.

### For Developers

1. Update any code that assumes replay protection activates at
   1st Kings.
2. Use `secondKingsActivationTime` as the default future replay
   protection boundary.
3. Verify tests and tools that inspect miner fund outputs account for the
   fan-out payout mode in 1st Kings.

## Version History

- **11.4.13** - Summer Solstice 2026 (1st Kings)
- **10.4.9** - Winter Solstice 2025 (2nd Samuel)
- **8.3.x** - Winter Solstice 2024 (Ruth)

## Technical Specifications

### 1st Kings Miner Fund Output Logic

```cpp
if (IsFirstKingsEnabled(params, pindexPrev)) {
    return BuildOutputsFanOut(params.coinbasePayoutAddresses.firstKings,
                              pindexPrev, blockReward);
}
```

### Replay Protection Default Activation

```cpp
static bool IsReplayProtectionEnabled(const Consensus::Params &params,
                                      int64_t nMedianTimePast) {
    return nMedianTimePast >=
           gArgs.GetArg("-replayprotectionactivationtime",
                        params.secondKingsActivationTime);
}
```

## Impact Summary

### Consensus

- Introduces the 1st Kings activation boundary.
- Changes the required miner fund coinbase structure at activation.
- Pre-schedules the next epoch's activation parameter.

### Miner Fund Enforcement

- Replaces Second Samuel's cycling behavior with 1st Kings fan-out.
- Finalizes the 1st Kings address set with two required payout
  destinations.

### Replay Protection

- Defers the default replay protection activation point to
  2nd Kings.
- Leaves the manual override path intact for testing.

### Validation and Testing

- Extends functional coverage to the Summer Solstice 2026 boundary.
- Verifies both acceptance and rejection cases around activation.

## References

- `src/chainparams.cpp`
- `src/consensus/params.h`
- `src/consensus/addresses.h`
- `src/minerfund.cpp`
- `src/init.cpp`
- `src/validation.cpp`
- `test/functional/logos_feature_minerfund_activation.py`

---

_This document describes the technical changes included in the Summer
Solstice 2026 upgrade branch and the consensus behavior they introduce._
