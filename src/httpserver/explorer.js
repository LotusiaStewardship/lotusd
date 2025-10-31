let lastHeight = null;
let currentView = 'home';
let currentHash = '';

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
        year: 'numeric',
        month: 'short',
        day: 'numeric',
        hour: '2-digit',
        minute: '2-digit',
        second: '2-digit'
    });
}

function formatAmount(satoshis) {
    return (satoshis / 100).toFixed(2) + ' XPI';
}

async function showHome() {
    currentView = 'home';
    document.querySelector('.container').innerHTML = `
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
                <div class="stat-value hash-value" id="hash">-</div>
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
    `;
    updateHome();
}

async function showBlock(hash) {
    currentView = 'block';
    currentHash = hash;
    
    document.querySelector('.container').innerHTML = `
        <div class="detail-container">
            <button class="back-button" onclick="showHome()">⬅️ Back to Blocks</button>
            <h2>🧱 Block Details</h2>
            <div id="blockDetail" class="loading">⏳ Loading block...</div>
        </div>
    `;
    
    try {
        const r = await fetch('/explorer/block/' + hash);
        const block = await r.json();
        
        if (block.error) {
            document.getElementById('blockDetail').innerHTML = 
                '<div class="error">❌ Error: ' + block.error + '</div>';
            return;
        }
        
        let html = `
            <div class="detail-section">
                <h3>📋 Block Information</h3>
                <div class="detail-row">
                    <div class="detail-label">Height:</div>
                    <div class="detail-value">${block.height}</div>
                </div>
                <div class="detail-row">
                    <div class="detail-label">Hash:</div>
                    <div class="detail-value">${block.hash}</div>
                </div>
                <div class="detail-row">
                    <div class="detail-label">Previous Block:</div>
                    <div class="detail-value"><a href="#" onclick="showBlock('${block.previousblockhash}'); return false;" style="color: #667eea;">${block.previousblockhash}</a></div>
                </div>
                <div class="detail-row">
                    <div class="detail-label">Merkle Root:</div>
                    <div class="detail-value">${block.merkleroot}</div>
                </div>
                <div class="detail-row">
                    <div class="detail-label">Timestamp:</div>
                    <div class="detail-value">${formatTime(block.time)}</div>
                </div>
                <div class="detail-row">
                    <div class="detail-label">Difficulty:</div>
                    <div class="detail-value">${block.difficulty}</div>
                </div>
                <div class="detail-row">
                    <div class="detail-label">Nonce:</div>
                    <div class="detail-value">${block.nonce}</div>
                </div>
                <div class="detail-row">
                    <div class="detail-label">Size:</div>
                    <div class="detail-value">${(block.size / 1024).toFixed(2)} KB</div>
                </div>
            </div>
            
            <div class="detail-section">
                <h3>💸 Transactions (${block.tx.length})</h3>
        `;
        
        for (const tx of block.tx) {
            html += `
                <div class="tx-card" onclick="showTransaction('${tx.txid}')">
                    <div class="tx-header">📄 ${tx.txid}</div>
                    <div class="tx-io">
                        <div class="tx-inputs">
                            <h4>⬇️ Inputs (${tx.vin.length})</h4>
            `;
            
            for (const input of tx.vin.slice(0, 3)) {
                if (input.coinbase) {
                    html += '<div class="io-item">⛏️ Coinbase (New Coins)</div>';
                } else {
                    html += `<div class="io-item">${input.txid || 'N/A'}:${input.vout || 0}</div>`;
                }
            }
            if (tx.vin.length > 3) {
                html += `<div class="io-item">... and ${tx.vin.length - 3} more</div>`;
            }
            
            html += `
                        </div>
                        <div class="tx-outputs">
                            <h4>⬆️ Outputs (${tx.vout.length})</h4>
            `;
            
            for (const output of tx.vout.slice(0, 3)) {
                const address = output.scriptPubKey.address || output.scriptPubKey.type;
                html += `<div class="io-item">${formatAmount(output.value)} → ${address}</div>`;
            }
            if (tx.vout.length > 3) {
                html += `<div class="io-item">... and ${tx.vout.length - 3} more</div>`;
            }
            
            html += `
                        </div>
                    </div>
                </div>
            `;
        }
        
        html += '</div>';
        document.getElementById('blockDetail').innerHTML = html;
    } catch(e) {
        document.getElementById('blockDetail').innerHTML = 
            '<div class="error">❌ Connection error: ' + e.message + '</div>';
    }
}

async function showTransaction(txid) {
    currentView = 'tx';
    
    document.querySelector('.container').innerHTML = `
        <div class="detail-container">
            <button class="back-button" onclick="showBlock('${currentHash}')">⬅️ Back to Block</button>
            <h2>💸 Transaction Details</h2>
            <div id="txDetail" class="loading">⏳ Loading transaction...</div>
        </div>
    `;
    
    try {
        const r = await fetch('/explorer/tx/' + txid);
        const tx = await r.json();
        
        if (tx.error) {
            document.getElementById('txDetail').innerHTML = 
                '<div class="error">❌ Error: ' + tx.error + '</div>';
            return;
        }
        
        let totalIn = 0;
        let totalOut = 0;
        
        let html = `
            <div class="detail-section">
                <h3>📋 Transaction Information</h3>
                <div class="detail-row">
                    <div class="detail-label">TxID:</div>
                    <div class="detail-value">${tx.txid}</div>
                </div>
                <div class="detail-row">
                    <div class="detail-label">Version:</div>
                    <div class="detail-value">${tx.version}</div>
                </div>
                <div class="detail-row">
                    <div class="detail-label">Lock Time:</div>
                    <div class="detail-value">${tx.locktime}</div>
                </div>
                <div class="detail-row">
                    <div class="detail-label">Size:</div>
                    <div class="detail-value">${tx.size} bytes</div>
                </div>
            </div>
            
            <div class="detail-section">
                <h3>⬇️ Inputs (${tx.vin.length})</h3>
        `;
        
        for (const input of tx.vin) {
            if (input.coinbase) {
                html += `
                    <div class="tx-card">
                        <strong>⛏️ Coinbase Input</strong><br>
                        Data: ${input.coinbase}
                    </div>
                `;
            } else {
                const scriptSigAsm = input.scriptSig ? (input.scriptSig.asm || input.scriptSig.hex) : 'N/A';
                html += `
                    <div class="tx-card">
                        <strong>Previous Output:</strong> ${input.txid}:${input.vout}<br>
                        <strong>ScriptSig:</strong> <code style="word-break: break-all; white-space: pre-wrap; font-size: 0.85em;">${scriptSigAsm}</code>
                    </div>
                `;
            }
        }
        
        html += `
            </div>
            <div class="detail-section">
                <h3>⬆️ Outputs (${tx.vout.length})</h3>
        `;
        
        for (const output of tx.vout) {
            totalOut += output.value;
            const address = output.scriptPubKey.address || 'N/A';
            html += `
                <div class="tx-card">
                    <strong>Output #${output.n}:</strong> ${formatAmount(output.value)}<br>
                    <strong>Address:</strong> <span style="color: #48bb78; font-family: monospace; font-size: 0.9em;">${address}</span><br>
                    <strong>Type:</strong> ${output.scriptPubKey.type}
                </div>
            `;
        }
        
        html += `
            </div>
            <div class="detail-section">
                <h3>💰 Summary</h3>
                <div class="detail-row">
                    <div class="detail-label">Total Output:</div>
                    <div class="detail-value" style="color: #667eea; font-weight: bold;">${formatAmount(totalOut)}</div>
                </div>
            </div>
        `;
        
        document.getElementById('txDetail').innerHTML = html;
    } catch(e) {
        document.getElementById('txDetail').innerHTML = 
            '<div class="error">❌ Connection error: ' + e.message + '</div>';
    }
}

async function updateHome() {
    if (currentView !== 'home') return;
    
    try {
        const r = await fetch('/explorer/api');
        const d = await r.json();
        
        if (d.error) {
            document.getElementById('blocks').innerHTML = 
                '<tr><td colspan="4" class="error">❌ Error: ' + d.error + '</td></tr>';
            return;
        }
        
        if (lastHeight !== null && d.height > lastHeight) {
            showUpdate();
        }
        lastHeight = d.height;
        
        document.getElementById('height').textContent = d.height.toLocaleString();
        document.getElementById('hash').textContent = d.hash;
        document.getElementById('chain').textContent = d.chain;
        
        let html = '';
        for (const b of d.blocks) {
            html += '<tr onclick="showBlock(\'' + b.hash + '\')">' +
                '<td class="block-height">🧱 ' + b.height.toLocaleString() + '</td>' +
                '<td class="block-hash">' + b.hash + '</td>' +
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

// Auto-update home view every 3 seconds
setInterval(() => {
    if (currentView === 'home') {
        updateHome();
    }
}, 3000);

// Initial load
showHome();

