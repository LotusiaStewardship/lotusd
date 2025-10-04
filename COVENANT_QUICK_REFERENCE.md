# Covenant Token Quick Reference

## 🎯 Critical Implementation Points

### 1. Balance Encoding: BIG-ENDIAN! ⚠️

```typescript
// ✅ CORRECT
const balanceBytes = Buffer.alloc(8);
balanceBytes.writeBigInt64BE(balance);

// ❌ WRONG  
balanceBytes.writeBigInt64LE(balance);  // This causes wrong balance!
```

### 2. ScriptPubKey: Always Fetch from Chain! ⚠️

```typescript
// ✅ CORRECT
const utxo = await rpc.call('gettxout', [txid, vout]);
const scriptPubKey = Buffer.from(utxo.scriptPubKey.hex, 'hex');

// ❌ WRONG
const scriptPubKey = buildP2PKH(addressToHash(address));  // Empty from backend!
```

### 3. Signature Hash: Use Full Covenant Script! ⚠️

```typescript
// ✅ CORRECT (91 bytes)
const scriptCode = utxo.scriptPubKeyBytes;  // The full covenant script

// ❌ WRONG (25 bytes)
const scriptCode = buildP2PKH(ownerPkh);  // Causes signature failure!
```

## 📏 Simple Covenant Format (91 bytes)

```
Offset | Bytes | Description
-------|-------|-------------
0      | 1     | 0x20 (push 32)
1-32   | 32    | Genesis ID (txid of genesis transaction)
33     | 1     | 0x75 (OP_DROP)
34     | 1     | 0x08 (push 8)
35-42  | 8     | Balance (BIG-ENDIAN int64)
43     | 1     | 0x75 (OP_DROP)
44     | 1     | 0x14 (push 20)
45-64  | 20    | Owner public key hash
65     | 1     | 0x75 (OP_DROP)
66     | 1     | 0x76 (OP_DUP)
67     | 1     | 0xa9 (OP_HASH160)
68     | 1     | 0x14 (push 20)
69-88  | 20    | Owner public key hash (again)
89     | 1     | 0x88 (OP_EQUALVERIFY)
90     | 1     | 0xac (OP_CHECKSIG)
-------|-------|-------------
Total  | 91    | Exactly 91 bytes
```

## 🔧 Introspection Opcodes

```
Opcode              | Hex  | What it does
--------------------|------|---------------------------
OP_INPUTINDEX       | 0xc0 | Push current input index
OP_ACTIVEBYTECODE   | 0xc1 | Push our scriptPubKey
OP_TXVERSION        | 0xc2 | Push tx version
OP_TXINPUTCOUNT     | 0xc3 | Push number of inputs
OP_TXOUTPUTCOUNT    | 0xc4 | Push number of outputs
OP_TXLOCKTIME       | 0xc5 | Push tx locktime
OP_UTXOVALUE        | 0xc6 | Push our input value (sats)
OP_OUTPUTVALUE      | 0xc7 | <idx> → <value>
OP_OUTPUTBYTECODE   | 0xc8 | <idx> → <scriptPubKey>
```

## 📊 RPC Commands

```bash
# Get token info (use this for detection!)
gettokeninfo <txid> <vout>
→ {valid, genesisid, balance, owner, ownerpubkeyhash}

# Scan transaction for tokens
scantokens <txid>
→ [{vout, genesisid, balance, owner}, ...]

# List tokens by address (needs UTXO iteration)
listtokensbyaddress <address>
→ [{txid, vout, genesisid, balance}, ...]

# Get genesis data (placeholder)
gettokengenesis <genesisid>
→ {genesisid, txid, vout, ...}
```

## 🎯 Token Detection (CORRECT METHOD)

```typescript
// ✅ Use RPC - Let the node do the work!
async function findCovenantTokens(address: string): Promise<Token[]> {
  const utxos = await rpc.call('getaddressutxos', [{addresses: [address]}]);
  const tokens = [];
  
  for (const utxo of utxos) {
    const info = await rpc.call('gettokeninfo', [utxo.txid, utxo.vout]);
    if (info.valid) {
      tokens.push({
        genesisId: info.genesisid,
        balance: BigInt(info.balance),  // Already parsed correctly!
        owner: info.owner,
        txid: utxo.txid,
        vout: utxo.vout
      });
    }
  }
  
  return tokens;
}
```

## 💰 Balance Conservation Rule

```typescript
// For each unique genesis ID:
Σ(input balances) == Σ(output balances)

// Example:
Input:  1000 tokens (genesis: abc123)
Output: 600 tokens (genesis: abc123) ← to recipient
        400 tokens (genesis: abc123) ← to sender (change)
Total:  600 + 400 = 1000 ✅

// Enforced by consensus after block 1134000
```

## 🔍 Debug Your Implementation

```typescript
// Log everything when building covenant:
console.log("Genesis ID (hex):", genesisId.toString('hex'));
console.log("Genesis ID (length):", genesisId.length);  // Must be 32

const balanceBytes = Buffer.alloc(8);
balanceBytes.writeBigInt64BE(balance);
console.log("Balance (value):", balance.toString());
console.log("Balance (hex BE):", balanceBytes.toString('hex'));
console.log("Balance (bytes):", Array.from(balanceBytes));

console.log("Owner PKH (hex):", ownerPkh.toString('hex'));
console.log("Owner PKH (length):", ownerPkh.length);  // Must be 20

const script = buildSimpleCovenant({genesisId, balance, ownerPkh});
console.log("Covenant script (length):", script.length);  // Must be 91
console.log("Covenant script (hex):", script.toString('hex'));

// Verify pattern
console.assert(script[0] === 0x20, "Byte 0 must be 0x20");
console.assert(script[33] === 0x75, "Byte 33 must be 0x75 (OP_DROP)");
console.assert(script[34] === 0x08, "Byte 34 must be 0x08");
console.assert(script[90] === 0xac, "Byte 90 must be 0xac (OP_CHECKSIG)");
```

## ⚡ Performance Tips

```typescript
// Cache token info to avoid repeated RPC calls
const tokenCache = new Map<string, TokenInfo>();

async function getTokenInfo(txid: string, vout: number): Promise<TokenInfo> {
  const key = `${txid}:${vout}`;
  
  if (tokenCache.has(key)) {
    return tokenCache.get(key)!;
  }
  
  const info = await rpc.call('gettokeninfo', [txid, vout]);
  if (info.valid) {
    tokenCache.set(key, info);
  }
  
  return info;
}
```

## 🎊 Success Indicators

Your implementation is correct when:

✅ `gettokeninfo` returns `valid: true`  
✅ `scantokens` finds your covenant outputs  
✅ Balance displays as expected (not garbled number)  
✅ `decoderawtransaction` shows `"type": "covenant_token"`  
✅ Transaction accepted (not rejected with signature error)  
✅ Tokens appear in wallet after creation  
✅ Transfers work without errors  

## 🐛 If Still Getting Errors

### "scriptpubkey" error
→ Node not updated yet (unlikely now)

### "mandatory-script-verify-flag-failed (Signature...)"  
→ **scriptCode is wrong!** Use full covenant script, not P2PKH

### Balance shows wrong number (e.g., 45161676009963520 instead of 10000000000000)
→ **Using little-endian instead of big-endian!**

### Tokens not detected after creation
→ **Use gettokeninfo RPC**, don't manually parse scriptPubKey

---

## 📞 Support

If issues persist:

1. Test transaction with: `testmempoolaccept`
2. Decode with: `decoderawtransaction` 
3. Check covenant type shows as: `"covenant_token"`
4. Verify script size is: exactly 91 bytes
5. Check balance bytes (35-42) are: big-endian encoded

**The node is ready and working. Just fix the three critical bugs above!** 🚀


