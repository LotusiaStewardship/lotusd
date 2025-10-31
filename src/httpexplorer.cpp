// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <httpexplorer.h>

#include <blockdb.h>
#include <chainparams.h>
#include <core_io.h>
#include <httpserver.h>
#include <key_io.h>
#include <primitives/block.h>
#include <rpc/blockchain.h>
#include <rpc/util.h>
#include <script/standard.h>
#include <sync.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <validation.h>

#include <event2/http.h>

// Include generated explorer resources
#include <explorer_resources.h>

// Explorer cache to avoid repeated disk reads
struct ExplorerCache {
    RecursiveMutex cs_cache;
    
    // Transaction cache: txid -> (tx, blockHash)
    std::map<TxId, std::pair<CTransactionRef, BlockHash>> txCache;
    
    // Block cache: hash -> block
    std::map<BlockHash, CBlock> blockCache;
    
    // Block index cache for quick height lookup
    std::map<int, BlockHash> heightToHashCache;
    
    static const size_t MAX_TX_CACHE = 10000;
    static const size_t MAX_BLOCK_CACHE = 500;
    
    void CacheTx(const TxId& txid, CTransactionRef tx, const BlockHash& blockHash) {
        LOCK(cs_cache);
        if (txCache.size() >= MAX_TX_CACHE) {
            // Remove oldest entry (simple FIFO eviction)
            txCache.erase(txCache.begin());
        }
        txCache[txid] = std::make_pair(tx, blockHash);
    }
    
    bool GetCachedTx(const TxId& txid, CTransactionRef& tx, BlockHash& blockHash) {
        LOCK(cs_cache);
        auto it = txCache.find(txid);
        if (it != txCache.end()) {
            tx = it->second.first;
            blockHash = it->second.second;
            return true;
        }
        return false;
    }
    
    void CacheBlock(const BlockHash& hash, const CBlock& block) {
        LOCK(cs_cache);
        if (blockCache.size() >= MAX_BLOCK_CACHE) {
            blockCache.erase(blockCache.begin());
        }
        blockCache[hash] = block;
    }
    
    bool GetCachedBlock(const BlockHash& hash, CBlock& block) {
        LOCK(cs_cache);
        auto it = blockCache.find(hash);
        if (it != blockCache.end()) {
            block = it->second;
            return true;
        }
        return false;
    }
    
    void CacheHeight(int height, const BlockHash& hash) {
        LOCK(cs_cache);
        heightToHashCache[height] = hash;
    }
};

static ExplorerCache g_explorerCache;

// Helper function to find transaction with caching
static CTransactionRef FindTransaction(const TxId& txid, BlockHash& hashBlock) {
    // Check cache first
    CTransactionRef tx;
    if (g_explorerCache.GetCachedTx(txid, tx, hashBlock)) {
        return tx;
    }
    
    LOCK(cs_main);
    
    // Try GetTransaction (uses txindex if enabled or searches mempool)
    tx = GetTransaction(nullptr, nullptr, txid, Params().GetConsensus(), hashBlock);
    
    // If not found, search all blocks (not just recent ones)
    if (!tx) {
        CChain& active_chain = ChainActive();
        int totalHeight = active_chain.Height();
        
        // Search from newest to oldest (more likely to find recent transactions)
        for (int i = 0; i <= totalHeight; i++) {
            CBlockIndex* pindex = active_chain[totalHeight - i];
            if (!pindex) continue;
            
            CBlock block;
            // Try cache first
            if (!g_explorerCache.GetCachedBlock(pindex->GetBlockHash(), block)) {
                if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
                    continue;
                }
                // Cache the block for future use
                g_explorerCache.CacheBlock(pindex->GetBlockHash(), block);
            }
            
            // Search transactions in this block
            for (const auto& blockTx : block.vtx) {
                if (blockTx->GetId() == txid) {
                    tx = blockTx;
                    hashBlock = pindex->GetBlockHash();
                    // Cache all transactions from this block
                    for (const auto& cacheTx : block.vtx) {
                        g_explorerCache.CacheTx(cacheTx->GetId(), cacheTx, hashBlock);
                    }
                    return tx;
                }
            }
        }
    }
    
    // Cache the result if found
    if (tx) {
        g_explorerCache.CacheTx(txid, tx, hashBlock);
    }
    
    return tx;
}

// Helper function to decode address from scriptPubKey
static std::string ScriptPubKeyToAddress(const CScript& scriptPubKey, const CChainParams& params) {
    CTxDestination dest;
    if (ExtractDestination(scriptPubKey, dest)) {
        return EncodeDestination(dest, params);
    }
    return ""; // Non-standard or undecodable script
}

// Helper function to get script type name
static std::string GetScriptTypeName(const CScript& scriptPubKey) {
    std::vector<std::vector<uint8_t>> vSolutions;
    TxoutType type = Solver(scriptPubKey, vSolutions);
    return GetTxnOutputType(type);
}

// Handle explorer requests  
static bool explorer_handler(Config &config, HTTPRequest* req, const std::string& path) {
    LogPrint(BCLog::HTTP, "Explorer: Handling request for path='%s'\n", path);
    
    std::string endpoint = path;
    
    LogPrint(BCLog::HTTP, "Explorer: Endpoint='%s'\n", endpoint);
    
    // Serve HTML
    if (endpoint.empty() || endpoint == "index.html") {
        req->WriteHeader("Content-Type", "text/html; charset=utf-8");
        req->WriteReply(HTTP_OK, explorer_resources::HTML);
        return true;
    }
    
    // Serve CSS
    if (endpoint == "style.css") {
        req->WriteHeader("Content-Type", "text/css; charset=utf-8");
        req->WriteReply(HTTP_OK, explorer_resources::CSS);
        return true;
    }
    
    // Serve JS
    if (endpoint == "script.js") {
        req->WriteHeader("Content-Type", "application/javascript; charset=utf-8");
        req->WriteReply(HTTP_OK, explorer_resources::JS);
        return true;
    }
    
    // API: Chain info + recent blocks
    if (endpoint == "api") {
        try {
            LOCK(cs_main);
            
            if (!::ChainActive().Tip()) {
                UniValue error(UniValue::VOBJ);
                error.pushKV("error", "Chain not active");
                req->WriteHeader("Content-Type", "application/json; charset=utf-8");
                req->WriteReply(HTTP_OK, error.write());
                return true;
            }
            
            UniValue result(UniValue::VOBJ);
            result.pushKV("height", ::ChainActive().Height());
            result.pushKV("hash", ::ChainActive().Tip()->GetBlockHash().GetHex());
            result.pushKV("chain", Params().NetworkIDString());
            
            UniValue blocks(UniValue::VARR);
            int h = ::ChainActive().Height();
            for (int i = 0; i < 20 && h >= 0; i++, h--) {
                CBlockIndex* idx = ::ChainActive()[h];
                if (!idx) break;
                
                CBlock block;
                // Try cache first
                if (!g_explorerCache.GetCachedBlock(idx->GetBlockHash(), block)) {
                    if (!ReadBlockFromDisk(block, idx, Params().GetConsensus())) {
                        continue;
                    }
                    g_explorerCache.CacheBlock(idx->GetBlockHash(), block);
                }
                
                UniValue b(UniValue::VOBJ);
                b.pushKV("height", idx->nHeight);
                b.pushKV("hash", idx->GetBlockHash().GetHex());
                b.pushKV("time", idx->GetBlockTime());
                b.pushKV("txs", (int)block.vtx.size());
                blocks.push_back(b);
            }
            result.pushKV("blocks", blocks);
            
            req->WriteHeader("Content-Type", "application/json; charset=utf-8");
            req->WriteReply(HTTP_OK, result.write());
            return true;
        } catch (const std::exception& e) {
            LogPrintf("Explorer: Exception in API handler: %s\n", e.what());
            UniValue error(UniValue::VOBJ);
            error.pushKV("error", std::string("Exception: ") + e.what());
            req->WriteHeader("Content-Type", "application/json; charset=utf-8");
            req->WriteReply(HTTP_INTERNAL, error.write());
            return true;
        }
    }
    
    // API: Block details by hash
    if (endpoint.substr(0, 6) == "block/") {
        std::string hashStr = endpoint.substr(6);
        try {
            LOCK(cs_main);
            
            BlockHash hash = BlockHash::fromHex(hashStr);
            CBlockIndex* pindex = LookupBlockIndex(hash);
            
            if (!pindex) {
                UniValue error(UniValue::VOBJ);
                error.pushKV("error", "Block not found");
                req->WriteHeader("Content-Type", "application/json; charset=utf-8");
                req->WriteReply(HTTP_OK, error.write());
                return true;
            }
            
            CBlock block;
            // Try cache first
            if (!g_explorerCache.GetCachedBlock(hash, block)) {
                if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus())) {
                    UniValue error(UniValue::VOBJ);
                    error.pushKV("error", "Failed to read block");
                    req->WriteHeader("Content-Type", "application/json; charset=utf-8");
                    req->WriteReply(HTTP_OK, error.write());
                    return true;
                }
                g_explorerCache.CacheBlock(hash, block);
                // Cache all transactions from this block
                for (const auto& tx : block.vtx) {
                    g_explorerCache.CacheTx(tx->GetId(), tx, hash);
                }
            }
            
            UniValue result(UniValue::VOBJ);
            result.pushKV("height", pindex->nHeight);
            result.pushKV("hash", pindex->GetBlockHash().GetHex());
            result.pushKV("previousblockhash", pindex->pprev ? pindex->pprev->GetBlockHash().GetHex() : "");
            result.pushKV("merkleroot", block.hashMerkleRoot.GetHex());
            result.pushKV("time", pindex->GetBlockTime());
            result.pushKV("difficulty", GetDifficulty(pindex));
            result.pushKV("nonce", (uint64_t)block.nNonce);
            result.pushKV("size", (int)::GetSerializeSize(block));
            
            UniValue txs(UniValue::VARR);
            for (const auto& tx : block.vtx) {
                UniValue txObj(UniValue::VOBJ);
                txObj.pushKV("txid", tx->GetId().GetHex());
                
                UniValue vins(UniValue::VARR);
                for (const auto& in : tx->vin) {
                    UniValue vinObj(UniValue::VOBJ);
                    if (tx->IsCoinBase()) {
                        vinObj.pushKV("coinbase", HexStr(in.scriptSig));
                    } else {
                        vinObj.pushKV("txid", in.prevout.GetTxId().GetHex());
                        vinObj.pushKV("vout", (int)in.prevout.GetN());
                        UniValue scriptSig(UniValue::VOBJ);
                        scriptSig.pushKV("hex", HexStr(in.scriptSig));
                        scriptSig.pushKV("asm", ScriptToAsmStr(in.scriptSig, true));
                        vinObj.pushKV("scriptSig", scriptSig);
                    }
                    vins.push_back(vinObj);
                }
                txObj.pushKV("vin", vins);
                
                UniValue vouts(UniValue::VARR);
                for (size_t i = 0; i < tx->vout.size(); i++) {
                    const auto& out = tx->vout[i];
                    UniValue voutObj(UniValue::VOBJ);
                    voutObj.pushKV("n", (int)i);
                    // Convert to XPI properly: amount is in satoshis, COIN = 1,000,000 satoshis
                    voutObj.pushKV("value", (double)(out.nValue / SATOSHI) / (COIN / SATOSHI));
                    
                    UniValue scriptPubKey(UniValue::VOBJ);
                    scriptPubKey.pushKV("type", GetScriptTypeName(out.scriptPubKey));
                    scriptPubKey.pushKV("hex", HexStr(out.scriptPubKey));
                    
                    // Decode address if possible
                    std::string address = ScriptPubKeyToAddress(out.scriptPubKey, Params());
                    if (!address.empty()) {
                        scriptPubKey.pushKV("address", address);
                    }
                    
                    voutObj.pushKV("scriptPubKey", scriptPubKey);
                    
                    vouts.push_back(voutObj);
                }
                txObj.pushKV("vout", vouts);
                
                txs.push_back(txObj);
            }
            result.pushKV("tx", txs);
            
            req->WriteHeader("Content-Type", "application/json; charset=utf-8");
            req->WriteReply(HTTP_OK, result.write());
            return true;
        } catch (const std::exception& e) {
            LogPrintf("Explorer: Exception in block handler: %s\n", e.what());
            UniValue error(UniValue::VOBJ);
            error.pushKV("error", std::string("Exception: ") + e.what());
            req->WriteHeader("Content-Type", "application/json; charset=utf-8");
            req->WriteReply(HTTP_INTERNAL, error.write());
            return true;
        }
    }
    
    // API: Transaction details by txid
    if (endpoint.substr(0, 3) == "tx/") {
        std::string txidStr = endpoint.substr(3);
        try {
            uint256 txid;
            txid.SetHex(txidStr);
            BlockHash hashBlock;
            
            // Use cached FindTransaction which searches all blocks
            CTransactionRef tx = FindTransaction(TxId(txid), hashBlock);
            
            if (!tx) {
                UniValue error(UniValue::VOBJ);
                error.pushKV("error", "Transaction not found");
                req->WriteHeader("Content-Type", "application/json; charset=utf-8");
                req->WriteReply(HTTP_OK, error.write());
                return true;
            }
            
            UniValue result(UniValue::VOBJ);
            result.pushKV("txid", tx->GetId().GetHex());
            result.pushKV("version", (int)tx->nVersion);
            result.pushKV("locktime", (int)tx->nLockTime);
            result.pushKV("size", (int)::GetSerializeSize(*tx));
            
            UniValue vins(UniValue::VARR);
            for (const auto& in : tx->vin) {
                UniValue vinObj(UniValue::VOBJ);
                if (tx->IsCoinBase()) {
                    vinObj.pushKV("coinbase", HexStr(in.scriptSig));
                } else {
                    vinObj.pushKV("txid", in.prevout.GetTxId().GetHex());
                    vinObj.pushKV("vout", (int)in.prevout.GetN());
                    UniValue scriptSig(UniValue::VOBJ);
                    scriptSig.pushKV("hex", HexStr(in.scriptSig));
                    scriptSig.pushKV("asm", ScriptToAsmStr(in.scriptSig, true));
                    vinObj.pushKV("scriptSig", scriptSig);
                }
                vins.push_back(vinObj);
            }
            result.pushKV("vin", vins);
            
            UniValue vouts(UniValue::VARR);
            for (size_t i = 0; i < tx->vout.size(); i++) {
                const auto& out = tx->vout[i];
                UniValue voutObj(UniValue::VOBJ);
                voutObj.pushKV("n", (int)i);
                // Convert to XPI properly: amount is in satoshis, COIN = 1,000,000 satoshis
                voutObj.pushKV("value", (double)(out.nValue / SATOSHI) / (COIN / SATOSHI));
                
                UniValue scriptPubKey(UniValue::VOBJ);
                scriptPubKey.pushKV("type", GetScriptTypeName(out.scriptPubKey));
                scriptPubKey.pushKV("hex", HexStr(out.scriptPubKey));
                
                // Decode address if possible
                std::string address = ScriptPubKeyToAddress(out.scriptPubKey, Params());
                if (!address.empty()) {
                    scriptPubKey.pushKV("address", address);
                }
                
                voutObj.pushKV("scriptPubKey", scriptPubKey);
                
                vouts.push_back(voutObj);
            }
            result.pushKV("vout", vouts);
            
            req->WriteHeader("Content-Type", "application/json; charset=utf-8");
            req->WriteReply(HTTP_OK, result.write());
            return true;
        } catch (const std::exception& e) {
            LogPrintf("Explorer: Exception in tx handler: %s\n", e.what());
            UniValue error(UniValue::VOBJ);
            error.pushKV("error", std::string("Exception: ") + e.what());
            req->WriteHeader("Content-Type", "application/json; charset=utf-8");
            req->WriteReply(HTTP_INTERNAL, error.write());
            return true;
        }
    }
    
    LogPrint(BCLog::HTTP, "Explorer: Unknown endpoint\n");
    req->WriteReply(HTTP_NOTFOUND, "Not found");
    return true;
}

bool InitHTTPExplorer() {
    const int explorerPort = gArgs.GetArg("-explorerport", 0);
    
    if (explorerPort == 0) {
        return true;
    }
    
    RegisterHTTPHandler("/explorer/", false, explorer_handler);
    
    const int rpcPort = gArgs.GetArg("-rpcport", BaseParams().RPCPort());
    LogPrintf("🌸 Block explorer: http://localhost:%d/explorer/\n", rpcPort);
    
    return true;
}

void InterruptHTTPExplorer() {
    // Nothing to do
}

void StopHTTPExplorer() {
    UnregisterHTTPHandler("/explorer/", false);
}
