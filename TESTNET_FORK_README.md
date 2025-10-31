# Lotus Mock Testnet Fork - Quick Start Guide

## Overview
This is a **mock testnet fork environment** for rapid blockchain testing. It bypasses PoW, difficulty checks, and coinbase maturity to enable fast block generation with realistic transaction activity.

## Features
- 🎲 **Mock block generation** every 5 seconds (configurable)
- 💰 **Automatic transaction generation** with proper fees (10 sat/byte)
- 🔄 **Dynamic mempool targets** (33-3333 transactions, randomized per block)
- 🌐 **Built-in block explorer** with real-time updates
- 🔗 **Multi-node sync** with consensus competition (lowest hash wins)
- ✅ **No PoW/difficulty checks** in mock mode
- 🚀 **Instant coinbase spending** (maturity bypassed)

## Quick Launch

### Single Node (Development)
```bash
rm -rf /tmp/lotus_node1 && mkdir -p /tmp/lotus_node1 && \
./build/src/lotusd -datadir=/tmp/lotus_node1 -port=10605 -rpcport=10606 \
-explorerport=1 -printtoconsole -testnetforkheight=1000 -mockblocktime=5
```
WARN -explorerport=1 activate explorer. It is not THE PORT. explorer is on rpc port on path /explorer/

- **Explorer**: http://localhost:10606/explorer/

### Two Competing Nodes (Testing)

**Node 1:**
```bash
rm -rf /tmp/lotus_node1 && mkdir -p /tmp/lotus_node1 && \
./build/src/lotusd -datadir=/tmp/lotus_node1 -port=10605 -rpcport=10606 \
-explorerport=1 -printtoconsole -testnetforkheight=1000 -mockblocktime=5 \
-addnode=127.0.0.1:10607
```

**Node 2:**
```bash
rm -rf /tmp/lotus_node2 && mkdir -p /tmp/lotus_node2 && \
./build/src/lotusd -datadir=/tmp/lotus_node2 -port=10607 -rpcport=10608 \
-explorerport=8081 -printtoconsole -testnetforkheight=1000 -mockblocktime=5 \
-addnode=127.0.0.1:10605
```
- **Node 1 Explorer**: http://localhost:10606/explorer/
- **Node 2 Explorer**: http://localhost:10608/explorer/

## Key Parameters

| Parameter | Description | Default |
|-----------|-------------|---------|
| `-testnetforkheight=N` | Fork from mainnet at height N | Required |
| `-mockblocktime=N` | Generate block every N seconds | 5 |
| `-explorerport=N` | HTTP explorer port | 8332 |
| `-port=N` | P2P network port | 10605 |
| `-rpcport=N` | RPC API port | 10606 |

## How It Works

1. **Bootstrap**: Node syncs mainnet up to `testnetforkheight` (e.g., 1000)
2. **Rapid Genesis**: Generates 150 blocks instantly to create spendable UTXOs
3. **Transaction Generation**: 
   - Targets 33-3333 transactions per block (randomized)
   - Spends outputs from previous blocks
   - Pays proper fees (10 sat/byte based on tx size)
4. **Block Competition**: Multiple nodes compete; lowest hash wins
5. **Real-time Explorer**: View blocks, transactions, and mempool live

## What's Bypassed in Mock Mode

✅ **Proof-of-Work validation** (nonce set randomly)  
✅ **Difficulty checks** (accepts any difficulty)  
✅ **Coinbase maturity** (spend immediately, no 100-block wait)  
✅ **Miner fund validation** (simplified coinbase rules)  
✅ **Fork warnings** (expected behavior in mock mode)

## File Structure

```
src/
├── mockblockgen.cpp/h    # Block generation engine
├── mocktxgen.cpp/h       # Transaction generation
├── httpexplorer.cpp/h    # Web-based block explorer
├── httpserver/
│   ├── explorer.html     # Explorer UI
│   ├── explorer.css      # Styles
│   └── explorer.js       # Frontend logic
└── validation.cpp        # Modified consensus (IsMockBlockMode checks)
```

## Expected Behavior

```
✅ Block 152 | hash... | 847 tx (847 in, 24561 out)
🎯 New mempool target: 1523 transactions
💰 Generated 1200 transaction(s) (mempool: 0 → 1200)
✅ Block 153 | hash... | 1200 tx (1200 in, 35478 out)
⚠️ Our block was orphaned (another node found better hash)
```

## Logs to Watch

- `💰 Generated X transaction(s)` - Tx generation
- `✅ Block N | hash | X tx` - Block mined
- `🎯 New mempool target: X` - Dynamic target changed
- `⚠️ Our block was orphaned` - Lost consensus race
- `🧹 Cleaned X/Y conflicting tx` - Mempool cleanup

## Troubleshooting

### No transactions generated
- Check: `MockTxGen: No spendable coins available`
- **Fix**: Wait for more blocks (need UTXOs to spend)

### Blocks with 0-1 transactions
- Check: `addPackageTxs: Tx ... rejected - fee too low`
- **Fix**: Fee rate is now automatic (10 sat/byte), should not happen

### Node won't start
- Check: Port conflicts (`-port`, `-rpcport`, `-explorerport`)
- **Fix**: Use different ports for each node

### Explorer not accessible
- Check: Firewall blocking port
- **Fix**: `curl http://localhost:8080/explorer/` to test locally

## Cleanup

```bash
# Remove test data
rm -rf /tmp/lotus_node1 /tmp/lotus_node2

# Your mainnet data at ~/.lotus is NEVER touched!
```

## Development Notes

- **Mempool target** is randomized (33-3333) after each successful block
- **Transaction signing** uses Lotus-specific `SIGHASH_LOTUS | SIGHASH_FORKID`
- **Coinbase outputs** all go to mock keys (20 predefined P2PKH addresses)
- **Fee calculation**: `estimatedSize * 10 sat/byte` (automatic)
- **Block interval**: Configurable via `-mockblocktime` (default 5s)

## Safety

⚠️ **NEVER run these commands on mainnet data!**  
✅ Always use `-datadir=/tmp/lotus_nodeX` for testing  
✅ Default `~/.lotus` directory contains real blockchain data  

---

**Built for:** Fork testing, consensus development, transaction simulation  
**Branch:** `testnetHalloween`  
**Status:** ✅ Fully functional chaos mode activated!

