// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <httpexplorer.h>

#include <blockdb.h>
#include <chainparams.h>
#include <httpserver.h>
#include <primitives/block.h>
#include <rpc/blockchain.h>
#include <sync.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <validation.h>

#include <event2/http.h>

// Simple HTML block explorer page
static const char* EXPLORER_HTML = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <title>Lotus Block Explorer</title>
</head>
<body>
    <h1>🌸 Lotus Block Explorer</h1>
    <hr>
    
    <h2>Chain Info</h2>
    <div id="info">Loading...</div>
    
    <hr>
    <h2>Recent Blocks</h2>
    <table id="blocks" border="1" cellpadding="5">
        <tr><th>Height</th><th>Hash</th><th>Time</th><th>Txs</th></tr>
    </table>
    
    <script>
    async function update() {
        try {
            const r = await fetch('/explorer/api');
            const d = await r.json();
            document.getElementById('info').innerHTML = 
                '<b>Height:</b> ' + d.height + '<br>' +
                '<b>Hash:</b> ' + d.hash + '<br>' +
                '<b>Chain:</b> ' + d.chain;
            
            let html = '<tr><th>Height</th><th>Hash</th><th>Time</th><th>Txs</th></tr>';
            for (const b of d.blocks) {
                const t = new Date(b.time * 1000).toLocaleString();
                html += '<tr><td>' + b.height + '</td><td>' + 
                        b.hash.substring(0,20) + '...</td><td>' + 
                        t + '</td><td>' + b.txs + '</td></tr>';
            }
            document.getElementById('blocks').innerHTML = html;
        } catch(e) {
            document.getElementById('info').innerHTML = 'Error: ' + e;
        }
    }
    setInterval(update, 5000);
    update();
    </script>
</body>
</html>
)HTML";

// Handle explorer requests
static bool explorer_handler(Config &config, HTTPRequest* req, const std::string& path) {
    std::string endpoint = path.substr(10); // Remove "/explorer/"
    
    if (endpoint == "" || endpoint == "index.html") {
        req->WriteHeader("Content-Type", "text/html");
        req->WriteReply(HTTP_OK, EXPLORER_HTML);
        return true;
    }
    
    if (endpoint == "api") {
        try {
            LOCK(cs_main);
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
                if (ReadBlockFromDisk(block, idx, Params().GetConsensus())) {
                    UniValue b(UniValue::VOBJ);
                    b.pushKV("height", idx->nHeight);
                    b.pushKV("hash", idx->GetBlockHash().GetHex());
                    b.pushKV("time", idx->GetBlockTime());
                    b.pushKV("txs", (int)block.vtx.size());
                    blocks.push_back(b);
                }
            }
            result.pushKV("blocks", blocks);
            
            req->WriteHeader("Content-Type", "application/json");
            req->WriteReply(HTTP_OK, result.write());
            return true;
        } catch (const std::exception& e) {
            req->WriteReply(HTTP_INTERNAL, e.what());
            return true;
        }
    }
    
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
