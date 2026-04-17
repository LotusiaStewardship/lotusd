#!/usr/bin/env node
/**
 * Functional tests for the Lotus REST API v1.
 *
 * Tests against a running lotusd node with -reindex or synced chain.
 *
 * Usage:
 *   node test_api_v1.mjs
 *
 * Environment:
 *   RPC_PORT  (default: 10604)
 */

const RPC_PORT = process.env.RPC_PORT || '10604';
const BASE_URL = `http://127.0.0.1:${RPC_PORT}/api/v1`;

let passed = 0;
let failed = 0;

async function api(path) {
    const url = `${BASE_URL}/${path}`;
    const res = await fetch(url);
    const body = await res.text();
    let json;
    try {
        json = JSON.parse(body);
    } catch {
        json = null;
    }
    return { status: res.status, json, body, headers: res.headers };
}

function assert(condition, msg) {
    if (!condition) {
        throw new Error(`Assertion failed: ${msg}`);
    }
}

async function test(name, fn) {
    try {
        await fn();
        passed++;
        console.log(`  ✓ ${name}`);
    } catch (e) {
        failed++;
        console.log(`  ✗ ${name}: ${e.message}`);
    }
}

// ─── Chain ────────────────────────────────────────────────────
console.log('\n── Chain endpoints ──');

await test('GET /chain returns chain info', async () => {
    const r = await api('chain');
    assert(r.status === 200, `status ${r.status}`);
    assert(r.json.height >= 0, 'has height');
    assert(r.json.best_block_hash, 'has hash');
    assert(r.json.chain, 'has chain name');
    assert(typeof r.json.difficulty === 'number', 'has difficulty');
});

await test('GET /chain/tip returns tip', async () => {
    const r = await api('chain/tip');
    assert(r.status === 200, `status ${r.status}`);
    assert(r.json.hash, 'has hash');
    assert(r.json.height >= 0, 'has height');
    assert(r.json.time > 0, 'has time');
});

// ─── Blocks ───────────────────────────────────────────────────
console.log('\n── Block endpoints ──');

await test('GET /blocks returns paginated block list', async () => {
    const r = await api('blocks?limit=5');
    assert(r.status === 200, `status ${r.status}`);
    assert(r.json.data, 'has data array');
    assert(r.json.pagination, 'has pagination');
    assert(r.json.data.length <= 5, `got ${r.json.data.length} <= 5`);
    assert(r.json.pagination.total > 0, 'has total');
});

await test('GET /blocks/0 returns genesis', async () => {
    const r = await api('blocks/0');
    assert(r.status === 200, `status ${r.status}`);
    assert(r.json.height === 0, 'height is 0');
    assert(r.json.hash, 'has hash');
});

let firstBlockHash;
await test('GET /blocks/1 returns block 1', async () => {
    const r = await api('blocks/1');
    assert(r.status === 200, `status ${r.status}`);
    assert(r.json.height === 1, 'height is 1');
    firstBlockHash = r.json.hash;
});

await test('GET /blocks/<hash> returns same block', async () => {
    if (!firstBlockHash) return;
    const r = await api(`blocks/${firstBlockHash}`);
    assert(r.status === 200, `status ${r.status}`);
    assert(r.json.height === 1, 'same block');
});

await test('GET /blocks/999999999 returns 404', async () => {
    const r = await api('blocks/999999999');
    assert(r.status === 404, `status ${r.status}`);
    assert(r.json.error === 'block_not_found', 'error code');
});

await test('GET /blocks/1/txs returns txs', async () => {
    const r = await api('blocks/1/txs?limit=10');
    assert(r.status === 200, `status ${r.status}`);
    assert(r.json.data, 'has data');
    assert(r.json.pagination, 'has pagination');
    if (r.json.data.length > 0) {
        assert(r.json.data[0].txid, 'tx has txid');
    }
});

// ─── Transactions ─────────────────────────────────────────────
console.log('\n── Transaction endpoints ──');

let testTxid;
await test('GET block 1 txs to find a txid', async () => {
    const r = await api('blocks/1/txs?limit=1');
    if (r.json.data && r.json.data.length > 0) {
        testTxid = r.json.data[0].txid;
    }
    assert(testTxid, 'found a txid');
});

await test('GET /txs/<txid> returns tx summary', async () => {
    if (!testTxid) return;
    const r = await api(`txs/${testTxid}`);
    assert(r.status === 200, `status ${r.status}`);
    assert(r.json.txid === testTxid, 'matches txid');
    assert(r.json.block_height >= 0, 'has height');
    assert(r.json.output_count >= 0, 'has output_count');
});

await test('GET /txs/<txid>/inputs returns inputs', async () => {
    if (!testTxid) return;
    const r = await api(`txs/${testTxid}/inputs`);
    assert(r.status === 200, `status ${r.status}`);
    assert(Array.isArray(r.json), 'is array');
});

await test('GET /txs/<txid>/outputs returns outputs', async () => {
    if (!testTxid) return;
    const r = await api(`txs/${testTxid}/outputs`);
    assert(r.status === 200, `status ${r.status}`);
    assert(Array.isArray(r.json), 'is array');
    if (r.json.length > 0) {
        assert(typeof r.json[0].value_sats === 'number', 'has value');
    }
});

await test('GET /txs with missing txid returns error', async () => {
    const r = await api('txs');
    assert(r.status === 400, `status ${r.status}`);
});

await test('GET /txs/<invalid> returns 400', async () => {
    const r = await api('txs/notahash');
    assert(r.status === 400, `status ${r.status}`);
});

// ─── Mempool ──────────────────────────────────────────────────
console.log('\n── Mempool endpoint ──');

await test('GET /mempool returns mempool info', async () => {
    const r = await api('mempool');
    assert(r.status === 200, `status ${r.status}`);
    assert(typeof r.json.size === 'number', 'has size');
    assert(r.json.transactions, 'has transactions array');
});

// ─── Addresses ────────────────────────────────────────────────
console.log('\n── Address endpoints ──');

await test('GET /addresses returns rich list', async () => {
    const r = await api('addresses?limit=5');
    assert(r.status === 200, `status ${r.status}`);
    assert(r.json.data, 'has data');
    assert(r.json.pagination, 'has pagination');
});

let testAddress;
await test('find an address from outputs', async () => {
    if (!testTxid) return;
    const r = await api(`txs/${testTxid}/outputs`);
    if (r.json && r.json.length > 0) {
        testAddress = r.json[0].address;
    }
    // testAddress may be null for OP_RETURN etc
});

await test('GET /addresses/<addr> returns summary', async () => {
    if (!testAddress) return;
    const r = await api(`addresses/${testAddress}`);
    assert(r.status === 200, `status ${r.status}`);
    assert(r.json.address === testAddress, 'matches');
    assert(typeof r.json.balance_sats === 'number', 'has balance');
    assert(typeof r.json.tx_count === 'number', 'has tx_count');
});

await test('GET /addresses/<addr>/txs returns history', async () => {
    if (!testAddress) return;
    const r = await api(`addresses/${testAddress}/txs?limit=5`);
    assert(r.status === 200, `status ${r.status}`);
    assert(r.json.data, 'has data');
});

await test('GET /addresses/<addr>/utxos returns UTXOs', async () => {
    if (!testAddress) return;
    const r = await api(`addresses/${testAddress}/utxos?limit=5`);
    assert(r.status === 200, `status ${r.status}`);
    assert(r.json.data, 'has data');
});

// ─── UTXOs ────────────────────────────────────────────────────
console.log('\n── UTXO endpoint ──');

await test('GET /utxos/<txid>/<vout> returns output', async () => {
    if (!testTxid) return;
    const r = await api(`utxos/${testTxid}/0`);
    assert(r.status === 200, `status ${r.status}`);
    assert(r.json.txid === testTxid, 'matches txid');
    assert(r.json.vout === 0, 'vout 0');
    assert(typeof r.json.value_sats === 'number', 'has value');
});

await test('GET /utxos missing params returns 400', async () => {
    const r = await api('utxos/abc');
    assert(r.status === 400, `status ${r.status}`);
});

// ─── Network ──────────────────────────────────────────────────
console.log('\n── Network endpoints ──');

await test('GET /network returns info', async () => {
    const r = await api('network');
    assert(r.status === 200, `status ${r.status}`);
    assert(typeof r.json.protocol_version === 'number', 'has protocol_version');
    assert(r.json.subversion, 'has subversion');
});

await test('GET /network/peers returns peer list', async () => {
    const r = await api('network/peers');
    assert(r.status === 200, `status ${r.status}`);
    assert(Array.isArray(r.json), 'is array');
});

// ─── Node ─────────────────────────────────────────────────────
console.log('\n── Node endpoint ──');

await test('GET /node returns node info', async () => {
    const r = await api('node');
    assert(r.status === 200, `status ${r.status}`);
    assert(typeof r.json.version === 'number', 'has version');
    assert(typeof r.json.uptime === 'number', 'has uptime');
    assert(typeof r.json.chain_height === 'number', 'has chain_height');
});

// ─── Mining ───────────────────────────────────────────────────
console.log('\n── Mining endpoint ──');

await test('GET /mining returns mining info', async () => {
    const r = await api('mining');
    assert(r.status === 200, `status ${r.status}`);
    assert(typeof r.json.height === 'number', 'has height');
    assert(typeof r.json.difficulty === 'number', 'has difficulty');
});

// ─── Events ───────────────────────────────────────────────────
console.log('\n── Events endpoint ──');

await test('GET /events returns event list', async () => {
    const r = await api('events');
    assert(r.status === 200, `status ${r.status}`);
    assert(r.json.events, 'has events array');
    assert(typeof r.json.latest_seq === 'number', 'has latest_seq');
});

// ─── OpenAPI ──────────────────────────────────────────────────
console.log('\n── OpenAPI endpoint ──');

await test('GET /openapi.json returns schema', async () => {
    const r = await api('openapi.json');
    assert(r.status === 200, `status ${r.status}`);
    assert(r.json.openapi === '3.1.0', 'version 3.1.0');
    assert(r.json.info.title === 'Lotus Node API', 'correct title');
    assert(r.json.paths, 'has paths');
    assert(r.json.components, 'has components');
    assert(Object.keys(r.json.paths).length >= 15, 'has enough paths');
});

// ─── Error handling ───────────────────────────────────────────
console.log('\n── Error handling ──');

await test('GET /nonexistent returns 404', async () => {
    const r = await api('nonexistent');
    assert(r.status === 404, `status ${r.status}`);
    assert(r.json.error === 'not_found', 'error code');
});

await test('CORS headers present', async () => {
    const r = await api('chain');
    const cors = r.headers.get('access-control-allow-origin');
    assert(cors === '*', `CORS: ${cors}`);
});

// ─── Summary ──────────────────────────────────────────────────
console.log(`\n══════════════════════════════════════`);
console.log(`  Results: ${passed} passed, ${failed} failed`);
console.log(`══════════════════════════════════════\n`);
process.exit(failed > 0 ? 1 : 0);
