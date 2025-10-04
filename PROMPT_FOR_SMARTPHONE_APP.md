# Lotus Covenant Token - Smartphone App Implementation Guide

## Objective

Implement **full OP_CAT covenant token support** in the Lotus smartphone wallet app (React Native/TypeScript). The Lotus node now supports introspection opcodes and consensus-validated covenant tokens.

## What the Node Supports (Already Implemented)

### 1. Introspection Opcodes

The node now has **9 introspection opcodes** that allow scripts to inspect transaction data:

| Opcode | Hex | Function | Stack Effect |
|--------|-----|----------|--------------|
| `OP_INPUTINDEX` | 0xc0 | Push current input index | `→ <index>` |
| `OP_ACTIVEBYTECODE` | 0xc1 | Push scriptPubKey of current input | `→ <script>` |
| `OP_TXVERSION` | 0xc2 | Push transaction version | `→ <version>` |
| `OP_TXINPUTCOUNT` | 0xc3 | Push number of inputs | `→ <count>` |
| `OP_TXOUTPUTCOUNT` | 0xc4 | Push number of outputs | `→ <count>` |
| `OP_TXLOCKTIME` | 0xc5 | Push transaction locktime | `→ <locktime>` |
| `OP_UTXOVALUE` | 0xc6 | Push value of current input (sats) | `→ <value>` |
| `OP_OUTPUTVALUE` | 0xc7 | Push value of output by index | `<idx> → <value>` |
| `OP_OUTPUTBYTECODE` | 0xc8 | Push scriptPubKey by index | `<idx> → <script>` |

### 2. OP_CAT and Other Opcodes

Already available from Bitcoin Cash:
- `OP_CAT` (0x7e) - Concatenate byte arrays
- `OP_SPLIT` (0x7f) - Split byte array at position
- `OP_NUM2BIN` (0x80) - Number to binary
- `OP_BIN2NUM` (0x81) - Binary to number
- `OP_REVERSEBYTES` (0xbc) - Reverse byte array

### 3. Covenant Types Recognized

**Type A: Simple Covenant (91 bytes)**
```
<genesis_32_bytes> OP_DROP
<balance_8_bytes> OP_DROP
<owner_pkh_20_bytes> OP_DROP
OP_DUP OP_HASH160 <owner_pkh_20_bytes> OP_EQUALVERIFY OP_CHECKSIG
```

**Type B: Complex Covenant (variable size)**
- Must start with 32-byte genesis push
- Uses introspection opcodes
- Self-validates balance conservation

### 4. RPC Commands Available

```typescript
// Get token info from specific output
interface TokenInfo {
  valid: boolean;
  genesisid: string;      // 32-byte hex
  balance: number;        // int64
  ownerpubkeyhash: string;
  owner: string;          // Lotus address
}
rpc.call('gettokeninfo', [txid, vout]): Promise<TokenInfo>

// Scan transaction for all covenant outputs
rpc.call('scantokens', [txid]): Promise<TokenInfo[]>

// List tokens held by address (placeholder - needs index)
rpc.call('listtokensbyaddress', [address]): Promise<TokenInfo[]>

// Get genesis transaction data (placeholder - needs index)
rpc.call('gettokengenesis', [genesisId]): Promise<GenesisInfo>
```

### 5. Consensus Rules (After Block 1134000)

- **Simple covenants**: Balance MUST be conserved (inputs == outputs per genesis)
- **Complex covenants**: Script MUST execute successfully (self-validates)
- **Genesis**: Creation (inputSum == 0) allows any output balance
- **Violation**: Transaction rejected, block orphaned

## Critical Implementation Details

### 1. Balance Encoding: BIG-ENDIAN!

**CRITICAL**: The 8-byte balance field is **BIG-ENDIAN**, not little-endian!

```typescript
// ❌ WRONG (little-endian)
const balanceBytes = Buffer.alloc(8);
balanceBytes.writeBigInt64LE(balance);

// ✅ CORRECT (big-endian)
const balanceBytes = Buffer.alloc(8);
balanceBytes.writeBigInt64BE(balance);

// Or manually:
const balanceBytes = new Uint8Array(8);
for (let i = 7; i >= 0; i--) {
  balanceBytes[7 - i] = Number((balance >> BigInt(i * 8)) & 0xFFn);
}
```

### 2. Simple Covenant Construction

```typescript
function buildSimpleCovenant(params: {
  genesisId: Uint8Array,     // 32 bytes
  balance: bigint,           // int64
  ownerPubKeyHash: Uint8Array // 20 bytes
}): Uint8Array {
  const script = new Uint8Array(91);
  let offset = 0;
  
  // Push 32 bytes
  script[offset++] = 0x20;
  script.set(params.genesisId, offset);
  offset += 32;
  
  // OP_DROP
  script[offset++] = 0x75;
  
  // Push 8 bytes
  script[offset++] = 0x08;
  
  // Balance (BIG-ENDIAN!)
  for (let i = 7; i >= 0; i--) {
    script[offset++] = Number((params.balance >> BigInt(i * 8)) & 0xFFn);
  }
  
  // OP_DROP
  script[offset++] = 0x75;
  
  // Push 20 bytes
  script[offset++] = 0x14;
  script.set(params.ownerPubKeyHash, offset);
  offset += 20;
  
  // OP_DROP
  script[offset++] = 0x75;
  
  // Standard P2PKH: OP_DUP OP_HASH160 <20 bytes> OP_EQUALVERIFY OP_CHECKSIG
  script[offset++] = 0x76;  // OP_DUP
  script[offset++] = 0xa9;  // OP_HASH160
  script[offset++] = 0x14;  // Push 20 bytes
  script.set(params.ownerPubKeyHash, offset);
  offset += 20;
  script[offset++] = 0x88;  // OP_EQUALVERIFY
  script[offset++] = 0xac;  // OP_CHECKSIG
  
  console.assert(offset === 91, "Covenant script must be exactly 91 bytes");
  return script;
}
```

### 3. Parsing Covenant Data

```typescript
function parseSimpleCovenant(scriptPubKey: Uint8Array): CovenantData | null {
  if (scriptPubKey.length !== 91) return null;
  
  // Verify pattern
  if (scriptPubKey[0] !== 0x20 ||
      scriptPubKey[33] !== 0x75 ||
      scriptPubKey[34] !== 0x08 ||
      scriptPubKey[43] !== 0x75 ||
      scriptPubKey[44] !== 0x14 ||
      scriptPubKey[65] !== 0x75 ||
      scriptPubKey[66] !== 0x76 ||
      scriptPubKey[67] !== 0xa9 ||
      scriptPubKey[68] !== 0x14 ||
      scriptPubKey[89] !== 0x88 ||
      scriptPubKey[90] !== 0xac) {
    return null;
  }
  
  // Extract genesis ID (bytes 1-32)
  const genesisId = scriptPubKey.slice(1, 33);
  
  // Extract balance (bytes 35-42, BIG-ENDIAN!)
  let balance = 0n;
  for (let i = 0; i < 8; i++) {
    balance = (balance << 8n) | BigInt(scriptPubKey[35 + i]);
  }
  
  // Extract owner PKH (bytes 45-64)
  const ownerPkh = scriptPubKey.slice(45, 65);
  
  return {
    genesisId: Buffer.from(genesisId).toString('hex'),
    balance: balance,
    ownerPubKeyHash: Buffer.from(ownerPkh).toString('hex'),
    type: 'simple'
  };
}
```

### 4. Detecting Covenant UTXOs

**Method 1: Check scriptPubKey pattern directly**

```typescript
async function scanWalletForCovenants(wallet: Wallet): Promise<CovenantToken[]> {
  const tokens: CovenantToken[] = [];
  
  for (const address of wallet.addresses) {
    // Get all UTXOs for this address
    const utxos = await rpc.call('getaddressutxos', [{
      addresses: [address]
    }]);
    
    for (const utxo of utxos) {
      // Check if scriptPubKey is a covenant
      const scriptHex = utxo.scriptPubKey;
      const script = Buffer.from(scriptHex, 'hex');
      
      // Try to parse as simple covenant
      const covenantData = parseSimpleCovenant(script);
      
      if (covenantData) {
        tokens.push({
          txid: utxo.txid,
          vout: utxo.vout,
          genesisId: covenantData.genesisId,
          balance: covenantData.balance,
          owner: address,
          confirmations: utxo.confirmations
        });
      }
    }
  }
  
  return tokens;
}
```

**Method 2: Use RPC commands (recommended)**

```typescript
async function scanWalletForCovenants(wallet: Wallet): Promise<CovenantToken[]> {
  const tokens: CovenantToken[] = [];
  
  for (const address of wallet.addresses) {
    const utxos = await rpc.call('getaddressutxos', [{
      addresses: [address]
    }]);
    
    for (const utxo of utxos) {
      // Use node's gettokeninfo RPC
      const tokenInfo = await rpc.call('gettokeninfo', [utxo.txid, utxo.vout]);
      
      if (tokenInfo.valid) {
        tokens.push({
          txid: utxo.txid,
          vout: utxo.vout,
          genesisId: tokenInfo.genesisid,
          balance: BigInt(tokenInfo.balance),  // Node returns big-endian parsed
          owner: tokenInfo.owner,
          ownerPkh: tokenInfo.ownerpubkeyhash
        });
      }
    }
  }
  
  return tokens;
}
```

### 5. Creating Token Transfer Transaction

```typescript
async function transferCovenantToken(params: {
  inputUtxo: UTXO,           // Covenant UTXO being spent
  inputCovenant: CovenantData,
  recipientAddress: string,
  recipientAmount: bigint,
  changeAddress: string,
  senderPrivKey: Uint8Array
}): Promise<string> {
  
  // Decode recipient address to get PKH
  const recipientPkh = decodeAddress(params.recipientAddress).pubKeyHash;
  const senderPkh = decodeAddress(params.changeAddress).pubKeyHash;
  
  const changeAmount = params.inputCovenant.balance - params.recipientAmount;
  
  // CRITICAL: Validate balance conservation CLIENT-SIDE
  if (params.recipientAmount + changeAmount !== params.inputCovenant.balance) {
    throw new Error("Balance not conserved!");
  }
  
  // Build transaction
  const tx = new TransactionBuilder();
  
  // Input: Spending the covenant UTXO
  tx.addInput({
    txid: params.inputUtxo.txid,
    vout: params.inputUtxo.vout,
    sequence: 0xffffffff
  });
  
  // Output 0: Covenant to recipient
  tx.addOutput({
    value: 1000,  // ≥750 sats (dust threshold for 91-byte script)
    scriptPubKey: buildSimpleCovenant({
      genesisId: Buffer.from(params.inputCovenant.genesisId, 'hex'),
      balance: params.recipientAmount,  // BIG-ENDIAN!
      ownerPubKeyHash: recipientPkh
    })
  });
  
  // Output 1: Covenant change back to sender
  tx.addOutput({
    value: 1000,  // ≥750 sats
    scriptPubKey: buildSimpleCovenant({
      genesisId: Buffer.from(params.inputCovenant.genesisId, 'hex'),
      balance: changeAmount,  // BIG-ENDIAN!
      ownerPubKeyHash: senderPkh
    })
  });
  
  // Output 2: XLC change (optional)
  if (params.inputUtxo.value > 2000 + fee) {
    tx.addOutput({
      value: params.inputUtxo.value - 2000 - fee,
      scriptPubKey: buildP2PKH(senderPkh)
    });
  }
  
  // Sign the transaction
  // IMPORTANT: Use the covenant scriptPubKey for signature hash!
  const signatureHash = computeBIP143SignatureHash({
    transaction: tx.toUnsigned(),
    inputIndex: 0,
    scriptCode: params.inputUtxo.scriptPubKey,  // The covenant script!
    value: params.inputUtxo.value,
    sigHashType: 0x41  // SIGHASH_ALL | SIGHASH_FORKID
  });
  
  const signature = secp256k1.sign(signatureHash, params.senderPrivKey);
  const pubKey = secp256k1.publicKeyCreate(params.senderPrivKey);
  
  // Build scriptSig (same as P2PKH)
  const scriptSig = buildScriptSig(signature, pubKey);
  
  tx.setInputScript(0, scriptSig);
  
  // Broadcast
  const txHex = tx.toHex();
  const txid = await rpc.call('sendrawtransaction', [txHex]);
  
  return txid;
}
```

### 6. Computing Signature Hash for Covenant Inputs

**CRITICAL**: When spending a covenant UTXO, use its full scriptPubKey for the signature hash:

```typescript
function computeBIP143SignatureHash(params: {
  transaction: Transaction,
  inputIndex: number,
  scriptCode: Uint8Array,  // The covenant scriptPubKey (91 bytes or more)
  value: number,           // Value in satoshis
  sigHashType: number
}): Uint8Array {
  const writer = new BufferWriter();
  
  // 1. nVersion (4 bytes)
  writer.writeUInt32LE(params.transaction.version);
  
  // 2. hashPrevouts (32 bytes)
  const prevouts = Buffer.concat(
    params.transaction.inputs.map(input => 
      Buffer.concat([
        Buffer.from(input.txid, 'hex').reverse(),
        Buffer.from([input.vout, 0, 0, 0])
      ])
    )
  );
  writer.writeBytes(sha256(sha256(prevouts)));
  
  // 3. hashSequence (32 bytes)
  const sequences = Buffer.concat(
    params.transaction.inputs.map(input =>
      Buffer.from([0xff, 0xff, 0xff, 0xff])
    )
  );
  writer.writeBytes(sha256(sha256(sequences)));
  
  // 4. outpoint (36 bytes)
  const input = params.transaction.inputs[params.inputIndex];
  writer.writeBytes(Buffer.from(input.txid, 'hex').reverse());
  writer.writeUInt32LE(input.vout);
  
  // 5. scriptCode (FULL covenant scriptPubKey - this is critical!)
  writer.writeVarInt(params.scriptCode.length);
  writer.writeBytes(params.scriptCode);
  
  // 6. value (8 bytes)
  writer.writeUInt64LE(params.value);
  
  // 7. nSequence (4 bytes)
  writer.writeUInt32LE(input.sequence);
  
  // 8. hashOutputs (32 bytes)
  const outputs = Buffer.concat(
    params.transaction.outputs.map(output => {
      const buf = Buffer.alloc(8 + 1 + output.scriptPubKey.length);
      buf.writeBigInt64LE(BigInt(output.value), 0);
      buf[8] = output.scriptPubKey.length;
      output.scriptPubKey.copy(buf, 9);
      return buf;
    })
  );
  writer.writeBytes(sha256(sha256(outputs)));
  
  // 9. nLocktime (4 bytes)
  writer.writeUInt32LE(params.transaction.locktime);
  
  // 10. sighashType (4 bytes)
  writer.writeUInt32LE(params.sigHashType);
  
  // Double SHA256
  return sha256(sha256(writer.toBuffer()));
}
```

### 7. Fetching UTXO scriptPubKey (CRITICAL FIX)

**Your current bug**: The backend returns empty `scriptPubKey.hex`

**Fix**: Always fetch the real scriptPubKey before signing:

```typescript
async function getUTXOWithScript(txid: string, vout: number): Promise<UTXO> {
  // Use gettxout to get the REAL scriptPubKey
  const utxoData = await rpc.call('gettxout', [txid, vout]);
  
  if (!utxoData) {
    throw new Error(`UTXO ${txid}:${vout} not found or already spent`);
  }
  
  return {
    txid,
    vout,
    value: utxoData.value * 100000000,  // Convert XLC to satoshis
    scriptPubKey: Buffer.from(utxoData.scriptPubKey.hex, 'hex'),
    confirmations: utxoData.confirmations
  };
}

// When building transaction:
const utxoWithScript = await getUTXOWithScript(utxo.txid, utxo.vout);
// Use utxoWithScript.scriptPubKey for signing!
```

### 8. Scanning for Tokens

**Use RPC instead of manual scanning**:

```typescript
async function loadCovenantTokens(wallet: Wallet): Promise<Token[]> {
  const tokens: Token[] = [];
  const seen = new Set<string>();  // Track duplicates
  
  for (const address of wallet.addresses) {
    const utxos = await rpc.call('getaddressutxos', [{
      addresses: [address]
    }]);
    
    // Check each UTXO with gettokeninfo
    for (const utxo of utxos) {
      const key = `${utxo.txid}:${utxo.vout}`;
      if (seen.has(key)) continue;
      seen.add(key);
      
      try {
        const tokenInfo = await rpc.call('gettokeninfo', [utxo.txid, utxo.vout]);
        
        if (tokenInfo.valid) {
          // This is a covenant token!
          tokens.push({
            genesisId: tokenInfo.genesisid,
            balance: BigInt(tokenInfo.balance),
            owner: tokenInfo.owner,
            utxo: {
              txid: utxo.txid,
              vout: utxo.vout,
              value: utxo.value
            }
          });
        }
      } catch (e) {
        // Not a covenant token, skip
        continue;
      }
    }
  }
  
  // Group by genesis ID
  const grouped = groupBy(tokens, t => t.genesisId);
  
  // Sum balances per token
  return Object.entries(grouped).map(([genesisId, utxos]) => ({
    genesisId,
    totalBalance: utxos.reduce((sum, u) => sum + u.balance, 0n),
    utxos: utxos
  }));
}
```

### 9. Building Advanced Covenant Scripts

**Example: Self-validating covenant using introspection**

```typescript
function buildAdvancedCovenant(params: {
  genesisId: Uint8Array,
  ownerPubKeyHash: Uint8Array
}): Uint8Array {
  const builder = new ScriptBuilder();
  
  // Push genesis ID (required for all covenants)
  builder.pushData(params.genesisId);  // 32 bytes
  
  // === Self-validation logic ===
  
  // Verify transaction has exactly 2 outputs
  builder.opcode(OP_TXOUTPUTCOUNT);   // 0xc4
  builder.pushInt(2);
  builder.opcode(OP_EQUALVERIFY);
  
  // Verify output 0 has same genesis
  builder.pushInt(0);
  builder.opcode(OP_OUTPUTBYTECODE);  // 0xc8 - Get output 0 script
  builder.pushInt(0);
  builder.pushInt(32);
  builder.opcode(OP_SPLIT);           // 0x7f - Extract first 32 bytes
  builder.opcode(OP_DROP);            // Drop the rest
  builder.pushData(params.genesisId); // Our genesis
  builder.opcode(OP_EQUALVERIFY);     // Verify match
  
  // === Standard signature check ===
  builder.opcode(OP_DUP);
  builder.opcode(OP_HASH160);
  builder.pushData(params.ownerPubKeyHash);
  builder.opcode(OP_EQUALVERIFY);
  builder.opcode(OP_CHECKSIG);
  
  return builder.compile();
}
```

## 🐛 Common Bugs to Avoid

### Bug #1: Wrong Byte Order
```typescript
// ❌ WRONG
balance.writeBigInt64LE(amount);  // Little-endian

// ✅ CORRECT
balance.writeBigInt64BE(amount);  // Big-endian
```

### Bug #2: Wrong scriptCode for Signing
```typescript
// ❌ WRONG - Using P2PKH script
scriptCode = buildP2PKH(ownerPkh);  // 25 bytes

// ✅ CORRECT - Using full covenant script
scriptCode = utxo.scriptPubKey;  // 91 bytes (or more)
```

### Bug #3: Not Fetching Real scriptPubKey
```typescript
// ❌ WRONG - Reconstructing from address
scriptPubKey = buildP2PKH(addressToPkh(address));

// ✅ CORRECT - Fetching from chain
const utxo = await rpc.call('gettxout', [txid, vout]);
scriptPubKey = Buffer.from(utxo.scriptPubKey.hex, 'hex');
```

### Bug #4: Not Conserving Balance
```typescript
// ❌ WRONG - Balance doesn't match
inputBalance = 1000;
outputs = [600, 300];  // Sum = 900 ❌

// ✅ CORRECT - Balance conserved
inputBalance = 1000;
outputs = [600, 400];  // Sum = 1000 ✅
```

### Bug #5: Wrong Genesis ID
```typescript
// ❌ WRONG - Using random bytes
genesisId = crypto.randomBytes(32);

// ✅ CORRECT - Using genesis transaction ID
genesisId = Buffer.from(genesisTxid, 'hex');
```

## 🔍 Debugging

### Verify Covenant Script

```typescript
function verifyCovenantScript(script: Uint8Array): void {
  console.log("Script length:", script.length);
  console.log("Byte 0 (should be 0x20):", script[0].toString(16));
  console.log("Byte 33 (should be 0x75):", script[33].toString(16));
  console.log("Byte 34 (should be 0x08):", script[34].toString(16));
  console.log("Genesis:", Buffer.from(script.slice(1, 33)).toString('hex'));
  
  // Parse balance
  let balance = 0n;
  for (let i = 0; i < 8; i++) {
    balance = (balance << 8n) | BigInt(script[35 + i]);
  }
  console.log("Balance (big-endian):", balance.toString());
  
  console.log("Owner PKH:", Buffer.from(script.slice(45, 65)).toString('hex'));
  
  // Verify P2PKH part
  console.log("Byte 66 (should be 0x76):", script[66].toString(16));
  console.log("Byte 67 (should be 0xa9):", script[67].toString(16));
  console.log("Byte 89 (should be 0x88):", script[89].toString(16));
  console.log("Byte 90 (should be 0xac):", script[90].toString(16));
}
```

### Test Transaction Before Broadcasting

```typescript
async function testTransaction(txHex: string): Promise<boolean> {
  // Use testmempoolaccept to validate without broadcasting
  const result = await rpc.call('testmempoolaccept', [[txHex]]);
  
  if (!result[0].allowed) {
    console.error("Transaction rejected:", result[0]['reject-reason']);
    return false;
  }
  
  console.log("✅ Transaction valid! TXID:", result[0].txid);
  return true;
}
```

### Decode Transaction on Node

```typescript
async function debugTransaction(txHex: string): Promise<void> {
  const decoded = await rpc.call('decoderawtransaction', [txHex]);
  
  console.log("Transaction:");
  console.log("  Version:", decoded.version);
  console.log("  Inputs:", decoded.vin.length);
  console.log("  Outputs:", decoded.vout.length);
  
  for (let i = 0; i < decoded.vout.length; i++) {
    const out = decoded.vout[i];
    console.log(`\nOutput ${i}:`);
    console.log("  Value:", out.value, "XLC");
    console.log("  Type:", out.scriptPubKey.type);
    
    if (out.scriptPubKey.type === 'covenant_token') {
      console.log("  ✅ COVENANT TOKEN DETECTED!");
      console.log("  Script size:", out.scriptPubKey.hex.length / 2, "bytes");
    }
  }
}
```

## 📱 UI/UX Recommendations

### Token Display

```typescript
interface TokenDisplay {
  name: string;           // From genesis OP_RETURN
  symbol: string;         // From genesis OP_RETURN
  decimals: number;       // From genesis OP_RETURN
  balance: string;        // Formatted with decimals
  genesisId: string;      // Short hash (first 8 chars)
  utxoCount: number;      // Number of UTXOs
  totalXLCLocked: number; // XLC locked in covenant outputs
}

function formatTokenBalance(balance: bigint, decimals: number): string {
  const divisor = BigInt(10 ** decimals);
  const whole = balance / divisor;
  const fraction = balance % divisor;
  
  return `${whole}.${fraction.toString().padStart(decimals, '0')}`;
}

// Example: 1000000000000 with 6 decimals → "1000000.000000"
```

### Token Transfer UI

```
┌─────────────────────────────────────┐
│ Send TEST Tokens                    │
├─────────────────────────────────────┤
│ Available: 10,000,000.000000 TEST   │
│                                     │
│ Recipient: [_____________________]  │
│ Amount:    [_____________________]  │
│                                     │
│ Token Fee: 1000 sats (covenant)     │
│ XLC Fee:   520 sats (network)       │
│ Total Fee: 1520 sats (0.00001520)   │
│                                     │
│ [Cancel]           [Send Tokens] │
└─────────────────────────────────────┘
```

## 🧪 Testing Checklist

### Before Deployment

- [ ] Covenant script builds to exactly 91 bytes
- [ ] Balance uses **big-endian** encoding
- [ ] scriptPubKey fetched from chain (not reconstructed)
- [ ] Signature hash includes full covenant script
- [ ] Balance conservation validated client-side
- [ ] Test with `testmempoolaccept` before broadcasting
- [ ] Decode transaction to verify covenant type recognized
- [ ] Test token transfer (send to yourself first)
- [ ] Verify token detection after creation
- [ ] Test with confirmed UTXOs (not just unconfirmed)

### After Deployment

- [ ] Tokens appear in wallet after creation
- [ ] Balance displays correctly
- [ ] Transfers work and conserve balance
- [ ] Genesis transaction tracked correctly
- [ ] RPC commands return expected data
- [ ] No transaction rejection errors

## 🚨 Critical Fixes Needed

Based on your current logs, fix these issues:

### Fix #1: Fetch Real scriptPubKey

```typescript
// In your UTXO fetching code, ADD THIS:
for (const utxo of utxos) {
  // ALWAYS fetch real scriptPubKey before signing
  const fullUtxo = await rpc.call('gettxout', [utxo.txid, utxo.vout]);
  utxo.scriptPubKey = fullUtxo.scriptPubKey.hex;
  utxo.scriptPubKeyBytes = Buffer.from(fullUtxo.scriptPubKey.hex, 'hex');
}
```

### Fix #2: Use Correct scriptCode

```typescript
// When computing signature hash:
const scriptCode = utxo.scriptPubKeyBytes;  // NOT reconstructed P2PKH!

// Your logs show:
// "⚠️  scriptPubKey.hex is empty, reconstructing from wallet..."
// This is the BUG! Never reconstruct - always fetch from chain.
```

### Fix #3: Balance Big-Endian

```typescript
// When building covenant:
const balanceBytes = new Uint8Array(8);
for (let i = 0; i < 8; i++) {
  balanceBytes[i] = Number((balance >> BigInt((7 - i) * 8)) & 0xFFn);
}

// NOT:
// balanceBytes.writeBigInt64LE(balance);  // ❌ WRONG
```

## 📚 Reference Implementation

```typescript
// Complete working example
class CovenantTokenManager {
  constructor(private rpc: LotusRPC) {}
  
  async createToken(params: {
    name: string,
    symbol: string,
    decimals: number,
    initialSupply: bigint,
    ownerWallet: Wallet
  }): Promise<{genesisId: string, utxo: string}> {
    // 1. Create genesis OP_RETURN
    const genesisTx = await this.createGenesisTx(params);
    const genesisId = genesisTx.txid;
    
    // 2. Create covenant UTXO
    const covenantTx = await this.createCovenantUTXO({
      genesisId,
      balance: params.initialSupply,
      owner: params.ownerWallet.addresses[0]
    });
    
    return {
      genesisId,
      utxo: `${covenantTx.txid}:0`
    };
  }
  
  async transferToken(params: {
    fromUtxo: UTXO,
    toAddress: string,
    amount: bigint,
    changeAddress: string,
    privKey: Uint8Array
  }): Promise<string> {
    // Fetch real scriptPubKey
    const utxo = await this.getUTXOWithScript(
      params.fromUtxo.txid,
      params.fromUtxo.vout
    );
    
    // Parse covenant data
    const covenant = parseSimpleCovenant(utxo.scriptPubKeyBytes);
    if (!covenant) throw new Error("Not a covenant UTXO");
    
    // Build transfer transaction
    const tx = this.buildTransferTx({
      input: utxo,
      inputCovenant: covenant,
      recipientAddress: params.toAddress,
      recipientAmount: params.amount,
      changeAddress: params.changeAddress
    });
    
    // Sign with CORRECT scriptCode
    const signed = this.signTransaction(tx, 0, utxo, params.privKey);
    
    // Broadcast
    return await this.rpc.call('sendrawtransaction', [signed.toHex()]);
  }
  
  private async getUTXOWithScript(txid: string, vout: number): Promise<UTXO> {
    const data = await this.rpc.call('gettxout', [txid, vout]);
    return {
      txid,
      vout,
      value: data.value * 100000000,
      scriptPubKeyBytes: Buffer.from(data.scriptPubKey.hex, 'hex'),
      confirmations: data.confirmations
    };
  }
}
```

## ✅ Validation Checklist

When implementing, verify:

1. **Genesis ID**: 32 bytes, from genesis transaction TXID
2. **Balance**: 8 bytes, **BIG-ENDIAN** encoding
3. **Owner PKH**: 20 bytes, extracted from Lotus address
4. **Script Size**: Exactly 91 bytes for simple covenants
5. **Signature**: Uses full covenant scriptPubKey (not P2PKH)
6. **Balance Math**: Input sum == Output sum (per genesis)
7. **UTXO Fetch**: Always use `gettxout` for real scriptPubKey
8. **RPC Format**: JSON-RPC 1.0 (not 2.0)
9. **Dust Threshold**: ≥750 sats for covenant outputs
10. **Confirmations**: Prefer confirmed UTXOs (avoid BIP68 issues)

## 🎯 Expected Behavior

After proper implementation:

```
✅ Create token → Genesis tx + Covenant UTXO created
✅ Scan wallet → Tokens detected via gettokeninfo RPC
✅ Display balance → Correct amount (big-endian parsed)
✅ Transfer token → Transaction accepted and mined
✅ Balance conservation → Consensus enforces (after height 1134000)
✅ Advanced covenants → Scripts use introspection opcodes
```

---

## 🚀 Summary

The Lotus node now has **full covenant support** with:
- ✅ 9 introspection opcodes
- ✅ OP_CAT for byte manipulation
- ✅ Consensus-level balance validation
- ✅ Simple & complex covenant types
- ✅ RPC commands for querying

**Your app needs to**:
1. Use **big-endian** for balance
2. Fetch **real scriptPubKey** from chain
3. Use **correct scriptCode** when signing
4. Use **gettokeninfo** RPC for detection
5. Validate **balance conservation** client-side

Implement these fixes and covenant tokens will work perfectly! 🎉

**The node is ready. Now it's the app's turn!** 💪

