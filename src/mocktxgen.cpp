// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <mocktxgen.h>

#include <blockdb.h>
#include <chainparams.h>
#include <coins.h>
#include <key.h>
#include <logging.h>
#include <policy/policy.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <pubkey.h>
#include <random.h>
#include <script/script.h>
#include <script/sign.h>
#include <script/signingprovider.h>
#include <script/standard.h>
#include <sync.h>
#include <txmempool.h>
#include <util/moneystr.h>
#include <validation.h>

#include <algorithm>

// External mempool reference (from mockblockgen)
extern CTxMemPool* g_mock_mempool;

// Store keys for mock addresses
static std::vector<CKey> g_mock_keys;
static std::vector<CScript> g_mock_scripts;
static std::map<CScript, CKey> g_script_to_key;

// Cache of generated coinbase transactions for signing later
static std::map<TxId, CTransactionRef> g_coinbase_cache;
static RecursiveMutex cs_coinbase_cache;

// Cache of recently spent outputs to prevent double-spending
static std::set<COutPoint> g_recently_spent_outputs;
static RecursiveMutex cs_spent_cache;
static const size_t MAX_SPENT_CACHE = 50000;

/**
 * Initialize mock addresses (call once)
 */
static void InitMockAddresses() {
    if (!g_mock_keys.empty()) {
        return;
    }
    
    // Generate 20 random addresses for transactions
    for (int i = 0; i < 20; i++) {
        CKey key;
        key.MakeNewKey(true);
        g_mock_keys.push_back(key);
        
        CPubKey pubkey = key.GetPubKey();
        CScript script = GetScriptForDestination(PKHash(pubkey));
        g_mock_scripts.push_back(script);
        g_script_to_key[script] = key;
    }
    
    LogPrint(BCLog::NET, "MockTxGen: Generated 20 mock key pairs\n");
}

/**
 * Get a random mock script for coinbase payout
 */
CScript GetRandomMockScript() {
    InitMockAddresses();
    if (g_mock_scripts.empty()) {
        return CScript();
    }
    return g_mock_scripts[GetRand(g_mock_scripts.size())];
}

/**
 * Get the FIRST script from mock key pool (for consistent coinbase)
 */
CScript GetFirstMockScript() {
    InitMockAddresses();
    if (g_mock_scripts.empty()) {
        return CScript();
    }
    return g_mock_scripts[0];
}

void RegisterMockCoinbase(const CTransactionRef& tx) {
    LOCK(cs_coinbase_cache);
    g_coinbase_cache[tx->GetId()] = tx;
    
    // Keep cache size reasonable - only keep last 200 blocks worth
    if (g_coinbase_cache.size() > 200) {
        // Remove oldest entry
        auto it = g_coinbase_cache.begin();
        g_coinbase_cache.erase(it);
    }
}

void ClearSpentOutputsCache() {
    LOCK(cs_spent_cache);
    LogPrint(BCLog::NET, "MockTxGen: Clearing spent outputs cache (%d entries)\n", 
             g_recently_spent_outputs.size());
    g_recently_spent_outputs.clear();
}

std::vector<CTransactionRef> GenerateRandomTransactions(int count, int currentHeight) {
    InitMockAddresses();
    
    std::vector<CTransactionRef> txs;
    
    // Get fork height to avoid spending pre-fork coins (they don't have our keys)
    int forkHeight = gArgs.GetArg("-testnetforkheight", 0);
    if (forkHeight == 0) {
        LogPrint(BCLog::NET, "MockTxGen: No fork height set\n");
        return txs;
    }
    
    // In mock mode, we can spend coinbases immediately (maturity checks are bypassed)
    // Just need at least 2 blocks after fork (fork block + 1 coinbase to spend)
    int minHeight = forkHeight + 2;
    if (currentHeight <= minHeight) {
        LogPrint(BCLog::NET, "MockTxGen: Too early, height %d (need > %d)\n", currentHeight, minHeight);
        return txs;
    }
    
    // Find spendable coinbase outputs from recent blocks (no maturity needed in mock mode!)
    std::vector<COutPoint> spendableCoins;
    
    // Build set of outpoints already spent in mempool
    std::set<COutPoint> mempoolSpentCoins;
    if (g_mock_mempool) {
        LOCK(g_mock_mempool->cs);
        for (const auto& entry : g_mock_mempool->mapTx) {
            const CTransactionRef& tx = entry.GetSharedTx();
            for (const auto& txin : tx->vin) {
                mempoolSpentCoins.insert(txin.prevout);
            }
        }
    }
    
    // Search for spendable coins from recent coinbases
    // Only look at blocks AFTER fork height (pre-fork blocks don't have our keys!)
    // In mock mode, look at last 50 blocks (no maturity requirement)
    int searchStart = std::max(forkHeight + 1, currentHeight - 50);
    int searchEnd = currentHeight - 1;  // Can spend coinbase from previous block!
    
    if (searchStart > searchEnd) {
        LogPrint(BCLog::NET, "MockTxGen: No mock blocks yet (fork at %d, height %d)\n", 
                forkHeight, currentHeight);
        return txs;
    }
    
    LogPrint(BCLog::NET, "MockTxGen: Searching for spendable coins from blocks %d to %d (%d already in mempool)\n",
             searchStart, searchEnd, (int)mempoolSpentCoins.size());
    
    {
        LOCK(cs_main);
        CCoinsViewCache& view = ::ChainstateActive().CoinsTip();
        
        // Look at recent blocks (only after fork height)
        for (int h = searchStart; h <= searchEnd && h >= 0; h++) {
            CBlockIndex* pindex = ::ChainActive()[h];
            if (!pindex) continue;
            
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
                continue;
            }
            
            if (block.vtx.empty()) continue;
            
            // Register coinbase for cache
            const CTransactionRef& coinbase = block.vtx[0];
            RegisterMockCoinbase(coinbase);
            
            // Check ALL transactions in the block (not just coinbase)
            for (const CTransactionRef& tx : block.vtx) {
                // Register all transactions in cache for signing
                if (tx->GetId() != coinbase->GetId()) {
                    RegisterMockCoinbase(tx); // Name is misleading, but it works for any tx
                }
                
                for (size_t i = 0; i < tx->vout.size(); i++) {
                    COutPoint outpoint(tx->GetId(), i);
                    
                    // Skip if already being spent in mempool
                    if (mempoolSpentCoins.count(outpoint) > 0) {
                        continue;
                    }
                    
                    // Skip if recently spent by us
                    {
                        LOCK(cs_spent_cache);
                        if (g_recently_spent_outputs.count(outpoint) > 0) {
                            continue;
                        }
                    }
                    
                    Coin coin;
                    if (view.GetCoin(outpoint, coin) && !coin.IsSpent()) {
                        // Skip OP_RETURN outputs
                        if (coin.GetTxOut().scriptPubKey.IsUnspendable()) {
                            continue;
                        }
                        
                        // Check if this output is spendable by our mock keys
                        const CScript& scriptPubKey = coin.GetTxOut().scriptPubKey;
                        if (g_script_to_key.find(scriptPubKey) != g_script_to_key.end()) {
                            spendableCoins.push_back(outpoint);
                        }
                    }
                }
            }
        }
    }
    
    if (spendableCoins.empty()) {
        LogPrint(BCLog::NET, "MockTxGen: No spendable coins available (all in use or spent)\n");
        return txs;
    }
    
    // Generate random transactions
    for (int i = 0; i < count && !spendableCoins.empty(); i++) {
        // Pick a random input
        int inputIdx = GetRand(spendableCoins.size());
        COutPoint input = spendableCoins[inputIdx];
        spendableCoins.erase(spendableCoins.begin() + inputIdx);
        
        // Get the coin value
        Amount inputValue;
        {
            LOCK(cs_main);
            Coin coin;
            if (!::ChainstateActive().CoinsTip().GetCoin(input, coin)) {
                continue;
            }
            inputValue = coin.GetTxOut().nValue;
        }
        
        // Create transaction
        CMutableTransaction mtx;
        mtx.nVersion = 2;
        mtx.vin.resize(1);
        mtx.vin[0].prevout = input;
        
        // Random number of outputs (1-50 for maximum chaos!)
        int numOutputs = 1 + GetRand(50);
        mtx.vout.resize(numOutputs);
        
        // Estimate transaction size for fee calculation
        // Rough formula: 10 (version/locktime) + 1 (input count) + 148 (per input) + 
        //                1 (output count) + 34*numOutputs (per output)
        int estimatedSize = 10 + 1 + 148 + 1 + (34 * numOutputs);
        
        // Fee rate: 10 sat/byte (generous for mock testing)
        int64_t feeAmount = estimatedSize * 10;
        
        // Distribute value randomly, leaving fee
        Amount remaining = inputValue - feeAmount * SATOSHI;
        
        // Ensure we have enough value left after fee
        if (remaining < (1000 * numOutputs * SATOSHI)) {
            LogPrint(BCLog::NET, "MockTxGen: Input value too low for %d outputs (value=%d, fee=%d)\n",
                    numOutputs, inputValue / SATOSHI, feeAmount);
            continue;
        }
        
        for (int j = 0; j < numOutputs - 1; j++) {
            int64_t remainingInt = remaining / SATOSHI;
            int64_t shareInt = (remainingInt / (numOutputs - j)) * GetRand(100) / 100;
            Amount share = shareInt * SATOSHI;
            share = std::max(share, 1000 * SATOSHI); // Minimum 1000 satoshi
            Amount minForRemaining = (numOutputs - j - 1) * 1000 * SATOSHI;
            share = std::min(share, remaining - minForRemaining);
            
            int addrIdx = GetRand(g_mock_scripts.size());
            mtx.vout[j].nValue = share;
            mtx.vout[j].scriptPubKey = g_mock_scripts[addrIdx];
            remaining -= share;
        }
        
        // Last output gets remainder
        int addrIdx = GetRand(g_mock_scripts.size());
        mtx.vout[numOutputs - 1].nValue = remaining;
        mtx.vout[numOutputs - 1].scriptPubKey = g_mock_scripts[addrIdx];
        
        // Get the coin and construct a minimal previous transaction for signing
        Coin prevCoin;
        CScript scriptPubKey;
        CTransactionRef prevTxRef;
        
        {
            LOCK(cs_main);
            
            // Get the coin
            if (!::ChainstateActive().CoinsTip().GetCoin(input, prevCoin)) {
                LogPrint(BCLog::NET, "MockTxGen: Failed to get prev coin\n");
                continue;
            }
            scriptPubKey = prevCoin.GetTxOut().scriptPubKey;
        }
        
        // Try to get the previous transaction from our cache
        {
            LOCK(cs_coinbase_cache);
            auto it = g_coinbase_cache.find(input.GetTxId());
            if (it != g_coinbase_cache.end()) {
                prevTxRef = it->second;
            }
        }
        
        if (!prevTxRef) {
            LogPrint(BCLog::NET, "MockTxGen: Prev transaction not in cache (txid=%s)\n",
                     input.GetTxId().ToString());
            continue;
        }
        
        // Verify the output exists in the prev tx
        if (input.GetN() >= prevTxRef->vout.size()) {
            LogPrint(BCLog::NET, "MockTxGen: Output %d doesn't exist in prev tx (has %d outputs)\n",
                     input.GetN(), prevTxRef->vout.size());
            continue;
        }
        
        // Verify the scriptPubKey matches
        if (prevTxRef->vout[input.GetN()].scriptPubKey != scriptPubKey) {
            LogPrint(BCLog::NET, "MockTxGen: ScriptPubKey mismatch!\n");
            continue;
        }
        
        // Find the key for this script
        auto keyIt = g_script_to_key.find(scriptPubKey);
        if (keyIt == g_script_to_key.end()) {
            // Not our key - skip
            LogPrint(BCLog::NET, "MockTxGen: Coin not from our keys, skipping\n");
            continue;
        }
        
        const CKey& key = keyIt->second;
        
        // Verify key is valid
        if (!key.IsValid()) {
            LogPrint(BCLog::NET, "MockTxGen: Invalid key!\n");
            continue;
        }
        
        // Create signing provider
        FillableSigningProvider provider;
        provider.AddKey(key);
        
        // Prepare spent outputs for Lotus sighash
        // For Lotus, we need to provide the actual spent output (not the whole prev tx)
        std::vector<CTxOut> spent_outputs;
        spent_outputs.push_back(prevTxRef->vout[input.GetN()]);
        
        // Create precomputed transaction data with spent outputs
        const PrecomputedTransactionData txdata(mtx, std::move(spent_outputs));
        
        LogPrint(BCLog::NET, "MockTxGen: Attempting to sign input spending %s:%d\n",
                 input.GetTxId().ToString(), input.GetN());
        
        // Sign with SIGHASH_LOTUS | SIGHASH_FORKID | SIGHASH_ALL
        SigHashType sigHashType = SigHashType().withLotus().withForkId();
        
        // Use the PrecomputedTransactionData overload for Lotus signing
        if (!SignSignature(provider, txdata, mtx, 0, sigHashType)) {
            LogPrint(BCLog::NET, "MockTxGen: Failed to sign transaction (key issue?)\n");
            continue;
        }
        
        // Mark this output as spent in our cache
        {
            LOCK(cs_spent_cache);
            g_recently_spent_outputs.insert(input);
            
            // Limit cache size
            if (g_recently_spent_outputs.size() > MAX_SPENT_CACHE) {
                // Remove oldest entries (simple approach: clear half)
                auto it = g_recently_spent_outputs.begin();
                std::advance(it, MAX_SPENT_CACHE / 2);
                g_recently_spent_outputs.erase(g_recently_spent_outputs.begin(), it);
            }
        }
        
        CTransactionRef tx = MakeTransactionRef(mtx);
        txs.push_back(tx);
        
        // Calculate actual fee paid
        Amount totalOut = Amount::zero();
        for (const auto& out : mtx.vout) {
            totalOut += out.nValue;
        }
        Amount actualFee = inputValue - totalOut;
        
        LogPrint(BCLog::NET, 
                 "MockTxGen: Created tx %s: 1 in → %d out, value %.3f XPI, fee %d sat (~%.1f sat/byte)\n",
                 tx->GetId().ToString().substr(0, 16),
                 numOutputs,
                 (double)(totalOut / SATOSHI) / (COIN / SATOSHI),
                 actualFee / SATOSHI,
                 (double)(actualFee / SATOSHI) / estimatedSize);
    }
    
    return txs;
}

