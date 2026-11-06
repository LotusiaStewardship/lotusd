# Winter Solstice 2026 Network Upgrade

**Version:** 12.0.0
**Activation Date:** December 21, 2026 at 20:50:00 UTC
**Epoch Name:** 2nd Kings
**Activation Timestamp:** 1797886200

## Overview

The Winter Solstice 2026 upgrade continues Lotus's biannual protocol
upgrade cadence by introducing the **2nd Kings** epoch.

This branch implements the 2nd Kings activation on mainnet, testnet,
and regtest; updates the miner fund payout rules and required addresses
for the new epoch; and prepares the next scheduled epoch by adding a
`firstChroniclesActivationTime` consensus parameter. It also defers
replay protection activation from 2nd Kings to 1st Chronicles, and
restores the strict coinbase-height commitment that an earlier coinbase
validation change had loosened.

## Key Changes

### 1. 2nd Kings Epoch Activation

**Technical Details:**

- Adds `IsSecondKingsEnabled` to `src/consensus/activation.h` /
  `activation.cpp`.
- Activation is determined by Median Time Past (MTP), consistent with
  prior Lotus upgrades.
- Mainnet activation timestamp: `1797886200`.
- Testnet activation timestamp: mainnet minus 21 days.
- Regtest activation timestamp: same as mainnet.

**Benefits:**

- Preserves the regular solstice-based upgrade schedule.
- Ensures deterministic activation behavior across nodes.
- Provides explicit network-specific activation values for testing and
  deployment.

### 2. Miner Fund Address Set Update for 2nd Kings

**Technical Details:**

- Registers the 2nd Kings miner fund address set in
  `src/consensus/addresses.h`.
- Final 2nd Kings address is:
  - `lotus_16PSJPVHNWEF4t1nSFefhth6HAtqn6oripT4k9iNv`
- The address set is finalized in the activation commit.

**Benefits:**

- Establishes the consensus-required payout destination for the 2nd
  Kings epoch.
- Ensures miners and validating nodes enforce the same coinbase output.

### 3. Miner Fund Payout Mode

**Technical Details:**

- 2nd Kings uses the **fan-out** payout mode with a single address.
- With one address, fan-out and cycling are equivalent; the coinbase is
  byte-identical.
- Implemented in `GetMinerFundRequiredOutputs()` via
  `BuildOutputsFanOut()` for `secondKings`.

**Benefits:**

- Keeps the required coinbase structure explicit for the new epoch.
- Matches the immediately preceding 1st Kings single-address payout.

### 4. 1st Chronicles Pre-Scheduling and Replay Protection Deferral

**Technical Details:**

- Adds a new consensus parameter: `firstChroniclesActivationTime`.
- Mainnet value: `1813587000` (2027-06-21 14:10:00 UTC).
- Testnet value: mainnet minus 21 days.
- Regtest value: same as mainnet.
- Extends `Consensus::CoinbaseAddresses` with a `firstChronicles` field.
- Adds `-firstchroniclesactivationtime` as a hidden command-line
  override.
- Changes replay protection's default activation point from
  `secondKingsActivationTime` to `firstChroniclesActivationTime`.

**Benefits:**

- Prepares the next biannual upgrade in the same branch that activates
  2nd Kings.
- Preserves the existing replay protection mechanism while deferring its
  default activation to the next epoch.

### 5. Coinbase Height Commitment Re-tightening

**Technical Details:**

- Restores the strict canonical coinbase check (`OP_RETURN` followed by
  the `logos` prefix and the serialized block height) that a prior
  coinbase validation change loosened.
- Gated behind the 2nd Kings MTP activation so that blocks mined under
  the loosened vanity-prefix rule before activation are not
  retroactively rejected.
- The loosened check returned `true` for any non-`logos` prefix past
  height 1,018,000, skipping the height commitment — the only mechanism
  keeping coinbase transaction IDs unique. This re-opened a
  duplicate-coinbase-txid overwrite (BIP30 / CVE-2012-1909 class).

**Benefits:**

- Closes a consensus-level coinbase uniqueness hole at the boundary.
- Restores the invariant that a coinbase's height is part of its
  transaction ID.

### 6. Functional Test Coverage for the Upgrade Boundary

**Technical Details:**

- Extends `test/functional/logos_feature_minerfund_activation.py` to
  cover the Winter Solstice 2026 transition.
- Re-derives `SECOND_KINGS_SCRIPTS` from the finalized 2nd Kings
  address.
- Adds `FIRST_CHRONICLES_ACTIVATION_TIME` and moves the replay
  protection activation constant to it.
- Verifies that pre-2nd-Kings address sets fail once 2nd Kings has
  activated.
- Verifies that 2nd Kings coinbase outputs succeed after activation.
- Confirms replay protection is still not enabled at the 2nd Kings
  boundary.

## Implementation Details

### Consensus Changes

1. **Activation Parameters**

   - `firstChroniclesActivationTime` added to `Consensus::Params`
   - Mainnet, testnet, and regtest values assigned in
     `src/chainparams.cpp`

2. **Miner Fund Addressing**

   - `Consensus::CoinbaseAddresses` now includes `secondKings` and
     `firstChronicles` fields
   - 2nd Kings uses a single-address miner fund set

3. **Miner Fund Output Selection**

   - `GetMinerFundRequiredOutputs()` dispatches to fan-out when
     `IsSecondKingsEnabled()` becomes true
   - The dispatch branch precedes `IsFirstKingsEnabled()`

4. **Coinbase Height Commitment**

   - `ContextualCheckBlock()` enforces the canonical coinbase form when
     `IsSecondKingsEnabled()` is true
   - The pre-activation path is unchanged

5. **Replay Protection Timing**

   - Default replay protection activation now references
     `params.firstChroniclesActivationTime`
   - Command-line override remains available through
     `-replayprotectionactivationtime`

### Node Configuration Updates

Hidden activation-time arguments now include:

- `-firstkingsactivationtime`
- `-secondkingsactivationtime`
- `-firstchroniclesactivationtime`

### Network Parameters

**Mainnet:**

- 2nd Kings: `1797886200`
- 1st Chronicles (pre-scheduled): `1813587000`

**Testnet:**

- 2nd Kings: mainnet minus 21 days
- 1st Chronicles: mainnet minus 21 days

**Regtest:**

- 2nd Kings: same as mainnet
- 1st Chronicles: same as mainnet

## Testing

Upgrade-specific verification in this branch includes:

- miner fund activation checks across the 2nd Kings boundary
- rejection of all prior epoch address sets after 2nd Kings activates
- acceptance of 2nd Kings-required coinbase outputs after activation
- confirmation that replay protection remains disabled at 2nd Kings
  and is deferred to 1st Chronicles by default
- native regression coverage that a non-`logos` coinbase is rejected
  after 2nd Kings activation

## Upgrade Instructions

### For Node Operators

1. Upgrade to a release containing the Winter Solstice 2026 changes.
2. Restart nodes before 2nd Kings activation.
3. If using custom activation overrides in testing environments, review
   `-secondkingsactivationtime` and `-firstchroniclesactivationtime`.

### For Miners

1. Update mining software before 2nd Kings activation.
2. Ensure coinbase construction produces the canonical
   `OP_RETURN "logos" <height>` prefix.
3. Ensure the coinbase produces the 2nd Kings miner fund output.

### For Developers

1. Update any code that assumes replay protection activates at
   2nd Kings.
2. Use `firstChroniclesActivationTime` as the default future replay
   protection boundary.
3. Verify tools that inspect miner fund outputs account for the single
   address fan-out payout in 2nd Kings.

## Version History

- **12.0.0** - Winter Solstice 2026 (2nd Kings)
- **11.4.13** - Summer Solstice 2026 (1st Kings)
- **10.4.9** - Winter Solstice 2025 (2nd Samuel)

## Technical Specifications

### 2nd Kings Miner Fund Output Logic

```cpp
if (IsSecondKingsEnabled(params, pindexPrev)) {
    return BuildOutputsFanOut(params.coinbasePayoutAddresses.secondKings,
                              pindexPrev, blockReward);
}
```

### Coinbase Height Commitment (post-activation)

```cpp
if (IsSecondKingsEnabled(params, pindexPrev)) {
    const CScript expect =
        CScript() << OP_RETURN << COINBASE_PREFIX << nHeight;
    if (block.vtx[0]->vout[0].scriptPubKey.size() < expect.size() ||
        !std::equal(expect.begin(), expect.end(),
                    block.vtx[0]->vout[0].scriptPubKey.begin())) {
        return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS,
                             "bad-cb-height",
                             "block height mismatch in coinbase");
    }
    return true;
}
```

### Replay Protection Default Activation

```cpp
static bool IsReplayProtectionEnabled(const Consensus::Params &params,
                                      int64_t nMedianTimePast) {
    return nMedianTimePast >=
           gArgs.GetArg("-replayprotectionactivationtime",
                        params.firstChroniclesActivationTime);
}
```

## Impact Summary

### Consensus

- Introduces the 2nd Kings activation boundary.
- Changes the required miner fund coinbase structure at activation.
- Restores the strict coinbase height commitment.
- Pre-schedules the next epoch's activation parameter.

### Miner Fund Enforcement

- Adds the 2nd Kings fan-out with a single required payout destination.

### Coinbase Uniqueness

- Closes the duplicate-coinbase-txid overwrite by re-enforcing the
  canonical height commitment.

### Replay Protection

- Defers the default replay protection activation point to 1st
  Chronicles.
- Leaves the manual override path intact for testing.

### Validation and Testing

- Extends functional coverage to the Winter Solstice 2026 boundary.
- Adds native regression coverage for the coinbase height gate.

## References

- `src/chainparams.cpp`
- `src/consensus/params.h`
- `src/consensus/addresses.h`
- `src/consensus/activation.h`
- `src/consensus/activation.cpp`
- `src/minerfund.cpp`
- `src/init.cpp`
- `src/validation.cpp`
- `test/functional/logos_feature_minerfund_activation.py`
- `src/test/validation_block_tests.cpp`

---

_This document describes the technical changes included in the Winter
Solstice 2026 upgrade branch and the consensus behavior they introduce._
