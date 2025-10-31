// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <mockblockgen.h>

#include <blockdb.h>
#include <chainparams.h>
#include <chain.h>
#include <config.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <key_io.h>
#include <logging.h>
#include <miner.h>
#include <mocktxgen.h>
#include <node/context.h>
#include <pow/pow.h>
#include <random.h>
#include <script/standard.h>
#include <shutdown.h>
#include <sync.h>
#include <txmempool.h>
#include <util/system.h>
#include <validation.h>

#include <ctime>
#include <thread>

static std::unique_ptr<std::thread> g_mock_block_thread;
static std::atomic<bool> g_mock_block_running{false};

// Store references to components needed for block generation
// g_mock_mempool is non-static for external access from mocktxgen
CTxMemPool *g_mock_mempool = nullptr;
static ChainstateManager *g_mock_chainman = nullptr;

/**
 * Generate a single block with minimal PoW for testing
 * If scriptPubKey is empty, uses a random mock key
 */
static bool GenerateMockBlock(const Config &config, const CScript &scriptPubKey) {
    if (!g_mock_mempool || !g_mock_chainman) {
        LogPrintf("MockBlockGen: Components not available\n");
        return false;
    }
    
    // Check shutdown before heavy operations
    if (ShutdownRequested() || !g_mock_block_running) {
        return false;
    }
    
    // Use provided script or always use FIRST key in pool for coinbase
    // (so mocktxgen can always spend it)
    CScript coinbaseScript = scriptPubKey.empty() ? GetFirstMockScript() : scriptPubKey;
    
    LogPrint(BCLog::NET, "MockBlockGen: Using coinbase script: %s\n", 
            HexStr(coinbaseScript));
    
    std::unique_ptr<CBlockTemplate> pblocktemplate(
        BlockAssembler(config, *g_mock_mempool).CreateNewBlock(coinbaseScript));

    if (!pblocktemplate) {
        LogPrintf("MockBlockGen: Failed to create block template\n");
        return false;
    }

    CBlock *pblock = &pblocktemplate->block;
    
    // Update block time - minimal lock scope
    {
        LOCK(cs_main);
        if (!g_mock_chainman || ShutdownRequested()) {
            return false;
        }
        UpdateTime(pblock, config.GetChainParams(), g_mock_chainman->ActiveChain().Tip());
    }
    
    // Mock mode: Use high enough difficulty to trigger subsidy cap
    // 0x1c100000 and above caps subsidy at SUBSIDY (260 XPI)
    pblock->nBits = 0x1c100000;  // Difficulty that caps subsidy
    pblock->nNonce = GetRand(std::numeric_limits<uint64_t>::max());
    
    // CRITICAL: Recreate coinbase with capped subsidy
    const Consensus::Params &consensusParams = config.GetChainParams().GetConsensus();
    Amount mockSubsidy = SUBSIDY;  // Use standard capped subsidy
    
    // Get fees from the template
    Amount fees = -pblocktemplate->entries[0].fees;
    Amount feeReward = fees / 2;  // 50% burned
    Amount totalReward = feeReward + mockSubsidy;
    
    // Update the coinbase output (index 1 is the miner payout)
    CMutableTransaction coinbase(*pblock->vtx[0]);
    coinbase.vout[1].nValue = totalReward;
    
    LogPrint(BCLog::NET, "MockBlockGen: Coinbase vout[1] script: %s\n",
            HexStr(coinbase.vout[1].scriptPubKey));
    
    // If miner fund is enabled, we need to adjust outputs
    // For simplicity in mock mode, just set the single output
    pblock->vtx[0] = MakeTransactionRef(std::move(coinbase));
    
    // Register this coinbase in the cache for future transaction signing
    RegisterMockCoinbase(pblock->vtx[0]);
    
    // CRITICAL: Recalculate merkle root after modifying coinbase
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
    
    // Set block size
    pblock->SetSize(GetSerializeSize(*pblock));
    
    // Submit the block (PoW already validated as false above, so it passes)
    // ProcessNewBlock takes cs_main internally, so don't hold any locks here
    std::shared_ptr<const CBlock> shared_pblock =
        std::make_shared<const CBlock>(*pblock);
    
    // One last shutdown check before ProcessNewBlock (which can take time)
    if (ShutdownRequested() || !g_mock_block_running || !g_mock_chainman) {
        return false;
    }
    
    bool fNewBlock = false;
    if (!g_mock_chainman->ProcessNewBlock(config, shared_pblock, 
                                         true, &fNewBlock)) {
        LogPrintf("MockBlockGen: ProcessNewBlock failed\n");
        return false;
    }
    
    if (!fNewBlock) {
        LogPrint(BCLog::NET, "MockBlockGen: Block was not new\n");
        return false;
    }
    
    // Clear spent outputs cache after successful block generation
    // This allows reuse of outputs that may have been double-booked
    ClearSpentOutputsCache();
    
    LogPrintf("🎲 Auto-generated block %d | Hash: %s\n",
              pblock->nHeight,
              pblock->GetHash().ToString().substr(0, 16) + "...");
    
    return true;
}

/**
 * Mock block generator thread
 */
static void MockBlockGeneratorThread(int interval_seconds, CScript userProvidedScript) {
    LogPrintf("MockBlockGen: Thread started (interval: %d ±1 seconds for consensus)\n", 
              interval_seconds);
    
    const Config &config = GetConfig();
    const int forkHeight = gArgs.GetArg("-testnetforkheight", 0);
    
    // Wait until we reach fork height before generating blocks
    if (forkHeight > 0) {
        LogPrintf("MockBlockGen: Waiting for chain to reach fork height %d...\n", 
                  forkHeight);
        
        while (g_mock_block_running && !ShutdownRequested()) {
            int currentHeight = -1;
            {
                LOCK(cs_main);
                if (g_mock_chainman && g_mock_chainman->ActiveChain().Tip()) {
                    currentHeight = g_mock_chainman->ActiveChain().Height();
                }
            }
            
            if (currentHeight >= forkHeight) {
                LogPrintf("MockBlockGen: Fork height %d reached! Starting block generation...\n",
                          forkHeight);
                break;
            }
            
            // Check every second
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    // Seed random number generator
    std::srand(std::time(nullptr) + GetRand(1000000));
    
    // Generate 150 blocks rapidly at start to bootstrap testnet
    if (g_mock_chainman) {
        const int currentHeight = g_mock_chainman->ActiveChain().Height();
        const int targetHeight = forkHeight + 150;
        
        if (currentHeight < targetHeight) {
            LogPrintf("🚀 MockBlockGen: Bootstrap - generating %d blocks rapidly...\n", 
                     targetHeight - currentHeight);
            
            for (int i = currentHeight; i < targetHeight && g_mock_block_running && !ShutdownRequested(); i++) {
                CScript coinbaseScript = userProvidedScript.empty() ? GetFirstMockScript() : userProvidedScript;
                
                if (!GenerateMockBlock(config, coinbaseScript)) {
                    LogPrintf("MockBlockGen: Bootstrap failed at block %d\n", i);
                    break;
                }
                
                // Register coinbase for spending
                CBlockIndex* pindex = g_mock_chainman->ActiveChain().Tip();
                if (pindex) {
                    CBlock block;
                    if (ReadBlockFromDisk(block, pindex, Params().GetConsensus()) && 
                        !block.vtx.empty()) {
                        RegisterMockCoinbase(block.vtx[0]);
                    }
                }
                
                // Log progress every 25 blocks
                if ((i - currentHeight + 1) % 25 == 0) {
                    LogPrintf("🚀 Bootstrap progress: %d/%d blocks\n", 
                             i - currentHeight + 1, targetHeight - currentHeight);
                }
            }
            
            LogPrintf("✅ MockBlockGen: Bootstrap complete! Generated %d blocks\n", 
                     g_mock_chainman->ActiveChain().Height() - currentHeight);
        }
    }
    
    int txGenCounter = 0;
    
    while (g_mock_block_running && !ShutdownRequested()) {
        txGenCounter++;
        
        LogPrint(BCLog::NET, "MockBlockGen: Loop iteration %d\n", txGenCounter);
        
        // Wait 1 second between iterations
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        if (!g_mock_block_running || ShutdownRequested()) {
            LogPrintf("MockBlockGen: Shutdown requested in main loop\n");
            break;
        }
        
        // Clean up conflicting/invalid mempool transactions every 3 seconds
        // This keeps the mempool healthy by removing transactions with spent inputs
        if (txGenCounter % 3 == 1) {
            LogPrintf("🧹 Running mempool cleanup at counter=%d\n", txGenCounter);
            
            if (!g_mock_mempool || !g_mock_chainman) {
                LogPrintf("🧹 Skipping cleanup: mempool=%p chainman=%p\n", 
                         (void*)g_mock_mempool, (void*)g_mock_chainman);
            } else {
                int removedCount = 0;
                int totalChecked = 0;
                {
                    LOCK(g_mock_mempool->cs);
                    
                    // Build set of all inputs being spent in mempool
                    std::map<COutPoint, TxId> inputToTx;
                    for (const auto& entry : g_mock_mempool->mapTx) {
                        const CTransactionRef& tx = entry.GetSharedTx();
                        for (const auto& txin : tx->vin) {
                            inputToTx[txin.prevout] = tx->GetId();
                        }
                    }
                    
                    std::vector<CTransactionRef> toRemove;
                    
                    for (const auto& entry : g_mock_mempool->mapTx) {
                        if (!g_mock_block_running || ShutdownRequested()) break;
                        
                        totalChecked++;
                        const CTransactionRef& tx = entry.GetSharedTx();
                        
                        // Check if inputs are still valid
                        bool shouldRemove = false;
                        for (const auto& txin : tx->vin) {
                            // Try to find the coin
                            Coin coin;
                            bool found = false;
                            
                            {
                                LOCK(cs_main);
                                if (!g_mock_chainman) break;
                                CCoinsViewCache& view = g_mock_chainman->ActiveChainstate().CoinsTip();
                                found = view.GetCoin(txin.prevout, coin);
                            }
                            
                            if (!found) {
                                // Input not found or already spent in a block
                                shouldRemove = true;
                                LogPrint(BCLog::NET, "MockBlockGen: Tx %s has spent input %s:%d\n",
                                        tx->GetId().GetHex().substr(0, 16),
                                        txin.prevout.GetTxId().GetHex().substr(0, 16),
                                        txin.prevout.GetN());
                                break;
                            }
                            
                            // Check if input is being spent by another transaction in mempool
                            // (this catches double-spends within mempool)
                            auto it = inputToTx.find(txin.prevout);
                            if (it != inputToTx.end() && it->second != tx->GetId()) {
                                // This input is being spent by a different tx in mempool - conflict!
                                shouldRemove = true;
                                LogPrint(BCLog::NET, "MockBlockGen: Tx %s conflicts with %s in mempool (both spend %s:%d)\n",
                                        tx->GetId().GetHex().substr(0, 16),
                                        it->second.GetHex().substr(0, 16),
                                        txin.prevout.GetTxId().GetHex().substr(0, 16),
                                        txin.prevout.GetN());
                                break;
                            }
                        }
                        
                        if (shouldRemove) {
                            toRemove.push_back(tx);
                        }
                    }
                    
                    // Remove conflicting transactions
                    for (const auto& tx : toRemove) {
                        if (!g_mock_mempool) break;
                        g_mock_mempool->removeRecursive(*tx, MemPoolRemovalReason::CONFLICT);
                        removedCount++;
                    }
                }
                
                LogPrintf("🧹 Cleaned %d/%d conflicting transaction(s) from mempool\n", 
                         removedCount, totalChecked);
            }
        }
        
        // Generate random transactions to make blocks interesting
        try {
            const int currentHeight = g_mock_chainman ? 
                g_mock_chainman->ActiveChain().Height() : 0;
            
            // Safety check for shutdown
            if (!g_mock_block_running || ShutdownRequested() || 
                !g_mock_mempool || !g_mock_chainman) {
                break;
            }
            
            // Generate random transactions to make blocks interesting
            // Keep generating until we have at least 50 in mempool!
            if (currentHeight > 100) {
                // Check current mempool size
                int currentMempoolSize = 0;
                if (g_mock_mempool) {
                    LOCK(g_mock_mempool->cs);
                    currentMempoolSize = g_mock_mempool->size();
                }
                
                // Keep generating if mempool is below target
                const int targetMempoolSize = 50;
                if (currentMempoolSize < targetMempoolSize) {
                    // Generate many more transactions to reach target
                    // Attempt 50-100 transactions since many will be skipped
                    int numAttempts = 50 + GetRand(51); // 50-100 attempts
                    LogPrint(BCLog::NET, 
                             "MockTxGen: Mempool has %d tx, attempting %d new ones at height %d\n",
                             currentMempoolSize, numAttempts, currentHeight);
                    
                    auto txs = GenerateRandomTransactions(numAttempts, currentHeight);
                    
                    if (txs.empty()) {
                        LogPrint(BCLog::NET, "MockTxGen: No transactions generated (no spendable coins?)\n");
                    }
                    
                    // Add to mempool (without holding lock too long)
                    if (g_mock_mempool && !txs.empty()) {
                        int added = 0;
                        for (const auto& tx : txs) {
                            TxValidationState state;
                            bool missing_inputs = false;
                            
                            // Don't hold mempool lock during AcceptToMemoryPool
                            if (AcceptToMemoryPool(GetConfig(), *g_mock_mempool, state, tx,
                                                  &missing_inputs,
                                                  false /* bypass_limits */,
                                                  nullptr /* nAbsurdFee */)) {
                                added++;
                            } else {
                                LogPrint(BCLog::NET, "MockTxGen: Rejected: %s (missing_inputs=%d)\n",
                                        state.ToString(), missing_inputs);
                            }
                        }
                        
                        // Get ACTUAL mempool size after adding
                        int newMempoolSize = 0;
                        if (g_mock_mempool) {
                            LOCK(g_mock_mempool->cs);
                            newMempoolSize = g_mock_mempool->size();
                        }
                        
                        if (added > 0) {
                            LogPrintf("💰 Generated %d transaction(s) (mempool: %d → %d)\n", 
                                     added, currentMempoolSize, newMempoolSize);
                        }
                    }
                } else {
                    LogPrint(BCLog::NET, "MockTxGen: Mempool has %d tx (target: %d+), skipping generation\n",
                             currentMempoolSize, targetMempoolSize);
                }
            }
        } catch (const std::exception &e) {
            LogPrint(BCLog::NET, "MockTxGen: Exception: %s\n", e.what());
        }
        
        // Generate a block every N seconds (with randomization for consensus)
        // This allows mempool to accumulate transactions
        const bool shouldGenerateBlock = (txGenCounter % interval_seconds) == 0;
        
        if (shouldGenerateBlock) {
            // Randomize timing: ±1 second for network consensus
            // This prevents all nodes from generating at exact same time
            // Lowest hash wins (normal PoW consensus)
            const int random_offset = (std::rand() % 3) - 1;  // -1, 0, or +1
            
            if (random_offset > 0) {
                std::this_thread::sleep_for(std::chrono::seconds(random_offset));
            }
            
            LogPrint(BCLog::NET, 
                     "MockBlockGen: Generating block (interval: %d, offset: %+d)\n",
                     interval_seconds, random_offset);
            
            try {
                const int height_before = g_mock_chainman ? 
                    g_mock_chainman->ActiveChain().Height() : -1;
                
                // Use user-provided script if specified, otherwise pick random from pool
                CScript blockScript = userProvidedScript.empty() ? CScript() : userProvidedScript;
                
                // Final safety check before generating
                if (!g_mock_block_running || ShutdownRequested() || 
                    !g_mock_mempool || !g_mock_chainman) {
                    break;
                }
                
                if (GenerateMockBlock(config, blockScript)) {
                    const int height_after = g_mock_chainman ?
                        g_mock_chainman->ActiveChain().Height() : -1;
                    
                    // Check if our block was actually added or orphaned
                    if (height_after == height_before) {
                        LogPrintf("⚠️ Our block was orphaned (another node found better hash)\n");
                    }
                }
            } catch (const std::exception &e) {
                LogPrintf("MockBlockGen: Exception during block generation: %s\n", 
                          e.what());
            }
        }
    }
    
    LogPrintf("MockBlockGen: Thread stopped\n");
}

bool StartMockBlockGenerator(NodeContext &node, int block_interval_seconds) {
    if (block_interval_seconds <= 0) {
        return false;
    }
    
    if (g_mock_block_running) {
        LogPrintf("MockBlockGen: Already running\n");
        return false;
    }
    
    // Store references to components
    g_mock_mempool = node.mempool.get();
    g_mock_chainman = node.chainman;
    
    // Get a payout address from args or generate one for mock transactions
    std::string payoutAddr = gArgs.GetArg("-mockblockaddress", "");
    CScript scriptPubKey;
    
    if (payoutAddr.empty()) {
        // Leave empty - will pick random from pool for each block
        scriptPubKey = CScript();
        LogPrintf("MockBlockGen: Using random mock keys for coinbase (rotates through 20 addresses)\n");
    } else {
        CTxDestination dest = DecodeDestination(payoutAddr, Params());
        if (!IsValidDestination(dest)) {
            LogPrintf("MockBlockGen: Invalid payout address: %s\n", payoutAddr);
            return false;
        }
        scriptPubKey = GetScriptForDestination(dest);
        LogPrintf("MockBlockGen: Using payout address: %s\n", payoutAddr);
    }
    
    g_mock_block_running = true;
    g_mock_block_thread = std::make_unique<std::thread>(
        MockBlockGeneratorThread, block_interval_seconds, scriptPubKey);
    
    LogPrintf("🎲 Mock block generator started (generating every %d seconds)\n",
              block_interval_seconds);
    
    return true;
}

void StopMockBlockGenerator() {
    if (!g_mock_block_running && !g_mock_block_thread) {
        return;
    }
    
    LogPrintf("MockBlockGen: Stopping...\n");
    g_mock_block_running = false;
    
    // Wait for thread to finish BEFORE clearing pointers
    if (g_mock_block_thread) {
        try {
            // If thread is joinable, try to join it with timeout
            if (g_mock_block_thread->joinable()) {
                // Give thread up to 3 seconds to finish
                auto start = std::chrono::steady_clock::now();
                while (g_mock_block_thread->joinable()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    auto elapsed = std::chrono::steady_clock::now() - start;
                    if (elapsed > std::chrono::seconds(3)) {
                        LogPrintf("MockBlockGen: Timeout waiting for thread, detaching...\n");
                        g_mock_block_thread->detach();
                        break;
                    }
                    // Check if thread finished
                    if (!g_mock_block_running) {
                        // Give it one more moment
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        if (g_mock_block_thread->joinable()) {
                            LogPrintf("MockBlockGen: Force joining thread...\n");
                            g_mock_block_thread->join();
                        }
                        break;
                    }
                }
            }
        } catch (const std::exception& e) {
            LogPrintf("MockBlockGen: Exception during shutdown: %s\n", e.what());
        }
        
        g_mock_block_thread.reset();
    }
    
    // Clear global pointers AFTER thread is stopped
    g_mock_mempool = nullptr;
    g_mock_chainman = nullptr;
    
    LogPrintf("MockBlockGen: Stopped\n");
}

bool IsMockBlockGeneratorRunning() {
    return g_mock_block_running;
}

bool IsMockBlockMode() {
    // Check if mock block mode is enabled via args
    return gArgs.GetArg("-mockblocktime", 0) > 0;
}

