# Winter Solstice 2025 Network Upgrade

**Version:** 10.4.9  
**Activation Date:** December 21, 2025 at 15:03:00 UTC  
**Epoch Name:** 2nd Samuel  
**Commit Range:** 73d3390bb..4c2f97e77

## Overview

The Winter Solstice 2025 upgrade marks the continuation of Lotus's biannual protocol upgrade cycle, introducing the **2nd Samuel** epoch. This upgrade maintains network consensus, updates the miner fund distribution addresses, and implements critical improvements to the protocol's signature and scripting capabilities.

## Key Changes

### 1. 2nd Samuel Epoch Activation

**Technical Details:**

- Activation timestamp: `1766329380` (December 21, 2025, 15:03:00 UTC)
- New miner fund addresses:
  - `lotus_16PSJMYJL6FxpRh9nP8iFZPiGhLM8p5S9L5dVXUcJ`
  - `lotus_16PSJHGmfZkU8zFrzU8Gw198o4j2XUryNnrMccuvZ`
- Implements cycling payout mechanism introduced in the Numbers epoch

**Benefits:**

- Maintains transparent and predictable miner fund distribution
- Ensures continuous network development funding
- Provides smooth transition from 1st Samuel epoch

### 2. 1st Kings Epoch Pre-Implementation

**Technical Details:**

- Pre-activation timestamp: `1782030240` (June 21, 2026, 08:24:00 UTC)
- Implements forward-looking replay protection mechanism

**Benefits:**

- Ensures time-based replay protection window for future upgrades
- Prevents transaction replay attacks across upgrade boundaries
- Maintains Lotus's security model with automatic protection

### 3. SIGHASH_LOTUS and Taproot Re-activation

**Technical Details:**

- Removes `SCRIPT_DISABLE_TAPROOT_SIGHASH_LOTUS` flag that was set during Numbers epoch
- Removes mempool rejection of Taproot transactions
- Removes block validation rejection of Taproot transactions
- Taproot and SIGHASH_LOTUS are enabled by default once disabling code is removed

**Benefits:**

- **Enhanced Privacy:** Taproot enables more private and efficient smart contracts
- **Improved Flexibility:** SIGHASH_LOTUS provides advanced signature hash types for complex transactions
- **Better Scalability:** Taproot reduces transaction sizes and improves validation performance
- **Smart Contract Capabilities:** Enables sophisticated covenant and token mechanisms

## Implementation Details

### Consensus Changes

1. **Activation Functions**

   - `IsSecondSamuelEnabled()` - Checks 2nd Samuel activation status
   - `IsFirstKingsEnabled()` - Checks 1st Kings pre-activation status
   - Both use Median Time Past (MTP) for deterministic activation

2. **Miner Fund Updates**

   - Cycling distribution continues from previous epochs
   - Single address per block (50% of block reward)
   - Enforced via consensus rules in coinbase validation

3. **Replay Protection**
   - Replay protection window set to 1st Kings activation time
   - Provides 6-month buffer between upgrades
   - Uses `SCRIPT_ENABLE_REPLAY_PROTECTION` flag

### Policy Updates

- Removed Taproot disable flag (`SCRIPT_DISABLE_TAPROOT_SIGHASH_LOTUS`)
- Removed mempool rejection of Taproot transactions
- Removed block validation rejection of Taproot transactions
- `STANDARD_SCRIPT_VERIFY_FLAGS` no longer includes the disable flag

### Network Parameters

**Mainnet:**

- 2nd Samuel: `1766329380` (Dec 21, 2025)
- 1st Kings: `1782030240` (Jun 21, 2026)

**Testnet:**

- Activations offset by 21 days earlier than mainnet

**Regtest:**

- Same activation times as mainnet for consistency

## Testing

Comprehensive test coverage includes:

- Full miner fund activation test suite
- Address validation and cycling mechanism tests
- Replay protection verification
- Backward compatibility with all previous epochs

## Upgrade Instructions

### For Node Operators

1. Update to lotusd version 10.4.2
2. Restart your node before December 21, 2025
3. No additional configuration required

### For Miners

1. Update mining software to support new addresses
2. Ensure compatibility with cycling payout mechanism
3. Verify miner fund validation logic

### For Developers

1. Update consensus parameter references
2. Test transaction creation with new signature flags
3. Verify Taproot and SIGHASH_LOTUS functionality

## Version History

- **10.4.2** - Winter Solstice 2025 (2nd Samuel + Taproot)
- **9.4.2** - Summer Solstice 2025 (1st Samuel)
- **8.3.x** - Winter Solstice 2024 (Ruth)

## Technical Specifications

### Activation Time Calculation

```cpp
bool IsSecondSamuelEnabled(const Consensus::Params &params,
                           const CBlockIndex *pindexPrev) {
    if (pindexPrev == nullptr) {
        return false;
    }
    return pindexPrev->GetMedianTimePast() >=
           gArgs.GetArg("-secondsamuelactivationtime",
                        params.secondSamuelActivationTime);
}
```

### Taproot/SIGHASH_LOTUS Re-activation

```cpp
// Numbers epoch (2022) - Disabled Taproot and SIGHASH_LOTUS
if (IsNumbersEnabled(params, pindex)) {
    flags |= SCRIPT_DISABLE_TAPROOT_SIGHASH_LOTUS;  // ❌ Disabled
}

// 2nd Samuel epoch (2025) - The above code is removed
// Taproot and SIGHASH_LOTUS are now enabled by default ✅
// No disable flag is set, so these features work normally
```

**What was removed:**

1. **From `GetNextBlockScriptFlags()`:**

   ```cpp
   // REMOVED in 2nd Samuel:
   if (IsNumbersEnabled(params, pindex)) {
       flags |= SCRIPT_DISABLE_TAPROOT_SIGHASH_LOTUS;
   }
   ```

2. **From `MemPoolAccept::PreChecks()`:**

   ```cpp
   // REMOVED in 2nd Samuel:
   if (fRequireStandardPolicy && TxHasPayToTaproot(tx)) {
       return state.Invalid(TxValidationResult::TX_NOT_STANDARD,
                            "bad-taproot-phased-out");
   }
   ```

3. **From `ConnectBlock()`:**
   ```cpp
   // REMOVED in 2nd Samuel:
   if (IsNumbersEnabled(consensusParams, pindex->pprev)) {
       if (TxHasPayToTaproot(tx)) {
           return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS,
                                "bad-taproot-phased-out");
       }
   }
   ```

## Impact Summary

### Security

- ✅ Replay protection maintained through 1st Kings pre-activation
- ✅ Taproot privacy and security features enabled
- ✅ Enhanced signature flexibility with SIGHASH_LOTUS

### Scalability

- ✅ Improved transaction efficiency via Taproot
- ✅ Reduced block space usage for complex scripts
- ✅ Better UTXO set management

### Developer Experience

- ✅ Advanced covenant capabilities
- ✅ Flexible signature construction
- ✅ Enhanced smart contract primitives

### Network Health

- ✅ Continued miner fund support
- ✅ Smooth upgrade path maintained
- ✅ Backward compatibility preserved

## References

- [Taproot BIP 341](https://github.com/bitcoin/bips/blob/master/bip-0341.mediawiki)
- [Taproot BIP 342](https://github.com/bitcoin/bips/blob/master/bip-0342.mediawiki)
- [Lotus Upgrade History](https://lotusia.org/upgrades)

## Contacts

- **Development Team:** development@lotusia.org
- **GitHub:** https://github.com/LotusiaStewardship/lotusd
- **Website:** https://lotusia.org

---

_This upgrade represents the continued evolution of the Lotus protocol, maintaining its commitment to regular, predictable network improvements while preserving security, decentralization, and compatibility._
