#!/usr/bin/env node
/**
 * Monitor: periodically index a batch of blocks from the syncing lotusd node
 * into SQLite, reporting stats after each round.
 */

import Database from 'better-sqlite3';
import { existsSync, unlinkSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const DB_PATH = join(__dirname, 'test_chain_monitor.db');
const RPC_USER = process.env.RPC_USER || 'test';
const RPC_PASS = process.env.RPC_PASS || 'test123';
const RPC_PORT = process.env.RPC_PORT || '10604';
const RPC_URL = `http://127.0.0.1:${RPC_PORT}/`;
const BATCH = parseInt(process.env.BATCH || '500', 10);
const ROUNDS = parseInt(process.env.ROUNDS || '20', 10);
const PAUSE_SEC = parseInt(process.env.PAUSE || '30', 10);

let rpcId = 0;
async function rpc(method, params = []) {
  const body = JSON.stringify({ jsonrpc: '1.0', id: ++rpcId, method, params });
  const auth = Buffer.from(`${RPC_USER}:${RPC_PASS}`).toString('base64');
  const res = await fetch(RPC_URL, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', Authorization: `Basic ${auth}` },
    body,
    signal: AbortSignal.timeout(60000),
  });
  if (!res.ok) throw new Error(`RPC HTTP ${res.status}`);
  const json = await res.json();
  if (json.error) throw new Error(`RPC ${json.error.code}: ${json.error.message}`);
  return json.result;
}

function satsFromValue(val) { return Math.round(Number(val || 0) * 1e6); }
function hexToBuffer(hex) {
  if (!hex) return Buffer.alloc(32);
  return Buffer.from(hex, 'hex');
}

const SCHEMA_DDL = `
CREATE TABLE IF NOT EXISTS utxos (
    txid BLOB NOT NULL, vout INTEGER NOT NULL, height INTEGER NOT NULL,
    coinbase INTEGER NOT NULL DEFAULT 0, amount INTEGER NOT NULL, script BLOB NOT NULL,
    PRIMARY KEY (txid, vout)) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS block_index (
    height INTEGER PRIMARY KEY, hash BLOB NOT NULL UNIQUE, prev_hash BLOB NOT NULL,
    time INTEGER NOT NULL, n_tx INTEGER NOT NULL DEFAULT 0, size INTEGER NOT NULL DEFAULT 0,
    status INTEGER NOT NULL DEFAULT 0, merkle_root BLOB,
    difficulty REAL NOT NULL DEFAULT 0, version INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS transactions (
    txid BLOB NOT NULL, block_height INTEGER NOT NULL, tx_index INTEGER NOT NULL DEFAULT 0,
    size INTEGER NOT NULL DEFAULT 0, version INTEGER NOT NULL DEFAULT 0,
    locktime INTEGER NOT NULL DEFAULT 0, is_coinbase INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (txid)) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS tx_inputs (
    txid BLOB NOT NULL, vin INTEGER NOT NULL, prev_txid BLOB, prev_vout INTEGER,
    value_sats INTEGER NOT NULL DEFAULT 0, address TEXT,
    coinbase INTEGER NOT NULL DEFAULT 0, PRIMARY KEY (txid, vin)) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS tx_outputs (
    txid BLOB NOT NULL, vout INTEGER NOT NULL, value_sats INTEGER NOT NULL,
    script_hex TEXT NOT NULL DEFAULT '', script_type TEXT NOT NULL DEFAULT '',
    address TEXT, spent INTEGER NOT NULL DEFAULT 0, spent_txid BLOB, spent_vin INTEGER,
    PRIMARY KEY (txid, vout)) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS address_balances (
    address TEXT PRIMARY KEY, balance_sats INTEGER NOT NULL DEFAULT 0,
    received_sats INTEGER NOT NULL DEFAULT 0, sent_sats INTEGER NOT NULL DEFAULT 0,
    tx_count INTEGER NOT NULL DEFAULT 0, utxo_count INTEGER NOT NULL DEFAULT 0,
    first_height INTEGER NOT NULL DEFAULT 0, last_height INTEGER NOT NULL DEFAULT 0);
CREATE TABLE IF NOT EXISTS address_history (
    address TEXT NOT NULL, block_height INTEGER NOT NULL, txid BLOB NOT NULL,
    delta_sats INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (address, block_height, txid)) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value BLOB NOT NULL);
`;

const INDEX_DDL = `
CREATE INDEX IF NOT EXISTS idx_tx_block_idx ON transactions(block_height, tx_index);
CREATE INDEX IF NOT EXISTS idx_txout_address ON tx_outputs(address) WHERE address IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_txout_unspent ON tx_outputs(address) WHERE spent = 0 AND address IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_txin_prevout ON tx_inputs(prev_txid, prev_vout) WHERE prev_txid IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_addr_bal_rank ON address_balances(balance_sats DESC);
CREATE INDEX IF NOT EXISTS idx_addr_hist ON address_history(address, block_height DESC);
CREATE INDEX IF NOT EXISTS idx_utxo_height ON utxos(height);
`;

async function main() {
  const FRESH = process.env.FRESH === '1';
  if (FRESH) {
    if (existsSync(DB_PATH)) unlinkSync(DB_PATH);
    ['-wal', '-shm'].forEach(s => { if (existsSync(DB_PATH + s)) unlinkSync(DB_PATH + s); });
  }

  const db = new Database(DB_PATH);
  db.pragma('journal_mode = WAL');
  db.pragma('synchronous = NORMAL');
  db.pragma('cache_size = -65536');
  db.pragma('mmap_size = 268435456');
  db.pragma('temp_store = MEMORY');
  db.exec(SCHEMA_DDL);
  db.exec(INDEX_DDL);

  const insertBlock = db.prepare(`INSERT OR REPLACE INTO block_index(height,hash,prev_hash,time,n_tx,size,status,merkle_root,difficulty,version) VALUES(@height,@hash,@prev_hash,@time,@n_tx,@size,1,@merkle_root,@difficulty,@version)`);
  const insertTx = db.prepare(`INSERT OR REPLACE INTO transactions(txid,block_height,tx_index,size,version,locktime,is_coinbase) VALUES(@txid,@block_height,@tx_index,@size,@version,@locktime,@is_coinbase)`);
  const insertInput = db.prepare(`INSERT OR REPLACE INTO tx_inputs(txid,vin,prev_txid,prev_vout,value_sats,address,coinbase) VALUES(@txid,@vin,@prev_txid,@prev_vout,@value_sats,@address,@coinbase)`);
  const insertOutput = db.prepare(`INSERT OR REPLACE INTO tx_outputs(txid,vout,value_sats,script_hex,script_type,address,spent) VALUES(@txid,@vout,@value_sats,@script_hex,@script_type,@address,0)`);
  const insertUtxo = db.prepare(`INSERT OR REPLACE INTO utxos(txid,vout,height,coinbase,amount,script) VALUES(@txid,@vout,@height,@coinbase,@amount,@script)`);
  const removeUtxo = db.prepare(`DELETE FROM utxos WHERE txid=@txid AND vout=@vout`);
  const markSpent = db.prepare(`UPDATE tx_outputs SET spent=1,spent_txid=@spent_txid,spent_vin=@spent_vin WHERE txid=@txid AND vout=@vout`);
  const upsertAddr = db.prepare(`INSERT INTO address_balances(address,balance_sats,received_sats,sent_sats,tx_count,utxo_count,first_height,last_height) VALUES(@address,@delta,@received,@sent,1,@utxo_delta,@height,@height) ON CONFLICT(address) DO UPDATE SET balance_sats=balance_sats+@delta,received_sats=received_sats+@received,sent_sats=sent_sats+@sent,tx_count=tx_count+1,utxo_count=utxo_count+@utxo_delta,last_height=MAX(last_height,@height)`);
  const insertHistory = db.prepare(`INSERT OR REPLACE INTO address_history(address,block_height,txid,delta_sats) VALUES(@address,@block_height,@txid,@delta_sats)`);
  const insertMeta = db.prepare(`INSERT OR REPLACE INTO meta(key,value) VALUES(?,?)`);

  const indexBlock = db.transaction((blk, txList) => {
    insertBlock.run({ height: blk.height, hash: hexToBuffer(blk.hash), prev_hash: hexToBuffer(blk.previousblockhash||''), time: blk.time, n_tx: blk.nTx, size: blk.size, merkle_root: hexToBuffer(blk.merkleroot||''), difficulty: blk.difficulty||0, version: blk.version||0 });
    for (let ti = 0; ti < txList.length; ti++) {
      const tx = txList[ti];
      const txBuf = hexToBuffer(tx.txid);
      const isCb = tx.vin?.[0]?.coinbase !== undefined ? 1 : 0;
      insertTx.run({ txid: txBuf, block_height: blk.height, tx_index: ti, size: tx.size||0, version: tx.version||0, locktime: tx.locktime||0, is_coinbase: isCb });
      const deltas = new Map();
      for (let vi = 0; vi < (tx.vin||[]).length; vi++) {
        const inp = tx.vin[vi]; const cb = inp.coinbase!==undefined?1:0;
        let addr=null, val=0;
        if (inp.txid && inp.vout!==undefined) {
          const prev = db.prepare('SELECT value_sats,address FROM tx_outputs WHERE txid=? AND vout=?').get(hexToBuffer(inp.txid), inp.vout);
          if (prev) { addr=prev.address; val=prev.value_sats; }
        }
        insertInput.run({ txid:txBuf, vin:vi, prev_txid:inp.txid?hexToBuffer(inp.txid):null, prev_vout:inp.vout??null, value_sats:val, address:addr, coinbase:cb });
        if (inp.txid && inp.vout!==undefined) {
          markSpent.run({ spent_txid:txBuf, spent_vin:vi, txid:hexToBuffer(inp.txid), vout:inp.vout });
          removeUtxo.run({ txid:hexToBuffer(inp.txid), vout:inp.vout });
        }
        if (addr && val>0) { const d=deltas.get(addr)||{r:0,s:0,u:0}; d.s+=val; d.u-=1; deltas.set(addr,d); }
      }
      for (let vo = 0; vo < (tx.vout||[]).length; vo++) {
        const out = tx.vout[vo]; const v = satsFromValue(out.value);
        const sh = out.scriptPubKey?.hex||''; const st = out.scriptPubKey?.type||'';
        const addrs = out.scriptPubKey?.addresses||[]; const addr = addrs[0]||null;
        insertOutput.run({ txid:txBuf, vout:vo, value_sats:v, script_hex:sh, script_type:st, address:addr });
        if (v>0) insertUtxo.run({ txid:txBuf, vout:vo, height:blk.height, coinbase:isCb, amount:v, script:hexToBuffer(sh) });
        if (addr&&v>0) { const d=deltas.get(addr)||{r:0,s:0,u:0}; d.r+=v; d.u+=1; deltas.set(addr,d); }
      }
      for (const [addr,d] of deltas) {
        upsertAddr.run({ address:addr, delta:d.r-d.s, received:d.r, sent:d.s, utxo_delta:d.u, height:blk.height });
        insertHistory.run({ address:addr, block_height:blk.height, txid:txBuf, delta_sats:d.r-d.s });
      }
    }
    insertMeta.run('best_block_height', Buffer.from(String(blk.height)));
    insertMeta.run('best_block_hash', hexToBuffer(blk.hash));
  });

  const existing = db.prepare("SELECT value FROM meta WHERE key='best_block_height'").get();
  let nextHeight = existing ? parseInt(existing.value.toString(), 10) + 1 : 1;
  if (nextHeight > 1) console.log(`Resuming from block ${nextHeight}`);

  const results = [];

  console.log('Round | Synced  | Range            | Blocks | Txs    | Inputs | Outputs | UTXOs  | Addrs | DB MB  | Idx avg ms | Idx p95 ms | Idx max ms | blocks/s | Chain breaks | UTXO ok');
  console.log('------|---------|------------------|--------|--------|--------|---------|--------|-------|--------|------------|------------|------------|----------|--------------|--------');

  for (let round = 1; round <= ROUNDS; round++) {
    const chainInfo = await rpc('getblockchaininfo');
    const available = chainInfo.blocks;

    if (nextHeight > available) {
      console.log(`R${round}: waiting for more blocks (at ${available}, need ${nextHeight})...`);
      await new Promise(r => setTimeout(r, PAUSE_SEC * 1000));
      continue;
    }

    const endH = Math.min(nextHeight + BATCH - 1, available);
    const count = endH - nextHeight + 1;
    let totalTxs = 0, totalIn = 0, totalOut = 0, errors = 0;
    const idxTimes = [];

    const t0 = performance.now();
    for (let h = nextHeight; h <= endH; h++) {
      try {
        const hash = await rpc('getblockhash', [h]);
        const blk = await rpc('getblock', [hash, 1]);
        const txList = [];
        for (const txid of (blk.tx||[])) {
          try { txList.push(await rpc('getrawtransaction', [txid, true, hash])); }
          catch(_) { errors++; }
        }
        const s = performance.now();
        indexBlock(blk, txList);
        idxTimes.push(performance.now() - s);
        totalTxs += txList.length;
        totalIn += txList.reduce((a,t) => a+(t.vin||[]).length, 0);
        totalOut += txList.reduce((a,t) => a+(t.vout||[]).length, 0);
      } catch(_) { errors++; }
    }
    const elapsed = performance.now() - t0;

    const counts = {};
    for (const t of ['block_index','transactions','tx_inputs','tx_outputs','utxos','address_balances','address_history'])
      counts[t] = db.prepare(`SELECT COUNT(*) as c FROM ${t}`).get().c;

    const chainBreaks = db.prepare(`SELECT COUNT(*) as c FROM block_index b1 JOIN block_index b2 ON b2.height=b1.height-1 WHERE b1.prev_hash!=b2.hash`).get().c;
    const utxoMissing = db.prepare(`SELECT COUNT(*) as c FROM tx_outputs o LEFT JOIN utxos u ON u.txid=o.txid AND u.vout=o.vout WHERE o.spent=0 AND o.value_sats>0 AND u.txid IS NULL`).get().c;
    const spentInUtxo = db.prepare(`SELECT COUNT(*) as c FROM tx_outputs o JOIN utxos u ON u.txid=o.txid AND u.vout=o.vout WHERE o.spent=1`).get().c;

    const pages = db.prepare("SELECT page_count*page_size as s FROM pragma_page_count(),pragma_page_size()").get().s;
    const dbMB = (pages/1024/1024).toFixed(2);

    idxTimes.sort((a,b)=>a-b);
    const avg = idxTimes.length>0 ? idxTimes.reduce((a,b)=>a+b,0)/idxTimes.length : 0;
    const p95 = idxTimes.length>0 ? idxTimes[Math.floor(idxTimes.length*0.95)] : 0;
    const max = idxTimes.length>0 ? idxTimes[idxTimes.length-1] : 0;
    const bps = (count / (elapsed / 1000)).toFixed(1);

    const utxoOk = utxoMissing === 0 && spentInUtxo === 0 ? 'OK' : `MISS:${utxoMissing} STALE:${spentInUtxo}`;

    const row = {
      round, synced: available, from: nextHeight, to: endH, blocks: count,
      txs: totalTxs, inputs: totalIn, outputs: totalOut,
      dbBlocks: counts.block_index, dbTxs: counts.transactions,
      dbUtxos: counts.utxos, dbAddrs: counts.address_balances,
      dbMB, avgMs: avg.toFixed(2), p95Ms: p95.toFixed(2), maxMs: max.toFixed(2),
      bps, chainBreaks, utxoOk, errors,
    };
    results.push(row);

    console.log(
      `${String(round).padStart(5)} | ${String(available).padStart(7)} | ${String(nextHeight).padStart(7)}-${String(endH).padStart(7)} | ${String(count).padStart(6)} | ${String(totalTxs).padStart(6)} | ${String(totalIn).padStart(6)} | ${String(totalOut).padStart(7)} | ${String(counts.utxos).padStart(6)} | ${String(counts.address_balances).padStart(5)} | ${dbMB.padStart(6)} | ${avg.toFixed(2).padStart(10)} | ${p95.toFixed(2).padStart(10)} | ${max.toFixed(2).padStart(10)} | ${bps.padStart(8)} | ${String(chainBreaks).padStart(12)} | ${utxoOk}`
    );

    nextHeight = endH + 1;

    if (round < ROUNDS) {
      await new Promise(r => setTimeout(r, PAUSE_SEC * 1000));
    }
  }

  // Final summary
  console.log();
  console.log('=== Final DB state ===');
  for (const t of ['block_index','transactions','tx_inputs','tx_outputs','utxos','address_balances','address_history']) {
    const c = db.prepare(`SELECT COUNT(*) as c FROM ${t}`).get().c;
    console.log(`  ${t.padEnd(20)}: ${c}`);
  }
  const totalSupply = db.prepare('SELECT SUM(amount) as s FROM utxos').get().s;
  console.log(`  total_supply_sats   : ${totalSupply}`);
  const richest = db.prepare('SELECT address, balance_sats, tx_count FROM address_balances ORDER BY balance_sats DESC LIMIT 5').all();
  console.log('  Top 5 addresses:');
  for (const r of richest) console.log(`    ${r.address}  balance=${r.balance_sats}  txs=${r.tx_count}`);

  db.close();
}

main().catch(err => { console.error('Fatal:', err); process.exit(1); });
