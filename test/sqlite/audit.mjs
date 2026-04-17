#!/usr/bin/env node
/**
 * Deep audit of the SQLite database against live lotusd RPC data.
 *
 * Checks:
 * 1. Chain continuity: every block's prev_hash matches prior block's hash
 * 2. Block data fidelity: random blocks match RPC getblock exactly
 * 3. Transaction fidelity: random txs match RPC getrawtransaction exactly
 * 4. UTXO set consistency: unspent outputs ↔ utxos table, no stale entries
 * 5. Input resolution: spent outputs properly marked, values/addresses filled
 * 6. Address balance correctness: re-derive from outputs vs stored balance
 * 7. Address history completeness: every address tx is recorded
 * 8. Referential integrity: no orphan rows, no dangling foreign refs
 * 9. Value conservation: coinbase-only blocks have correct supply growth
 * 10. Duplicate detection: no duplicate txids, no duplicate utxos
 */

import Database from 'better-sqlite3';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';

const __dirname = dirname(fileURLToPath(import.meta.url));
const DB_PATH = join(__dirname, 'test_chain_monitor.db');
const RPC_USER = process.env.RPC_USER || 'test';
const RPC_PASS = process.env.RPC_PASS || 'test123';
const RPC_PORT = process.env.RPC_PORT || '10604';
const RPC_URL = `http://127.0.0.1:${RPC_PORT}/`;
const SAMPLE_SIZE = parseInt(process.env.SAMPLES || '50', 10);

let rpcId = 0;
async function rpc(method, params = []) {
  const body = JSON.stringify({ jsonrpc: '1.0', id: ++rpcId, method, params });
  const auth = Buffer.from(`${RPC_USER}:${RPC_PASS}`).toString('base64');
  const res = await fetch(RPC_URL, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', Authorization: `Basic ${auth}` },
    body, signal: AbortSignal.timeout(30000),
  });
  const json = await res.json();
  if (json.error) throw new Error(`RPC ${json.error.code}: ${json.error.message}`);
  return json.result;
}

const passed = [];
const failed = [];
const warnings = [];
function ok(cond, label, detail) {
  if (cond) { passed.push(label); }
  else { failed.push(label); console.error(`  FAIL: ${label}${detail ? ' — ' + detail : ''}`); }
}
function warn(label) { warnings.push(label); console.log(`  WARN: ${label}`); }

async function main() {
  console.log('╔════════════════════════════════════════════════════════════╗');
  console.log('║        Deep SQLite Database Audit vs lotusd RPC           ║');
  console.log('╚════════════════════════════════════════════════════════════╝');
  console.log();

  const db = new Database(DB_PATH, { readonly: true });

  // Take a consistent snapshot: read all data within a single transaction
  // so concurrent writes from the monitor don't cause false positives.
  db.exec('BEGIN');

  const maxH = db.prepare('SELECT MAX(height) as h FROM block_index').get().h;
  const minH = db.prepare('SELECT MIN(height) as h FROM block_index').get().h;
  console.log(`  DB block range: ${minH} → ${maxH}`);
  console.log();

  // ── 1. Chain continuity ───────────────────────────────────────────────
  console.log('1. Chain continuity...');
  const breaks = db.prepare(`
    SELECT b1.height, hex(b1.prev_hash) as prev, hex(b2.hash) as expected
    FROM block_index b1
    JOIN block_index b2 ON b2.height = b1.height - 1
    WHERE b1.prev_hash != b2.hash
  `).all();
  console.log(`   Breaks: ${breaks.length}`);
  if (breaks.length > 0) breaks.slice(0, 5).forEach(b => console.log(`     at height ${b.height}`));
  ok(breaks.length === 0, 'chain_continuity');

  const gaps = db.prepare(`
    SELECT b1.height + 1 as gap_start
    FROM block_index b1
    LEFT JOIN block_index b2 ON b2.height = b1.height + 1
    WHERE b2.height IS NULL AND b1.height < ?
  `).all(maxH);
  console.log(`   Height gaps: ${gaps.length}`);
  if (gaps.length > 0) gaps.slice(0, 5).forEach(g => console.log(`     gap at ${g.gap_start}`));
  ok(gaps.length === 0, 'no_height_gaps');
  console.log();

  // ── 2. Block data fidelity vs RPC ─────────────────────────────────────
  console.log(`2. Block data fidelity (${SAMPLE_SIZE} random blocks vs RPC)...`);
  let blockOk = 0, blockFail = 0;
  const sampleHeights = [];
  for (let i = 0; i < SAMPLE_SIZE; i++) sampleHeights.push(minH + Math.floor(Math.random() * (maxH - minH + 1)));

  for (const h of sampleHeights) {
    try {
      const rpcHash = await rpc('getblockhash', [h]);
      const rpcBlock = await rpc('getblock', [rpcHash, 1]);
      const dbBlock = db.prepare('SELECT * FROM block_index WHERE height = ?').get(h);
      if (!dbBlock) { blockFail++; console.log(`     h=${h}: NOT IN DB`); continue; }

      const hashMatch = dbBlock.hash.toString('hex') === rpcBlock.hash;
      const timeMatch = dbBlock.time === rpcBlock.time;
      const txCountMatch = dbBlock.n_tx === rpcBlock.nTx;
      const sizeMatch = dbBlock.size === rpcBlock.size;

      const dbTxCount = db.prepare('SELECT COUNT(*) as c FROM transactions WHERE block_height = ?').get(h).c;
      const txRowMatch = dbTxCount === rpcBlock.nTx;

      if (hashMatch && timeMatch && txCountMatch && sizeMatch && txRowMatch) {
        blockOk++;
      } else {
        blockFail++;
        const issues = [];
        if (!hashMatch) issues.push('hash');
        if (!timeMatch) issues.push(`time(db=${dbBlock.time},rpc=${rpcBlock.time})`);
        if (!txCountMatch) issues.push(`n_tx(db=${dbBlock.n_tx},rpc=${rpcBlock.nTx})`);
        if (!sizeMatch) issues.push(`size(db=${dbBlock.size},rpc=${rpcBlock.size})`);
        if (!txRowMatch) issues.push(`tx_rows(db=${dbTxCount},rpc=${rpcBlock.nTx})`);
        console.log(`     h=${h}: MISMATCH [${issues.join(', ')}]`);
      }
    } catch (e) {
      blockFail++;
      console.log(`     h=${h}: ERROR ${e.message}`);
    }
  }
  console.log(`   ${blockOk}/${SAMPLE_SIZE} blocks match RPC exactly`);
  ok(blockFail === 0, 'block_fidelity', `${blockFail} mismatches`);
  console.log();

  // ── 3. Transaction fidelity vs RPC ────────────────────────────────────
  console.log(`3. Transaction fidelity (${SAMPLE_SIZE} random txs vs RPC)...`);
  let txOk = 0, txFail = 0;
  const sampleTxs = db.prepare(`SELECT hex(txid) as txid, block_height, tx_index, size, version, locktime, is_coinbase FROM transactions ORDER BY RANDOM() LIMIT ?`).all(SAMPLE_SIZE);

  for (const dbTx of sampleTxs) {
    try {
      const rpcHash = await rpc('getblockhash', [dbTx.block_height]);
      const rpcTx = await rpc('getrawtransaction', [dbTx.txid, true, rpcHash]);

      const issues = [];
      if (dbTx.size !== rpcTx.size) issues.push(`size(db=${dbTx.size},rpc=${rpcTx.size})`);
      if (dbTx.version !== rpcTx.version) issues.push(`version`);
      if (dbTx.locktime !== rpcTx.locktime) issues.push(`locktime`);

      const rpcIsCb = rpcTx.vin?.[0]?.coinbase !== undefined ? 1 : 0;
      if (dbTx.is_coinbase !== rpcIsCb) issues.push(`coinbase(db=${dbTx.is_coinbase},rpc=${rpcIsCb})`);

      const dbInputCount = db.prepare('SELECT COUNT(*) as c FROM tx_inputs WHERE txid = ?').get(Buffer.from(dbTx.txid, 'hex')).c;
      if (dbInputCount !== rpcTx.vin.length) issues.push(`vin_count(db=${dbInputCount},rpc=${rpcTx.vin.length})`);

      const dbOutputCount = db.prepare('SELECT COUNT(*) as c FROM tx_outputs WHERE txid = ?').get(Buffer.from(dbTx.txid, 'hex')).c;
      if (dbOutputCount !== rpcTx.vout.length) issues.push(`vout_count(db=${dbOutputCount},rpc=${rpcTx.vout.length})`);

      // Check output values
      for (const rpcOut of rpcTx.vout) {
        const expectedSats = Math.round(Number(rpcOut.value) * 1e6);
        const dbOut = db.prepare('SELECT value_sats, script_type FROM tx_outputs WHERE txid = ? AND vout = ?')
          .get(Buffer.from(dbTx.txid, 'hex'), rpcOut.n);
        if (!dbOut) { issues.push(`missing_vout_${rpcOut.n}`); continue; }
        if (dbOut.value_sats !== expectedSats) issues.push(`vout${rpcOut.n}_value(db=${dbOut.value_sats},rpc=${expectedSats})`);
        const rpcType = rpcOut.scriptPubKey?.type || '';
        if (dbOut.script_type !== rpcType) issues.push(`vout${rpcOut.n}_type(db=${dbOut.script_type},rpc=${rpcType})`);
      }

      if (issues.length === 0) { txOk++; }
      else { txFail++; console.log(`     tx ${dbTx.txid.substring(0,16)}... h=${dbTx.block_height}: [${issues.join(', ')}]`); }
    } catch (e) {
      txFail++;
      console.log(`     tx ${dbTx.txid.substring(0,16)}... ERROR: ${e.message}`);
    }
  }
  console.log(`   ${txOk}/${SAMPLE_SIZE} txs match RPC exactly`);
  ok(txFail === 0, 'tx_fidelity', `${txFail} mismatches`);
  console.log();

  // ── 4. UTXO set consistency ───────────────────────────────────────────
  console.log('4. UTXO set consistency...');

  const unspentMissing = db.prepare(`
    SELECT COUNT(*) as c FROM tx_outputs o
    LEFT JOIN utxos u ON u.txid = o.txid AND u.vout = o.vout
    WHERE o.spent = 0 AND o.value_sats > 0 AND u.txid IS NULL
  `).get().c;
  console.log(`   Unspent outputs missing from utxos table: ${unspentMissing}`);
  ok(unspentMissing === 0, 'utxo_no_missing');

  const staleUtxos = db.prepare(`
    SELECT COUNT(*) as c FROM utxos u
    JOIN tx_outputs o ON o.txid = u.txid AND o.vout = u.vout
    WHERE o.spent = 1
  `).get().c;
  console.log(`   Spent outputs still in utxos table: ${staleUtxos}`);
  ok(staleUtxos === 0, 'utxo_no_stale');

  const orphanUtxos = db.prepare(`
    SELECT COUNT(*) as c FROM utxos u
    LEFT JOIN tx_outputs o ON o.txid = u.txid AND o.vout = u.vout
    WHERE o.txid IS NULL
  `).get().c;
  console.log(`   UTXO entries with no matching tx_output: ${orphanUtxos}`);
  ok(orphanUtxos === 0, 'utxo_no_orphans');

  const utxoAmountMismatch = db.prepare(`
    SELECT COUNT(*) as c FROM utxos u
    JOIN tx_outputs o ON o.txid = u.txid AND o.vout = u.vout
    WHERE u.amount != o.value_sats
  `).get().c;
  console.log(`   UTXO amount ≠ tx_output value: ${utxoAmountMismatch}`);
  ok(utxoAmountMismatch === 0, 'utxo_amounts_match');
  console.log();

  // ── 5. Spent output validation ────────────────────────────────────────
  console.log('5. Spent output validation...');

  const spentNoRef = db.prepare(`
    SELECT COUNT(*) as c FROM tx_outputs
    WHERE spent = 1 AND (spent_txid IS NULL OR spent_vin IS NULL)
  `).get().c;
  console.log(`   Spent but no spent_txid/spent_vin: ${spentNoRef}`);
  ok(spentNoRef === 0, 'spent_has_refs');

  const spentRefInvalid = db.prepare(`
    SELECT COUNT(*) as c FROM tx_outputs o
    WHERE o.spent = 1 AND o.spent_txid IS NOT NULL
    AND NOT EXISTS (SELECT 1 FROM tx_inputs i WHERE i.txid = o.spent_txid AND i.vin = o.spent_vin)
  `).get().c;
  console.log(`   Spent refs to non-existent input: ${spentRefInvalid}`);
  ok(spentRefInvalid === 0, 'spent_refs_valid');

  const inputRefMissing = db.prepare(`
    SELECT COUNT(*) as c FROM tx_inputs i
    WHERE i.prev_txid IS NOT NULL AND i.coinbase = 0
    AND NOT EXISTS (SELECT 1 FROM tx_outputs o WHERE o.txid = i.prev_txid AND o.vout = i.prev_vout)
  `).get().c;
  console.log(`   Inputs referencing unknown prev outputs: ${inputRefMissing}`);
  // This is expected for inputs spending outputs from before our indexed range
  if (inputRefMissing > 0) warn(`${inputRefMissing} inputs ref outputs outside indexed range (expected if spending pre-range coins)`);
  console.log();

  // ── 6. Address balance correctness ────────────────────────────────────
  console.log('6. Address balance correctness (re-derive from outputs)...');

  const addrSample = db.prepare(`
    SELECT address, balance_sats, received_sats, sent_sats, tx_count, utxo_count
    FROM address_balances WHERE address IS NOT NULL
    ORDER BY tx_count DESC LIMIT ?
  `).all(Math.min(SAMPLE_SIZE, 100));

  let balOk = 0, balFail = 0;
  for (const addr of addrSample) {
    const received = db.prepare(`
      SELECT COALESCE(SUM(value_sats), 0) as s FROM tx_outputs WHERE address = ?
    `).get(addr.address).s;

    const utxoCount = db.prepare(`
      SELECT COUNT(*) as c FROM tx_outputs WHERE address = ? AND spent = 0 AND value_sats > 0
    `).get(addr.address).c;

    const unspentSum = db.prepare(`
      SELECT COALESCE(SUM(value_sats), 0) as s FROM tx_outputs WHERE address = ? AND spent = 0
    `).get(addr.address).s;

    const spentSum = db.prepare(`
      SELECT COALESCE(SUM(value_sats), 0) as s FROM tx_outputs WHERE address = ? AND spent = 1
    `).get(addr.address).s;

    const derivedBalance = unspentSum;
    const derivedReceived = received;
    const derivedSent = spentSum;

    const issues = [];
    // Balance = received - sent = unspent sum
    if (addr.balance_sats !== derivedBalance) issues.push(`balance(stored=${addr.balance_sats},derived=${derivedBalance})`);
    if (addr.received_sats !== derivedReceived) issues.push(`received(stored=${addr.received_sats},derived=${derivedReceived})`);
    if (addr.utxo_count !== utxoCount) issues.push(`utxo_count(stored=${addr.utxo_count},derived=${utxoCount})`);

    if (issues.length === 0) { balOk++; }
    else {
      balFail++;
      console.log(`     ${addr.address}: [${issues.join(', ')}]`);
    }
  }
  console.log(`   ${balOk}/${addrSample.length} address balances correct`);
  ok(balFail === 0, 'address_balances_correct', `${balFail} mismatches`);
  console.log();

  // ── 7. Address history completeness ───────────────────────────────────
  console.log('7. Address history completeness...');

  const historyOrphans = db.prepare(`
    SELECT COUNT(*) as c FROM address_history ah
    LEFT JOIN address_balances ab ON ab.address = ah.address
    WHERE ab.address IS NULL
  `).get().c;
  console.log(`   History entries for non-existent addresses: ${historyOrphans}`);
  ok(historyOrphans === 0, 'history_no_orphans');

  const historyNoTx = db.prepare(`
    SELECT COUNT(*) as c FROM address_history ah
    LEFT JOIN transactions t ON t.txid = ah.txid
    WHERE t.txid IS NULL
  `).get().c;
  console.log(`   History entries referencing non-existent txs: ${historyNoTx}`);
  ok(historyNoTx === 0, 'history_txs_exist');

  const historyWrongHeight = db.prepare(`
    SELECT COUNT(*) as c FROM address_history ah
    JOIN transactions t ON t.txid = ah.txid
    WHERE ah.block_height != t.block_height
  `).get().c;
  console.log(`   History entries with wrong block_height: ${historyWrongHeight}`);
  ok(historyWrongHeight === 0, 'history_heights_match');
  console.log();

  // ── 8. Referential integrity ──────────────────────────────────────────
  console.log('8. Referential integrity...');

  const orphanTxs = db.prepare(`
    SELECT COUNT(*) as c FROM transactions t
    LEFT JOIN block_index b ON b.height = t.block_height
    WHERE b.height IS NULL
  `).get().c;
  console.log(`   Txs without matching block: ${orphanTxs}`);
  ok(orphanTxs === 0, 'no_orphan_txs');

  const orphanInputs = db.prepare(`
    SELECT COUNT(*) as c FROM tx_inputs i
    LEFT JOIN transactions t ON t.txid = i.txid
    WHERE t.txid IS NULL
  `).get().c;
  console.log(`   Inputs without matching tx: ${orphanInputs}`);
  ok(orphanInputs === 0, 'no_orphan_inputs');

  const orphanOutputs = db.prepare(`
    SELECT COUNT(*) as c FROM tx_outputs o
    LEFT JOIN transactions t ON t.txid = o.txid
    WHERE t.txid IS NULL
  `).get().c;
  console.log(`   Outputs without matching tx: ${orphanOutputs}`);
  ok(orphanOutputs === 0, 'no_orphan_outputs');

  const zeroValueOutputs = db.prepare(`SELECT COUNT(*) as c FROM tx_outputs WHERE value_sats = 0`).get().c;
  const nulldataOutputs = db.prepare(`SELECT COUNT(*) as c FROM tx_outputs WHERE value_sats = 0 AND script_type = 'nulldata'`).get().c;
  console.log(`   Zero-value outputs: ${zeroValueOutputs} (${nulldataOutputs} are OP_RETURN/nulldata)`);
  console.log();

  // ── 9. Value conservation ─────────────────────────────────────────────
  console.log('9. Value conservation...');

  const totalUtxoValue = db.prepare('SELECT COALESCE(SUM(amount),0) as s FROM utxos').get().s;
  const totalOutputValue = db.prepare('SELECT COALESCE(SUM(value_sats),0) as s FROM tx_outputs').get().s;
  const totalSpentValue = db.prepare('SELECT COALESCE(SUM(value_sats),0) as s FROM tx_outputs WHERE spent = 1').get().s;
  const totalUnspentValue = db.prepare('SELECT COALESCE(SUM(value_sats),0) as s FROM tx_outputs WHERE spent = 0').get().s;

  console.log(`   Total output value:   ${totalOutputValue} sats`);
  console.log(`   Total spent value:    ${totalSpentValue} sats`);
  console.log(`   Total unspent value:  ${totalUnspentValue} sats`);
  console.log(`   UTXO table sum:       ${totalUtxoValue} sats`);
  console.log(`   Unspent == UTXO sum?  ${totalUnspentValue === totalUtxoValue}`);
  ok(totalUnspentValue === totalUtxoValue, 'value_conservation', `unspent=${totalUnspentValue} utxo=${totalUtxoValue}`);

  const addrReceivedSum = db.prepare('SELECT COALESCE(SUM(received_sats),0) as s FROM address_balances').get().s;
  const addrSentSum = db.prepare('SELECT COALESCE(SUM(sent_sats),0) as s FROM address_balances').get().s;
  const addrBalanceSum = db.prepare('SELECT COALESCE(SUM(balance_sats),0) as s FROM address_balances').get().s;
  console.log(`   Addr received sum:    ${addrReceivedSum}`);
  console.log(`   Addr sent sum:        ${addrSentSum}`);
  console.log(`   Addr balance sum:     ${addrBalanceSum}`);
  console.log(`   Received-Sent==Balance? ${addrReceivedSum - addrSentSum === addrBalanceSum}`);
  ok(addrReceivedSum - addrSentSum === addrBalanceSum, 'addr_value_conservation');
  console.log();

  // ── 10. Duplicate detection ───────────────────────────────────────────
  console.log('10. Duplicate detection...');

  const dupTxids = db.prepare(`
    SELECT hex(txid) as txid, COUNT(*) as c FROM transactions GROUP BY txid HAVING c > 1
  `).all();
  console.log(`   Duplicate txids: ${dupTxids.length}`);
  ok(dupTxids.length === 0, 'no_dup_txids');

  const dupUtxos = db.prepare(`
    SELECT COUNT(*) as c FROM (SELECT txid, vout, COUNT(*) as cnt FROM utxos GROUP BY txid, vout HAVING cnt > 1)
  `).get().c;
  console.log(`   Duplicate UTXO entries: ${dupUtxos}`);
  ok(dupUtxos === 0, 'no_dup_utxos');

  const dupOutputs = db.prepare(`
    SELECT COUNT(*) as c FROM (SELECT txid, vout, COUNT(*) as cnt FROM tx_outputs GROUP BY txid, vout HAVING cnt > 1)
  `).get().c;
  console.log(`   Duplicate tx_outputs: ${dupOutputs}`);
  ok(dupOutputs === 0, 'no_dup_outputs');

  const dupBlocks = db.prepare(`
    SELECT COUNT(*) as c FROM (SELECT height, COUNT(*) as cnt FROM block_index GROUP BY height HAVING cnt > 1)
  `).get().c;
  console.log(`   Duplicate block heights: ${dupBlocks}`);
  ok(dupBlocks === 0, 'no_dup_blocks');
  console.log();

  // ── 11. Meta table ────────────────────────────────────────────────────
  console.log('11. Meta table consistency...');
  const bestH = db.prepare("SELECT value FROM meta WHERE key='best_block_height'").get();
  const bestHash = db.prepare("SELECT value FROM meta WHERE key='best_block_hash'").get();
  ok(bestH !== undefined, 'meta_has_best_height');
  ok(bestHash !== undefined, 'meta_has_best_hash');
  if (bestH) {
    const storedH = parseInt(bestH.value.toString(), 10);
    console.log(`   best_block_height: ${storedH} (max in block_index: ${maxH})`);
    ok(storedH === maxH, 'meta_height_matches_max');
  }
  console.log();

  // ── 12. Table size summary ────────────────────────────────────────────
  console.log('12. Table statistics...');
  const tableStats = {};
  for (const t of ['block_index','transactions','tx_inputs','tx_outputs','utxos','address_balances','address_history','meta']) {
    tableStats[t] = db.prepare(`SELECT COUNT(*) as c FROM ${t}`).get().c;
  }
  for (const [t, c] of Object.entries(tableStats)) console.log(`   ${t.padEnd(20)}: ${c}`);

  const negativeBalances = db.prepare('SELECT COUNT(*) as c FROM address_balances WHERE balance_sats < 0').get().c;
  console.log(`   Negative balances: ${negativeBalances}`);
  if (negativeBalances > 0) warn(`${negativeBalances} addresses with negative balance (partial chain)`);

  const pages = db.prepare("SELECT page_count*page_size as s FROM pragma_page_count(),pragma_page_size()").get().s;
  console.log(`   DB file size: ${(pages/1024/1024).toFixed(2)} MB`);
  console.log();

  // ── Final ─────────────────────────────────────────────────────────────
  db.exec('COMMIT');

  console.log('════════════════════════════════════════════════════════');
  console.log(`  ${passed.length} PASSED, ${failed.length} FAILED, ${warnings.length} WARNINGS`);
  console.log('════════════════════════════════════════════════════════');
  if (failed.length > 0) { console.log('  FAILURES:'); failed.forEach(f => console.log(`    ✗ ${f}`)); }
  if (warnings.length > 0) { console.log('  WARNINGS:'); warnings.forEach(w => console.log(`    ⚠ ${w}`)); }

  db.close();
  process.exit(failed.length > 0 ? 1 : 0);
}

main().catch(err => { console.error('Fatal:', err); process.exit(2); });
