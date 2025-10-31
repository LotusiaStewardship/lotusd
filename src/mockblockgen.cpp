// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <mockblockgen.h>

#include <chainparams.h>
#include <chain.h>
#include <config.h>
#include <consensus/merkle.h>
#include <consensus/validation.h>
#include <key_io.h>
#include <logging.h>
#include <miner.h>
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
static CTxMemPool *g_mock_mempool = nullptr;
static ChainstateManager *g_mock_chainman = nullptr;

/**
 * Generate a single block with minimal PoW for testing
 */
static bool GenerateMockBlock(const Config &config, const CScript &scriptPubKey) {
    if (!g_mock_mempool || !g_mock_chainman) {
        LogPrintf("MockBlockGen: Components not available\n");
        return false;
    }
    
    std::unique_ptr<CBlockTemplate> pblocktemplate(
        BlockAssembler(config, *g_mock_mempool).CreateNewBlock(scriptPubKey));

    if (!pblocktemplate) {
        LogPrintf("MockBlockGen: Failed to create block template\n");
        return false;
    }

    CBlock *pblock = &pblocktemplate->block;
    
    // Update block time
    {
        LOCK(cs_main);
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
    
    // If miner fund is enabled, we need to adjust outputs
    // For simplicity in mock mode, just set the single output
    pblock->vtx[0] = MakeTransactionRef(std::move(coinbase));
    
    // CRITICAL: Recalculate merkle root after modifying coinbase
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);
    
    // Set block size
    pblock->SetSize(GetSerializeSize(*pblock));
    
    // Submit the block (PoW already validated as false above, so it passes)
    std::shared_ptr<const CBlock> shared_pblock =
        std::make_shared<const CBlock>(*pblock);
    
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
    
    LogPrintf("🎲 Auto-generated block %d | Hash: %s\n",
              pblock->nHeight,
              pblock->GetHash().ToString().substr(0, 16) + "...");
    
    return true;
}

/**
 * Mock block generator thread
 */
static void MockBlockGeneratorThread(int interval_seconds, CScript scriptPubKey) {
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
    
    while (g_mock_block_running && !ShutdownRequested()) {
        // Randomize timing: interval ±1 second for network consensus
        // This prevents all nodes from generating at exact same time
        // Lowest hash wins (normal PoW consensus)
        const int random_offset = (std::rand() % 3) - 1;  // -1, 0, or +1
        const int actual_interval = interval_seconds + random_offset;
        
        LogPrint(BCLog::NET, 
                 "MockBlockGen: Next block in %d seconds (base: %d, offset: %+d)\n",
                 actual_interval, interval_seconds, random_offset);
        
        // Wait for the randomized interval
        for (int i = 0; i < actual_interval && g_mock_block_running && 
             !ShutdownRequested(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        if (!g_mock_block_running || ShutdownRequested()) {
            break;
        }
        
        // Generate a block - normal PoW consensus applies
        // If another node finds a better (lower) hash first, our block becomes orphan
        try {
            const int height_before = g_mock_chainman ? 
                g_mock_chainman->ActiveChain().Height() : -1;
            
            if (GenerateMockBlock(config, scriptPubKey)) {
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
    
    // Get a payout address from args or use a burn address
    std::string payoutAddr = gArgs.GetArg("-mockblockaddress", "");
    CScript scriptPubKey;
    
    if (payoutAddr.empty()) {
        // Use a standard burn address (OP_RETURN with data)
        // This makes coins unspendable but is standard
        scriptPubKey = CScript() << OP_RETURN << std::vector<uint8_t>{0x4d, 0x6f, 0x63, 0x6b}; // "Mock"
        LogPrintf("MockBlockGen: Using OP_RETURN burn script (rewards destroyed)\n");
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
    if (!g_mock_block_running) {
        return;
    }
    
    LogPrintf("MockBlockGen: Stopping...\n");
    g_mock_block_running = false;
    
    if (g_mock_block_thread && g_mock_block_thread->joinable()) {
        g_mock_block_thread->join();
    }
    
    g_mock_block_thread.reset();
    LogPrintf("MockBlockGen: Stopped\n");
}

bool IsMockBlockGeneratorRunning() {
    return g_mock_block_running;
}

bool IsMockBlockMode() {
    // Check if mock block mode is enabled via args
    return gArgs.GetArg("-mockblocktime", 0) > 0;
}

