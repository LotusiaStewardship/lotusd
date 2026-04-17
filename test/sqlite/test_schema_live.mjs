#!/usr/bin/env node
/**
 * Live schema validation test for the lotusd SQLite database plan.
 *
 * Connects to a running local lotusd node via RPC, pulls blocks synced
 * via P2P, populates a local SQLite database with the planned schema,
 * and validates correctness + measures performance.
 *
 * Usage:
 *   node test_schema_live.mjs
 *
 * Environment:
 *   RPC_USER     (default: test)
 *   RPC_PASS     (default: test123)
 *   RPC_PORT     (default: 10604)
 *   TEST_BLOCKS  (default: 200)
 */

import Database from 'better-sqlite3';
import { existsSync, unlinkSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const DB_PATH = join(__dirname, 'test_chain.db');
const RPC_USER = process.env.RPC_USER || 'test';
const RPC_PASS = process.env.RPC_PASS || 'test123';
const RPC_PORT = process.env.RPC_PORT || '10604';
const RPC_URL = `http://127.0.0.1:${RPC_PORT}/`;
const BLOCKS_TO_PULL = parseInt(process.env.TEST_BLOCKS || '200', 10);

const passed = [];
const failed = [];
const benchmarks = {};

function assert(condition, label) {
  if (condition) {
    passed.push(label);
  } else {
    failed.push(label);
    console.error(`  FAIL: ${label}`);
  }
}

function bench(label, fn) {
  const start = performance.now();
  const result = fn();
  const elapsed = performance.now() - start;
  benchmarks[label] = elapsed;
  return result;
}

// ── RPC client ──────────────────────────────────────────────────────────────

let rpcId = 0;
async function rpc(method, params = []) {
  const body = JSON.stringify({ jsonrpc: '1.0', id: ++rpcId, method, params });
  const auth = Buffer.from(`${RPC_USER}:${RPC_PASS}`).toString('base64');
  const res = await fetch(RPC_URL, {
    method: 'POST',
    headers: {
      'Content-Type': 'application/json',
      Authorization: `Basic ${auth}`,
    },
    body,
    signal: AbortSignal.timeout(30000),
  });
  if (!res.ok) throw new Error(`RPC HTTP ${res.status}: ${method}`);
  const json = await res.json();
  if (json.error) throw new Error(`RPC error ${json.error.code}: ${json.error.message}`);
  return json.result;
}

function satsFromValue(val) {
  return Math.round(Number(val || 0) * 1e6);
}

// ── Schema DDL ──────────────────────────────────────────────────────────────

const SCHEMA_DDL = `
CREATE TABLE IF NOT EXISTS utxos (
    txid        BLOB NOT NULL,
    vout        INTEGER NOT NULL,
    height      INTEGER NOT NULL,
    coinbase    INTEGER NOT NULL DEFAULT 0,
    amount      INTEGER NOT NULL,
    script      BLOB NOT NULL,
    PRIMARY KEY (txid, vout)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS block_index (
    height          INTEGER PRIMARY KEY,
    hash            BLOB NOT NULL UNIQUE,
    prev_hash       BLOB NOT NULL,
    time            INTEGER NOT NULL,
    n_tx            INTEGER NOT NULL DEFAULT 0,
    size            INTEGER NOT NULL DEFAULT 0,
    status          INTEGER NOT NULL DEFAULT 0,
    merkle_root     BLOB,
    difficulty      REAL NOT NULL DEFAULT 0,
    version         INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS transactions (
    txid            BLOB NOT NULL,
    block_height    INTEGER NOT NULL,
    tx_index        INTEGER NOT NULL DEFAULT 0,
    size            INTEGER NOT NULL DEFAULT 0,
    version         INTEGER NOT NULL DEFAULT 0,
    locktime        INTEGER NOT NULL DEFAULT 0,
    is_coinbase     INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (txid)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS tx_inputs (
    txid            BLOB NOT NULL,
    vin             INTEGER NOT NULL,
    prev_txid       BLOB,
    prev_vout       INTEGER,
    value_sats      INTEGER NOT NULL DEFAULT 0,
    address         TEXT,
    coinbase        INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (txid, vin)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS tx_outputs (
    txid            BLOB NOT NULL,
    vout            INTEGER NOT NULL,
    value_sats      INTEGER NOT NULL,
    script_hex      TEXT NOT NULL DEFAULT '',
    script_type     TEXT NOT NULL DEFAULT '',
    address         TEXT,
    spent           INTEGER NOT NULL DEFAULT 0,
    spent_txid      BLOB,
    spent_vin       INTEGER,
    PRIMARY KEY (txid, vout)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS address_balances (
    address         TEXT PRIMARY KEY,
    balance_sats    INTEGER NOT NULL DEFAULT 0,
    received_sats   INTEGER NOT NULL DEFAULT 0,
    sent_sats       INTEGER NOT NULL DEFAULT 0,
    tx_count        INTEGER NOT NULL DEFAULT 0,
    utxo_count      INTEGER NOT NULL DEFAULT 0,
    first_height    INTEGER NOT NULL DEFAULT 0,
    last_height     INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS address_history (
    address         TEXT NOT NULL,
    block_height    INTEGER NOT NULL,
    txid            BLOB NOT NULL,
    delta_sats      INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (address, block_height, txid)
) WITHOUT ROWID;

CREATE TABLE IF NOT EXISTS meta (
    key     TEXT PRIMARY KEY,
    value   BLOB NOT NULL
);
`;

const INDEX_DDL = `
CREATE INDEX IF NOT EXISTS idx_block_hash ON block_index(hash);
CREATE INDEX IF NOT EXISTS idx_tx_block ON transactions(block_height);
CREATE INDEX IF NOT EXISTS idx_tx_block_idx ON transactions(block_height, tx_index);
CREATE INDEX IF NOT EXISTS idx_txout_address ON tx_outputs(address) WHERE address IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_txout_unspent ON tx_outputs(address) WHERE spent = 0 AND address IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_txin_prevout ON tx_inputs(prev_txid, prev_vout) WHERE prev_txid IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_addr_bal_rank ON address_balances(balance_sats DESC);
CREATE INDEX IF NOT EXISTS idx_addr_hist ON address_history(address, block_height DESC);
CREATE INDEX IF NOT EXISTS idx_utxo_height ON utxos(height);
`;

function hexToBuffer(hex) {
  if (!hex || hex === '0000000000000000000000000000000000000000000000000000000000000000') {
    return Buffer.alloc(32);
  }
  return Buffer.from(hex, 'hex');
}

// ── Main ─────────────────────────────────────────────────────────────────────

async function main() {
  console.log('╔════════════════════════════════════════════════════════════╗');
  console.log('║  lotusd SQLite Schema — P2P Live Network Validation       ║');
  console.log('╚════════════════════════════════════════════════════════════╝');
  console.log();

  if (existsSync(DB_PATH)) unlinkSync(DB_PATH);
  if (existsSync(DB_PATH + '-wal')) unlinkSync(DB_PATH + '-wal');
  if (existsSync(DB_PATH + '-shm')) unlinkSync(DB_PATH + '-shm');

  // ── Phase 1: Connect to local lotusd via RPC ──────────────────────────
  console.log('Phase 1: Connecting to local lotusd via RPC...');
  const chainInfo = await rpc('getblockchaininfo');
  const netInfo = await rpc('getnetworkinfo');
  const peerInfo = await rpc('getpeerinfo');

  console.log(`  Node:      ${netInfo.subversion} (protocol ${netInfo.protocolversion})`);
  console.log(`  Chain:     ${chainInfo.chain}`);
  console.log(`  Headers:   ${chainInfo.headers}`);
  console.log(`  Blocks:    ${chainInfo.blocks}`);
  console.log(`  IBD:       ${chainInfo.initialblockdownload}`);
  console.log(`  Peers:     ${peerInfo.length}`);
  console.log(`  Disk:      ${(chainInfo.size_on_disk / 1024 / 1024).toFixed(1)} MB`);

  assert(chainInfo.blocks > 0, 'node_has_blocks');
  assert(peerInfo.length > 0, 'node_has_peers');

  if (chainInfo.blocks < BLOCKS_TO_PULL) {
    console.log(`\n  Waiting for node to sync at least ${BLOCKS_TO_PULL} blocks...`);
    let lastBlocks = chainInfo.blocks;
    while (true) {
      await new Promise(r => setTimeout(r, 5000));
      const info = await rpc('getblockchaininfo');
      if (info.blocks !== lastBlocks) {
        process.stdout.write(`\r  Synced: ${info.blocks} blocks, ${info.headers} headers`);
        lastBlocks = info.blocks;
      }
      if (info.blocks >= BLOCKS_TO_PULL) {
        console.log();
        break;
      }
    }
  }

  const currentBlocks = (await rpc('getblockchaininfo')).blocks;
  const startHeight = 1;
  const endHeight = Math.min(startHeight + BLOCKS_TO_PULL - 1, currentBlocks);
  const actualBlocks = endHeight - startHeight + 1;
  console.log(`  Will index blocks ${startHeight} to ${endHeight} (${actualBlocks} blocks)`);
  console.log();

  // ── Phase 2: Create SQLite DB + Schema ────────────────────────────────
  console.log('Phase 2: Creating SQLite database and schema...');
  const db = new Database(DB_PATH);

  bench('schema_creation', () => {
    db.pragma('journal_mode = WAL');
    db.pragma('synchronous = NORMAL');
    db.pragma('cache_size = -65536');
    db.pragma('mmap_size = 268435456');
    db.pragma('temp_store = MEMORY');
    db.pragma('busy_timeout = 30000');
    db.exec(SCHEMA_DDL);
    db.exec(INDEX_DDL);
  });

  console.log(`  Schema created in ${benchmarks['schema_creation'].toFixed(1)}ms`);
  const tables = db.prepare("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name").all().map(r => r.name);
  console.log(`  Tables: ${tables.join(', ')}`);
  assert(tables.length === 8, `8_tables_created`);
  console.log();

  // ── Phase 3: Prepare statements ───────────────────────────────────────
  console.log('Phase 3: Preparing statements...');

  const insertBlock = db.prepare(`
    INSERT OR REPLACE INTO block_index(height, hash, prev_hash, time, n_tx, size, status, merkle_root, difficulty, version)
    VALUES(@height, @hash, @prev_hash, @time, @n_tx, @size, 1, @merkle_root, @difficulty, @version)
  `);
  const insertTx = db.prepare(`
    INSERT OR REPLACE INTO transactions(txid, block_height, tx_index, size, version, locktime, is_coinbase)
    VALUES(@txid, @block_height, @tx_index, @size, @version, @locktime, @is_coinbase)
  `);
  const insertInput = db.prepare(`
    INSERT OR REPLACE INTO tx_inputs(txid, vin, prev_txid, prev_vout, value_sats, address, coinbase)
    VALUES(@txid, @vin, @prev_txid, @prev_vout, @value_sats, @address, @coinbase)
  `);
  const insertOutput = db.prepare(`
    INSERT OR REPLACE INTO tx_outputs(txid, vout, value_sats, script_hex, script_type, address, spent)
    VALUES(@txid, @vout, @value_sats, @script_hex, @script_type, @address, 0)
  `);
  const insertUtxo = db.prepare(`
    INSERT OR REPLACE INTO utxos(txid, vout, height, coinbase, amount, script)
    VALUES(@txid, @vout, @height, @coinbase, @amount, @script)
  `);
  const removeUtxo = db.prepare(`DELETE FROM utxos WHERE txid = @txid AND vout = @vout`);
  const markSpent = db.prepare(`
    UPDATE tx_outputs SET spent = 1, spent_txid = @spent_txid, spent_vin = @spent_vin
    WHERE txid = @txid AND vout = @vout
  `);
  const upsertAddrBalance = db.prepare(`
    INSERT INTO address_balances(address, balance_sats, received_sats, sent_sats, tx_count, utxo_count, first_height, last_height)
    VALUES(@address, @delta, @received, @sent, 1, @utxo_delta, @height, @height)
    ON CONFLICT(address) DO UPDATE SET
      balance_sats = balance_sats + @delta,
      received_sats = received_sats + @received,
      sent_sats = sent_sats + @sent,
      tx_count = tx_count + 1,
      utxo_count = utxo_count + @utxo_delta,
      last_height = MAX(last_height, @height)
  `);
  const insertHistory = db.prepare(`
    INSERT OR REPLACE INTO address_history(address, block_height, txid, delta_sats)
    VALUES(@address, @block_height, @txid, @delta_sats)
  `);
  const insertMeta = db.prepare(`INSERT OR REPLACE INTO meta(key, value) VALUES(?, ?)`);

  const indexBlock = db.transaction((blockRpc, txList) => {
    insertBlock.run({
      height: blockRpc.height,
      hash: hexToBuffer(blockRpc.hash),
      prev_hash: hexToBuffer(blockRpc.previousblockhash || ''),
      time: blockRpc.time,
      n_tx: blockRpc.nTx,
      size: blockRpc.size,
      merkle_root: hexToBuffer(blockRpc.merkleroot || ''),
      difficulty: blockRpc.difficulty || 0,
      version: blockRpc.version || 0,
    });

    for (let txIdx = 0; txIdx < txList.length; txIdx++) {
      const tx = txList[txIdx];
      const txidBuf = hexToBuffer(tx.txid);
      const isCoinbase = tx.vin?.[0]?.coinbase !== undefined ? 1 : 0;

      insertTx.run({
        txid: txidBuf,
        block_height: blockRpc.height,
        tx_index: txIdx,
        size: tx.size || 0,
        version: tx.version || 0,
        locktime: tx.locktime || 0,
        is_coinbase: isCoinbase,
      });

      const addrDeltas = new Map();

      for (let vi = 0; vi < (tx.vin || []).length; vi++) {
        const inp = tx.vin[vi];
        const isCb = inp.coinbase !== undefined ? 1 : 0;
        const prevTxid = inp.txid || null;
        const prevVout = inp.vout ?? null;

        let inputAddr = null;
        let inputValue = 0;
        if (prevTxid && prevVout !== null) {
          const prevOut = db.prepare(
            'SELECT value_sats, address FROM tx_outputs WHERE txid = ? AND vout = ?'
          ).get(hexToBuffer(prevTxid), prevVout);
          if (prevOut) {
            inputAddr = prevOut.address;
            inputValue = prevOut.value_sats;
          }
        }

        insertInput.run({
          txid: txidBuf, vin: vi,
          prev_txid: prevTxid ? hexToBuffer(prevTxid) : null,
          prev_vout: prevVout,
          value_sats: inputValue,
          address: inputAddr,
          coinbase: isCb,
        });

        if (prevTxid && prevVout !== null) {
          markSpent.run({
            spent_txid: txidBuf, spent_vin: vi,
            txid: hexToBuffer(prevTxid), vout: prevVout,
          });
          removeUtxo.run({ txid: hexToBuffer(prevTxid), vout: prevVout });
        }

        if (inputAddr && inputValue > 0) {
          const d = addrDeltas.get(inputAddr) || { received: 0, sent: 0, utxo_delta: 0 };
          d.sent += inputValue;
          d.utxo_delta -= 1;
          addrDeltas.set(inputAddr, d);
        }
      }

      for (let vo = 0; vo < (tx.vout || []).length; vo++) {
        const out = tx.vout[vo];
        const valueSats = satsFromValue(out.value);
        const scriptHex = out.scriptPubKey?.hex || '';
        const scriptType = out.scriptPubKey?.type || '';
        const addresses = out.scriptPubKey?.addresses || [];
        const addr = addresses.length > 0 ? addresses[0] : null;

        insertOutput.run({
          txid: txidBuf, vout: vo,
          value_sats: valueSats,
          script_hex: scriptHex,
          script_type: scriptType,
          address: addr,
        });

        if (valueSats > 0) {
          insertUtxo.run({
            txid: txidBuf, vout: vo,
            height: blockRpc.height,
            coinbase: isCoinbase,
            amount: valueSats,
            script: hexToBuffer(scriptHex),
          });
        }

        if (addr && valueSats > 0) {
          const d = addrDeltas.get(addr) || { received: 0, sent: 0, utxo_delta: 0 };
          d.received += valueSats;
          d.utxo_delta += 1;
          addrDeltas.set(addr, d);
        }
      }

      for (const [addr, d] of addrDeltas) {
        const net = d.received - d.sent;
        upsertAddrBalance.run({
          address: addr, delta: net,
          received: d.received, sent: d.sent,
          utxo_delta: d.utxo_delta, height: blockRpc.height,
        });
        insertHistory.run({
          address: addr, block_height: blockRpc.height,
          txid: txidBuf, delta_sats: net,
        });
      }
    }

    insertMeta.run('best_block_height', Buffer.from(String(blockRpc.height)));
    insertMeta.run('best_block_hash', hexToBuffer(blockRpc.hash));
  });

  console.log('  All statements prepared OK');
  console.log();

  // ── Phase 4: Pull blocks from lotusd P2P-synced data ─────────────────
  console.log(`Phase 4: Indexing ${actualBlocks} blocks from local lotusd (P2P-synced)...`);

  let totalTxs = 0;
  let totalInputs = 0;
  let totalOutputs = 0;
  let blocksFetched = 0;
  let maxTxsInBlock = 0;
  let maxTxBlockHeight = 0;
  const errors = [];

  const overallStart = performance.now();

  for (let h = startHeight; h <= endHeight; h++) {
    try {
      const hash = await rpc('getblockhash', [h]);
      const block = await rpc('getblock', [hash, 1]);

      const txList = [];
      for (const txid of (block.tx || [])) {
        try {
          const tx = await rpc('getrawtransaction', [txid, true, hash]);
          txList.push(tx);
        } catch (e) {
          errors.push(`  tx ${txid} at h=${h}: ${e.message}`);
        }
      }

      bench(`index_block_${h}`, () => {
        indexBlock(block, txList);
      });

      totalTxs += txList.length;
      totalInputs += txList.reduce((s, t) => s + (t.vin || []).length, 0);
      totalOutputs += txList.reduce((s, t) => s + (t.vout || []).length, 0);
      blocksFetched++;

      if (txList.length > maxTxsInBlock) {
        maxTxsInBlock = txList.length;
        maxTxBlockHeight = h;
      }

      if (h % 20 === 0 || h === endHeight) {
        const pct = Math.round(((h - startHeight + 1) / actualBlocks) * 100);
        process.stdout.write(
          `\r  [${pct.toString().padStart(3)}%] h=${h} txs=${txList.length} ` +
          `| total: ${blocksFetched} blocks, ${totalTxs} txs, ${totalOutputs} outputs`
        );
      }
    } catch (e) {
      errors.push(`  block ${h}: ${e.message}`);
    }
  }

  const overallElapsed = performance.now() - overallStart;
  console.log();
  console.log();
  console.log(`  Completed in ${(overallElapsed / 1000).toFixed(2)}s`);
  console.log(`  Blocks indexed: ${blocksFetched}`);
  console.log(`  Total txs: ${totalTxs}`);
  console.log(`  Total inputs: ${totalInputs}`);
  console.log(`  Total outputs: ${totalOutputs}`);
  console.log(`  Largest block: h=${maxTxBlockHeight} (${maxTxsInBlock} txs)`);
  if (errors.length > 0) {
    console.log(`  Errors (${errors.length}):`);
    errors.slice(0, 10).forEach(e => console.log(e));
  }
  console.log();

  assert(blocksFetched > 0, 'blocks_fetched');
  assert(totalTxs > 0, 'txs_fetched');
  assert(totalOutputs > 0, 'outputs_fetched');

  // ── Phase 5: Data integrity ───────────────────────────────────────────
  console.log('Phase 5: Data integrity validation...');

  const counts = {};
  for (const t of ['block_index', 'transactions', 'tx_inputs', 'tx_outputs', 'utxos', 'address_balances', 'address_history']) {
    counts[t] = db.prepare(`SELECT COUNT(*) as c FROM ${t}`).get().c;
  }

  console.log('  Row counts:');
  for (const [k, v] of Object.entries(counts)) console.log(`    ${k.padEnd(20)}: ${v}`);

  assert(counts.block_index === blocksFetched, `blocks_match`);
  assert(counts.transactions === totalTxs, `txs_match`);
  assert(counts.tx_inputs === totalInputs, `inputs_match`);
  assert(counts.tx_outputs === totalOutputs, `outputs_match`);
  assert(counts.utxos > 0, 'utxos_populated');
  assert(counts.address_balances > 0, 'addresses_populated');
  assert(counts.address_history > 0, 'history_populated');

  const chainBreaks = db.prepare(`
    SELECT b1.height FROM block_index b1
    JOIN block_index b2 ON b2.height = b1.height - 1
    WHERE b1.prev_hash != b2.hash
  `).all();
  console.log(`  Chain linkage breaks: ${chainBreaks.length}`);
  assert(chainBreaks.length === 0, 'chain_linkage_intact');

  const txMismatches = db.prepare(`
    SELECT b.height, b.n_tx AS expected, COUNT(t.txid) AS actual
    FROM block_index b LEFT JOIN transactions t ON t.block_height = b.height
    GROUP BY b.height HAVING expected != actual
  `).all();
  console.log(`  Tx-count mismatches: ${txMismatches.length}`);
  assert(txMismatches.length === 0, 'tx_counts_match');

  const coinbaseTxs = db.prepare('SELECT COUNT(*) as c FROM transactions WHERE is_coinbase = 1').get().c;
  console.log(`  Coinbase txs: ${coinbaseTxs}`);
  assert(coinbaseTxs === blocksFetched, 'coinbase_count_correct');

  const utxoMismatch = db.prepare(`
    SELECT COUNT(*) as c FROM tx_outputs o
    LEFT JOIN utxos u ON u.txid = o.txid AND u.vout = o.vout
    WHERE o.spent = 0 AND o.value_sats > 0 AND u.txid IS NULL
  `).get().c;
  console.log(`  Unspent missing from UTXO: ${utxoMismatch}`);
  assert(utxoMismatch === 0, 'utxo_set_complete');

  const spentInUtxo = db.prepare(`
    SELECT COUNT(*) as c FROM tx_outputs o
    JOIN utxos u ON u.txid = o.txid AND u.vout = o.vout
    WHERE o.spent = 1
  `).get().c;
  console.log(`  Spent still in UTXO: ${spentInUtxo}`);
  assert(spentInUtxo === 0, 'no_spent_in_utxo');
  console.log();

  // ── Phase 6: Cross-validate with RPC ──────────────────────────────────
  console.log('Phase 6: Cross-validating random blocks against lotusd RPC...');
  const sampleCount = 5;
  let crossOk = 0;

  for (let i = 0; i < sampleCount; i++) {
    const h = startHeight + Math.floor(Math.random() * blocksFetched);
    try {
      const hash = await rpc('getblockhash', [h]);
      const apiBlock = await rpc('getblock', [hash, 1]);
      const dbBlock = db.prepare('SELECT * FROM block_index WHERE height = ?').get(h);
      const dbTxCount = db.prepare('SELECT COUNT(*) as c FROM transactions WHERE block_height = ?').get(h).c;

      const hashOk = dbBlock && dbBlock.hash.toString('hex') === apiBlock.hash;
      const txOk = dbTxCount === apiBlock.nTx;
      const timeOk = dbBlock && dbBlock.time === apiBlock.time;

      if (hashOk && txOk && timeOk) {
        crossOk++;
        console.log(`  Block ${h}: OK`);
      } else {
        console.log(`  Block ${h}: MISMATCH hash=${hashOk} txs=${txOk}(db=${dbTxCount},rpc=${apiBlock.nTx}) time=${timeOk}`);
      }
    } catch (e) {
      console.log(`  Block ${h}: ERROR ${e.message}`);
    }
  }
  assert(crossOk === sampleCount, `cross_validation_all_pass`);
  console.log();

  // ── Phase 7: Query benchmarks ─────────────────────────────────────────
  console.log('Phase 7: Query benchmarks...');

  const midHeight = startHeight + Math.floor(actualBlocks / 2);

  bench('q_blocks_page_25', () => {
    db.prepare('SELECT height, hash, time, n_tx, size FROM block_index ORDER BY height DESC LIMIT 25').all();
  });

  bench('q_block_by_height_1k', () => {
    for (let i = 0; i < 1000; i++)
      db.prepare('SELECT * FROM block_index WHERE height = ?').get(midHeight);
  });
  benchmarks['q_block_by_height_avg_us'] = (benchmarks['q_block_by_height_1k'] / 1000) * 1000;

  bench('q_txs_in_block', () => {
    db.prepare('SELECT * FROM transactions WHERE block_height = ? ORDER BY tx_index').all(maxTxBlockHeight);
  });

  const sampleUtxo = db.prepare('SELECT txid, vout FROM utxos LIMIT 1').get();
  if (sampleUtxo) {
    bench('q_utxo_lookup_1k', () => {
      for (let i = 0; i < 1000; i++)
        db.prepare('SELECT * FROM utxos WHERE txid = ? AND vout = ?').get(sampleUtxo.txid, sampleUtxo.vout);
    });
    benchmarks['q_utxo_lookup_avg_us'] = (benchmarks['q_utxo_lookup_1k'] / 1000) * 1000;
  }

  const topAddr = db.prepare("SELECT address, balance_sats, tx_count FROM address_balances WHERE address IS NOT NULL ORDER BY tx_count DESC LIMIT 1").get();
  if (topAddr) {
    console.log(`  Top address: ${topAddr.address} (${topAddr.tx_count} txs, ${topAddr.balance_sats} sats)`);
    bench('q_addr_balance_1k', () => {
      for (let i = 0; i < 1000; i++)
        db.prepare('SELECT * FROM address_balances WHERE address = ?').get(topAddr.address);
    });
    benchmarks['q_addr_balance_avg_us'] = (benchmarks['q_addr_balance_1k'] / 1000) * 1000;

    bench('q_addr_history_page', () => {
      db.prepare('SELECT * FROM address_history WHERE address = ? ORDER BY block_height DESC LIMIT 25').all(topAddr.address);
    });

    bench('q_addr_utxos', () => {
      db.prepare('SELECT * FROM tx_outputs WHERE address = ? AND spent = 0').all(topAddr.address);
    });
  }

  bench('q_richlist_top100', () => {
    db.prepare('SELECT address, balance_sats FROM address_balances ORDER BY balance_sats DESC LIMIT 100').all();
  });

  bench('q_total_utxo_count', () => { db.prepare('SELECT COUNT(*) FROM utxos').get(); });
  bench('q_total_supply', () => { db.prepare('SELECT SUM(amount) FROM utxos').get(); });

  const sampleTx = db.prepare('SELECT txid FROM transactions LIMIT 1').get();
  if (sampleTx) {
    bench('q_tx_by_txid_1k', () => {
      for (let i = 0; i < 1000; i++)
        db.prepare('SELECT * FROM transactions WHERE txid = ?').get(sampleTx.txid);
    });
    benchmarks['q_tx_by_txid_avg_us'] = (benchmarks['q_tx_by_txid_1k'] / 1000) * 1000;
  }

  console.log();
  console.log('  Query latencies:');
  for (const [name, ms] of Object.entries(benchmarks).filter(([k]) => k.startsWith('q_')).sort(([, a], [, b]) => a - b)) {
    const unit = ms < 1 ? `${(ms * 1000).toFixed(0)}µs` : `${ms.toFixed(2)}ms`;
    console.log(`    ${name.padEnd(30)}: ${unit}`);
  }
  console.log();

  // ── Phase 8: Query plans ──────────────────────────────────────────────
  console.log('Phase 8: EXPLAIN QUERY PLAN...');
  const plans = [
    ['blocks_page',     'SELECT height, hash, time FROM block_index ORDER BY height DESC LIMIT 25'],
    ['block_by_height', 'SELECT * FROM block_index WHERE height = 100'],
    ['block_by_hash',   "SELECT * FROM block_index WHERE hash = X'00'"],
    ['txs_in_block',    'SELECT * FROM transactions WHERE block_height = 100 ORDER BY tx_index'],
    ['tx_by_txid',      "SELECT * FROM transactions WHERE txid = X'00'"],
    ['utxo_lookup',     "SELECT * FROM utxos WHERE txid = X'00' AND vout = 0"],
    ['addr_balance',    "SELECT * FROM address_balances WHERE address = 'test'"],
    ['addr_history',    "SELECT * FROM address_history WHERE address = 'test' ORDER BY block_height DESC LIMIT 25"],
    ['addr_utxos',      "SELECT * FROM tx_outputs WHERE address = 'test' AND spent = 0"],
    ['richlist',        'SELECT address, balance_sats FROM address_balances ORDER BY balance_sats DESC LIMIT 100'],
    ['prevout_lookup',  "SELECT * FROM tx_inputs WHERE prev_txid = X'00' AND prev_vout = 0"],
  ];

  let idxHits = 0;
  for (const [name, sql] of plans) {
    try {
      const plan = db.prepare(`EXPLAIN QUERY PLAN ${sql}`).all();
      const detail = plan.map(r => r.detail).join(' | ');
      const usesIdx = /USING (COVERING )?INDEX|PRIMARY KEY/.test(detail);
      if (usesIdx) idxHits++;
      console.log(`  [${usesIdx ? 'IDX' : 'SCN'}] ${name.padEnd(16)}: ${detail.substring(0, 70)}`);
    } catch (e) {
      console.log(`  [ERR] ${name}: ${e.message}`);
    }
  }
  assert(idxHits >= 8, `index_coverage_${idxHits}_of_${plans.length}`);
  console.log();

  // ── Phase 9: Stats ────────────────────────────────────────────────────
  console.log('Phase 9: Summary...');
  const pageCount = db.prepare("SELECT page_count FROM pragma_page_count()").get().page_count;
  const pageSize = db.prepare("SELECT page_size FROM pragma_page_size()").get().page_size;
  const dbSize = pageCount * pageSize;

  const indexTimes = Object.entries(benchmarks).filter(([k]) => k.startsWith('index_block_')).map(([, v]) => v).sort((a, b) => a - b);
  const avg = indexTimes.reduce((a, b) => a + b, 0) / indexTimes.length;
  const p50 = indexTimes[Math.floor(indexTimes.length * 0.5)];
  const p95 = indexTimes[Math.floor(indexTimes.length * 0.95)];
  const max = indexTimes[indexTimes.length - 1];

  console.log(`  DB size: ${(dbSize / 1024 / 1024).toFixed(2)} MB`);
  console.log(`  Total time: ${(overallElapsed / 1000).toFixed(2)}s (RPC fetch + index)`);
  console.log(`  Throughput: ${(blocksFetched / (overallElapsed / 1000)).toFixed(1)} blocks/s, ${(totalTxs / (overallElapsed / 1000)).toFixed(0)} tx/s`);
  console.log(`  Index time: avg=${avg.toFixed(2)}ms p50=${p50.toFixed(2)}ms p95=${p95.toFixed(2)}ms max=${max.toFixed(2)}ms`);
  console.log();

  // ── Results ───────────────────────────────────────────────────────────
  console.log('════════════════════════════════════════════════════════');
  console.log(`  RESULTS: ${passed.length} PASSED, ${failed.length} FAILED`);
  console.log('════════════════════════════════════════════════════════');
  if (failed.length > 0) {
    console.log('  Failed:');
    failed.forEach(f => console.log(`    ✗ ${f}`));
  }
  console.log();
  console.log(`  Database: ${DB_PATH}`);
  console.log(`  Data source: lotusd P2P network (${netInfo.subversion})`);

  db.close();
  process.exit(failed.length > 0 ? 1 : 0);
}

main().catch(err => {
  console.error('Fatal error:', err);
  process.exit(2);
});
