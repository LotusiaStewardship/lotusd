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

// Modern HTML5 block explorer page with UTF-8 support
static const char* EXPLORER_HTML = R"HTML(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>🌸 Lotus Block Explorer</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Helvetica Neue', Arial, sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
            color: #333;
        }
        .container {
            max-width: 1400px;
            margin: 0 auto;
        }
        .header {
            background: white;
            border-radius: 15px;
            padding: 30px;
            margin-bottom: 30px;
            box-shadow: 0 10px 40px rgba(0,0,0,0.2);
            animation: slideDown 0.5s ease-out;
        }
        @keyframes slideDown {
            from { opacity: 0; transform: translateY(-20px); }
            to { opacity: 1; transform: translateY(0); }
        }
        h1 {
            font-size: 2.5em;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            background-clip: text;
            margin-bottom: 10px;
        }
        .subtitle {
            color: #666;
            font-size: 1.1em;
        }
        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 20px;
            margin-bottom: 30px;
        }
        .stat-card {
            background: white;
            border-radius: 15px;
            padding: 25px;
            box-shadow: 0 5px 20px rgba(0,0,0,0.1);
            transition: transform 0.3s ease, box-shadow 0.3s ease;
            animation: fadeIn 0.5s ease-out;
        }
        @keyframes fadeIn {
            from { opacity: 0; transform: scale(0.95); }
            to { opacity: 1; transform: scale(1); }
        }
        .stat-card:hover {
            transform: translateY(-5px);
            box-shadow: 0 10px 30px rgba(0,0,0,0.2);
        }
        .stat-label {
            color: #888;
            font-size: 0.9em;
            text-transform: uppercase;
            letter-spacing: 1px;
            margin-bottom: 10px;
        }
        .stat-value {
            font-size: 1.8em;
            font-weight: bold;
            color: #667eea;
            word-break: break-all;
        }
        .blocks-container {
            background: white;
            border-radius: 15px;
            padding: 30px;
            box-shadow: 0 10px 40px rgba(0,0,0,0.2);
            animation: fadeIn 0.7s ease-out;
        }
        h2 {
            font-size: 1.8em;
            margin-bottom: 20px;
            color: #333;
        }
        .block-table {
            width: 100%;
            border-collapse: separate;
            border-spacing: 0 10px;
        }
        .block-table thead th {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 15px;
            text-align: left;
            font-weight: 600;
            text-transform: uppercase;
            font-size: 0.85em;
            letter-spacing: 1px;
        }
        .block-table thead th:first-child { border-radius: 10px 0 0 10px; }
        .block-table thead th:last-child { border-radius: 0 10px 10px 0; }
        .block-table tbody tr {
            background: #f8f9fa;
            transition: all 0.3s ease;
        }
        .block-table tbody tr:hover {
            background: linear-gradient(to right, #f0f2ff, #fff);
            transform: scale(1.01);
            box-shadow: 0 5px 15px rgba(102, 126, 234, 0.2);
        }
        .block-table tbody td {
            padding: 15px;
            border: none;
        }
        .block-table tbody tr td:first-child { border-radius: 10px 0 0 10px; }
        .block-table tbody tr td:last-child { border-radius: 0 10px 10px 0; }
        .block-height {
            font-weight: bold;
            color: #667eea;
            font-size: 1.1em;
        }
        .block-hash {
            font-family: 'Courier New', monospace;
            color: #666;
            font-size: 0.9em;
        }
        .block-time {
            color: #888;
        }
        .block-txs {
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 5px 15px;
            border-radius: 20px;
            font-weight: bold;
            display: inline-block;
        }
        .loading {
            text-align: center;
            padding: 40px;
            color: #667eea;
            font-size: 1.2em;
        }
        .error {
            background: #ff6b6b;
            color: white;
            padding: 20px;
            border-radius: 10px;
            margin: 20px 0;
        }
        .update-indicator {
            position: fixed;
            top: 20px;
            right: 20px;
            background: #4ade80;
            color: white;
            padding: 10px 20px;
            border-radius: 25px;
            font-weight: bold;
            box-shadow: 0 5px 15px rgba(74, 222, 128, 0.4);
            opacity: 0;
            transition: opacity 0.3s ease;
        }
        .update-indicator.show {
            opacity: 1;
        }
        @media (max-width: 768px) {
            h1 { font-size: 1.8em; }
            .stat-value { font-size: 1.3em; }
            .block-table { font-size: 0.85em; }
        }
    </style>
</head>
<body>
    <div class="update-indicator" id="updateIndicator">🔄 Updated</div>
    
    <div class="container">
        <div class="header">
            <h1>🌸 Lotus Block Explorer</h1>
            <div class="subtitle">Real-time blockchain monitoring</div>
        </div>
        
        <div class="stats-grid" id="stats">
            <div class="stat-card">
                <div class="stat-label">⛓️ Block Height</div>
                <div class="stat-value" id="height">-</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">🔗 Best Block Hash</div>
                <div class="stat-value" id="hash" style="font-size: 0.8em;">-</div>
            </div>
            <div class="stat-card">
                <div class="stat-label">🌐 Network</div>
                <div class="stat-value" id="chain">-</div>
            </div>
        </div>
        
        <div class="blocks-container">
            <h2>📦 Recent Blocks</h2>
            <table class="block-table">
                <thead>
                    <tr>
                        <th>Height</th>
                        <th>Block Hash</th>
                        <th>Timestamp</th>
                        <th>Transactions</th>
                    </tr>
                </thead>
                <tbody id="blocks">
                    <tr><td colspan="4" class="loading">⏳ Loading blocks...</td></tr>
                </tbody>
            </table>
        </div>
    </div>
    
    <script>
        let lastHeight = null;
        
        function showUpdate() {
            const indicator = document.getElementById('updateIndicator');
            indicator.classList.add('show');
            setTimeout(() => indicator.classList.remove('show'), 2000);
        }
        
        function formatTime(timestamp) {
            const date = new Date(timestamp * 1000);
            const now = new Date();
            const diff = Math.floor((now - date) / 1000);
            
            if (diff < 60) return `${diff}s ago`;
            if (diff < 3600) return `${Math.floor(diff / 60)}m ago`;
            if (diff < 86400) return `${Math.floor(diff / 3600)}h ago`;
            
            return date.toLocaleString('en-US', {
                month: 'short',
                day: 'numeric',
                hour: '2-digit',
                minute: '2-digit'
            });
        }
        
        async function update() {
            try {
                const r = await fetch('/explorer/api');
                const d = await r.json();
                
                if (d.error) {
                    document.getElementById('blocks').innerHTML = 
                        '<tr><td colspan="4" class="error">❌ Error: ' + d.error + '</td></tr>';
                    return;
                }
                
                // Update stats with animation if height changed
                if (lastHeight !== null && d.height > lastHeight) {
                    showUpdate();
                }
                lastHeight = d.height;
                
                document.getElementById('height').textContent = d.height.toLocaleString();
                document.getElementById('hash').textContent = d.hash;
                document.getElementById('chain').textContent = d.chain;
                
                // Update blocks table
                let html = '';
                for (const b of d.blocks) {
                    html += '<tr>' +
                        '<td class="block-height">🧱 ' + b.height.toLocaleString() + '</td>' +
                        '<td class="block-hash">' + b.hash.substring(0, 32) + '...</td>' +
                        '<td class="block-time">🕐 ' + formatTime(b.time) + '</td>' +
                        '<td><span class="block-txs">💸 ' + b.txs + ' tx</span></td>' +
                        '</tr>';
                }
                document.getElementById('blocks').innerHTML = html;
            } catch(e) {
                document.getElementById('blocks').innerHTML = 
                    '<tr><td colspan="4" class="error">❌ Connection error: ' + e.message + '</td></tr>';
            }
        }
        
        // Update every 3 seconds
        setInterval(update, 3000);
        update();
    </script>
</body>
</html>
)HTML";

// Handle explorer requests  
static bool explorer_handler(Config &config, HTTPRequest* req, const std::string& path) {
    LogPrint(BCLog::HTTP, "Explorer: Handling request for path='%s'\n", path);
    
    // The path parameter has "/explorer/" already stripped by http_request_cb
    // So path is empty for /explorer/, or "api" for /explorer/api
    std::string endpoint = path;
    
    LogPrint(BCLog::HTTP, "Explorer: Endpoint='%s'\n", endpoint);
    
    if (endpoint.empty() || endpoint == "index.html") {
        LogPrint(BCLog::HTTP, "Explorer: Serving HTML page\n");
        req->WriteHeader("Content-Type", "text/html");
        req->WriteReply(HTTP_OK, EXPLORER_HTML);
        return true;
    }
    
    if (endpoint == "api") {
        LogPrint(BCLog::HTTP, "Explorer: Serving API\n");
        try {
            LOCK(cs_main);
            
            // Check if chain is active
            if (!::ChainActive().Tip()) {
                UniValue error(UniValue::VOBJ);
                error.pushKV("error", "Chain not active");
                req->WriteHeader("Content-Type", "application/json");
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
            LogPrintf("Explorer: Exception in API handler: %s\n", e.what());
            UniValue error(UniValue::VOBJ);
            error.pushKV("error", std::string("Exception: ") + e.what());
            req->WriteHeader("Content-Type", "application/json");
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
