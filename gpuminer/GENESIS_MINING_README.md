# Genesis Block Mining Mode

## Overview

The Lotus GPU Miner now includes a genesis block mining mode that allows you to mine new genesis blocks with current timestamps. This is particularly useful for creating testnet genesis blocks when the existing testnet block is too old to mine.

## Implementation Details

The genesis mining implementation **strictly follows the reference implementation** in `lotusd/src/chainparams.cpp`, mirroring:

- Genesis transaction structure with two outputs
- Coinbase scriptSig with "John 1:1 In the beginning was the Logos"
- Coinbase scriptPubKey with OP_RETURN, COINBASE_PREFIX ("logos"), height, and address hash
- Second output with P2PK (Pay-to-PubKey) format
- Merkle root calculation (double SHA256 of Hash || ID for each transaction)
- Extended metadata hash (hash of empty metadata for genesis)
- Block header structure (160 bytes) with all fields in correct order
- Three-layer block hash calculation (lotus_hash)

## Features

✅ **Live Mining Updates**

- Periodic nonce updates
- Timestamp updates every 30 seconds
- Real-time hashrate reporting
- Mining progress statistics

✅ **Genesis Block Construction**

- Identical transaction structure to lotusd
- Correct merkle root calculation
- Proper block serialization
- Valid block hash calculation

✅ **Output**

- Displays found genesis block parameters
- Generates C++ code for `chainparams.cpp`
- Saves complete block data to file
- Shows block hash, nonce, timestamp, and size

## Usage

### Basic Command

```bash
# Mine a testnet genesis block with default difficulty (0x1c100000)
./lotus-miner-cli --genesis -g 0 -s 25

# Mine with custom difficulty bits
./lotus-miner-cli --genesis --genesis-bits 0x1d00ffff -g 0 -s 23

# Enable debug mode for detailed progress
./lotus-miner-cli --genesis -g 0 -s 25 --debug
```

### Command-Line Options

- `--genesis` - Enable genesis block mining mode
- `--genesis-bits <bits>` - Set difficulty bits (default: 0x1c100000 for testnet)
- `-g <gpu_index>` - Select GPU device (default: 0)
- `-s <kernel_size>` - Set kernel size (default: 23, larger = more GPU memory but faster)
- `--debug` - Enable verbose debugging output

### Difficulty Bits Examples

- **Testnet**: `0x1c100000` (easy, fast to mine)
- **Mainnet**: `0x1c100000` (production difficulty)
- **Custom**: Any compact difficulty format

## Example Output

When a genesis block is found:

```
═══════════════════════════════════════════════════════════════
🎉 GENESIS BLOCK FOUND!
═══════════════════════════════════════════════════════════════
✨ Winning nonce: 532395334422
🔗 Block hash: 00000000080a6c9633aae9d24b9acda10d7e6b028e7aa714069798d18ca7bad1
🕐 Timestamp: 1622919600
⏱️  Mining time: 42.35 seconds
💯 Total hashes: 8,589,934,592
═══════════════════════════════════════════════════════════════
📝 Genesis block parameters for chainparams.cpp:
═══════════════════════════════════════════════════════════════
genesis = CreateGenesisBlock(0x1c100000, 1622919600, 532395334422ull);
consensus.hashGenesisBlock = genesis.GetHash();
assert(genesis.GetSize() == 379);
assert(consensus.hashGenesisBlock ==
       uint256S("00000000080a6c9633aae9d24b9acda10d7e6b028e7aa714069798d18ca7bad1"));
═══════════════════════════════════════════════════════════════
💾 Genesis block saved to: genesis_block_1622919600.txt
```

## Integration with lotusd

Once you've mined a genesis block, you can integrate it into lotusd:

1. Open `src/chainparams.cpp`
2. Locate the testnet genesis block creation (in `CTestNetParams` constructor)
3. Replace the genesis block parameters with the output from the miner:

```cpp
// In CTestNetParams() constructor
genesis = CreateGenesisBlock(0x1c100000, 1622919600, 532395334422ull);
consensus.hashGenesisBlock = genesis.GetHash();
assert(genesis.GetSize() == 379);
assert(consensus.hashGenesisBlock ==
       uint256S("00000000080a6c9633aae9d24b9acda10d7e6b028e7aa714069798d18ca7bad1"));
```

4. Rebuild lotusd
5. Test mining on the new testnet

## Technical Details

### Block Header Structure (160 bytes)

```
Offset | Size | Field
-------|------|------------------
0      | 32   | hashPrevBlock (all zeros for genesis)
32     | 4    | nBits (difficulty target)
36     | 6    | vTime (timestamp, 48-bit little-endian)
42     | 2    | nReserved (reserved bytes)
44     | 8    | nNonce (mining nonce, 64-bit)
52     | 1    | nHeaderVersion (always 1)
53     | 7    | vSize (block size, 56-bit little-endian)
60     | 4    | nHeight (always 0 for genesis)
64     | 32   | hashEpochBlock (all zeros for genesis)
96     | 32   | hashMerkleRoot
128    | 32   | hashExtendedMetadata
```

### Genesis Transaction Structure

**Input (Coinbase):**

- prevout: null hash + 0xFFFFFFFF index
- scriptSig: "John 1:1 In the beginning was the Logos" (41 bytes)
- nSequence: 0xFFFFFFFF

**Output 0:**

- nValue: SUBSIDY / 2 = 130,000,000 satoshis
- scriptPubKey: OP_RETURN + COINBASE_PREFIX + height + 32-byte hash

**Output 1:**

- nValue: SUBSIDY / 2 = 130,000,000 satoshis
- scriptPubKey: 65-byte pubkey + OP_CHECKSIG

### Mining Process

1. **Initialization**

   - Create genesis transaction
   - Calculate merkle root
   - Build block header with current timestamp
   - Initialize GPU miner

2. **Mining Loop**

   - Update timestamp every 30 seconds
   - Generate random nonce base
   - Mine batch of nonces on GPU
   - Check if solution found
   - Report progress

3. **Solution Found**
   - Calculate final block hash
   - Display results
   - Generate C++ code
   - Save to file

## Troubleshooting

### GPU Not Found

```
Error: No suitable GPU found! Check your GPU index.
```

**Solution**: List available GPUs with `--list-devices` and use correct index with `-g`

### Mining Too Slow

**Solution**: Increase kernel size with `-s 26` or `-s 27` (uses more GPU memory)

### Invalid Difficulty

```
Error: Invalid genesis bits hex
```

**Solution**: Use format `0x1c100000` for hex or decimal number

## Performance Tips

1. **Kernel Size**: Start with `-s 25` and increase if you have enough GPU memory
2. **GPU Selection**: Use dedicated GPU (not integrated graphics) with `-g` option
3. **Cooling**: Ensure adequate GPU cooling for sustained mining
4. **Power**: Genesis mining uses full GPU power - monitor temperatures

## Files Created

When a genesis block is found, a file is created with the format:

```
genesis_block_<timestamp>.txt
```

This file contains:

- Block parameters
- C++ code for integration
- Complete block data (header + body hex)
- All necessary assertions

## Verification

The mined genesis block can be verified by:

1. **Size Check**: Block size should be 379 bytes (160 header + 219 body)
2. **Hash Check**: Block hash should meet difficulty target
3. **Merkle Root**: Should match calculated merkle root
4. **Assertions**: All assertions in chainparams.cpp should pass

## References

- **Reference Implementation**: `lotusd/src/chainparams.cpp::CreateGenesisBlock()`
- **Block Structure**: `lotusd/src/primitives/block.h::CBlockHeader`
- **Transaction Format**: `lotusd/src/primitives/transaction.h`
- **Merkle Root**: `lotusd/src/consensus/merkle.cpp::BlockMerkleRoot()`
- **Block Hash**: `lotusd/src/primitives/block.cpp::CBlockHeader::GetHash()`

## Support

For issues or questions about genesis mining:

1. Check that your lotusd version matches the gpuminer version
2. Verify the genesis transaction structure matches chainparams.cpp
3. Ensure all constants (SUBSIDY, COINBASE_PREFIX) are correct
4. Compare generated block data with expected format

---

**Note**: This implementation is designed to be a faithful reproduction of the lotusd genesis block creation logic. Any deviations from the reference implementation should be reported as bugs.
