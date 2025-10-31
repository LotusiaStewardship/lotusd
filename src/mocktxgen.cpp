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
#include <util/moneystr.h>
#include <validation.h>

#include <algorithm>

// Store keys for mock addresses
static std::vector<CKey> g_mock_keys;
static std::vector<CScript> g_mock_scripts;
static std::map<CScript, CKey> g_script_to_key;

// Cache of generated coinbase transactions for signing later
static std::map<TxId, CTransactionRef> g_coinbase_cache;
static RecursiveMutex cs_coinbase_cache;

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

std::vector<CTransactionRef> GenerateRandomTransactions(int count, int currentHeight) {
    InitMockAddresses();
    
    std::vector<CTransactionRef> txs;
    
    // Need at least 101 blocks for coinbase maturity (100 block maturity + 1)
    if (currentHeight <= 1100) {
        LogPrint(BCLog::NET, "MockTxGen: Too early, height %d (need > 1100)\n", currentHeight);
        return txs;
    }
    
    // Find spendable coinbase outputs from mature blocks (100+ blocks old)
    std::vector<COutPoint> spendableCoins;
    
    LogPrint(BCLog::NET, "MockTxGen: Searching for spendable coins from blocks %d to %d\n",
             currentHeight - 200, currentHeight - 101);
    
    {
        LOCK(cs_main);
        CCoinsViewCache& view = ::ChainstateActive().CoinsTip();
        
        // Look at blocks from 100-200 blocks ago
        for (int h = currentHeight - 200; h < currentHeight - 100 && h >= 0; h++) {
            CBlockIndex* pindex = ::ChainActive()[h];
            if (!pindex) continue;
            
            // Get the coinbase txid for this block
            CBlock block;
            if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
                continue;
            }
            
            if (block.vtx.empty()) continue;
            
            const CTransactionRef& coinbase = block.vtx[0];
            
            // Register this coinbase in our cache for future signing
            RegisterMockCoinbase(coinbase);
            
            // Check if outputs are unspent
            for (size_t i = 0; i < coinbase->vout.size(); i++) {
                COutPoint outpoint(coinbase->GetId(), i);
                Coin coin;
                if (view.GetCoin(outpoint, coin) && !coin.IsSpent()) {
                    // Skip OP_RETURN outputs
                    if (!coin.GetTxOut().scriptPubKey.IsUnspendable()) {
                        spendableCoins.push_back(outpoint);
                    }
                }
            }
        }
    }
    
    if (spendableCoins.empty()) {
        LogPrint(BCLog::NET, "MockTxGen: No spendable coins available yet\n");
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
        
        // Random number of outputs (1-5)
        int numOutputs = 1 + GetRand(4);
        mtx.vout.resize(numOutputs);
        
        // Distribute value randomly
        Amount remaining = inputValue - 1000 * SATOSHI; // Leave fee
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
        
        CTransactionRef tx = MakeTransactionRef(mtx);
        txs.push_back(tx);
        
        LogPrint(BCLog::NET, 
                 "MockTxGen: Created tx %s: 1 in → %d out, value %s XPI\n",
                 tx->GetId().ToString().substr(0, 16),
                 numOutputs,
                 FormatMoney(inputValue - 1000 * SATOSHI));
    }
    
    return txs;
}

