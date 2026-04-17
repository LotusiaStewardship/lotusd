// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sqlite/block_analytics.h>

#include <chain.h>
#include <key_io.h>
#include <logging.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <script/standard.h>
#include <sqlite3.h>

#include <unordered_map>

std::unique_ptr<CBlockAnalytics> g_block_analytics;

static void BindHash256(sqlite3_stmt *stmt, int col, const uint256 &hash) {
    sqlite3_bind_blob(stmt, col, hash.begin(), 32, SQLITE_STATIC);
}

static std::string ScriptToAddress(const CScript &script,
                                   const CChainParams &params) {
    CTxDestination dest;
    if (ExtractDestination(script, dest)) {
        return EncodeDestination(dest, params);
    }
    return std::string();
}

static std::string ScriptToType(const CScript &script) {
    std::vector<std::vector<uint8_t>> solutions;
    TxoutType type = Solver(script, solutions);
    if (type == TxoutType::NONSTANDARD || type == TxoutType::NULL_DATA) {
        return std::string();
    }
    return GetTxnOutputType(type);
}

CBlockAnalytics::CBlockAnalytics(CSqliteWrapper &db)
    : m_db(db), m_addr_index(db) {}

void CBlockAnalytics::ConnectBlock(const CBlock &block,
                                   const CBlockIndex *pindex,
                                   const CChainParams &params) {
    if (!m_db.BeginTransaction()) {
        LogPrintf("ERROR: CBlockAnalytics::ConnectBlock: failed to begin "
                  "transaction at height %d\n",
                  pindex->nHeight);
        return;
    }

    sqlite3_stmt *ins_tx = m_db.Prepare(
        "INSERT OR IGNORE INTO transactions(txid, block_height, block_pos) "
        "VALUES(?1, ?2, ?3)");

    sqlite3_stmt *ins_out = m_db.Prepare(
        "INSERT OR IGNORE INTO tx_outputs("
        "txid, vout, value_sats, script, script_type, address, spent"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6, 0)");

    sqlite3_stmt *ins_in = m_db.Prepare(
        "INSERT OR IGNORE INTO tx_inputs("
        "txid, vin, prev_txid, prev_vout, value_sats, address"
        ") VALUES(?1, ?2, ?3, ?4, ?5, ?6)");

    sqlite3_stmt *upd_spent = m_db.Prepare(
        "UPDATE tx_outputs SET spent = 1, spent_txid = ?1, spent_vin = ?2 "
        "WHERE txid = ?3 AND vout = ?4");

    const int height = pindex->nHeight;

    // Pass 1: insert all transactions and outputs (mirrors AddCoins pass
    // in validation.cpp which adds all outputs before any spending).
    for (size_t txIdx = 0; txIdx < block.vtx.size(); txIdx++) {
        const CTransaction &tx = *block.vtx[txIdx];
        const TxId &txid = tx.GetId();

        BindHash256(ins_tx, 1, txid);
        sqlite3_bind_int(ins_tx, 2, height);
        sqlite3_bind_int(ins_tx, 3, static_cast<int>(txIdx));
        sqlite3_step(ins_tx);
        sqlite3_reset(ins_tx);

        for (size_t o = 0; o < tx.vout.size(); o++) {
            const CTxOut &out = tx.vout[o];
            int64_t value = out.nValue / SATOSHI;

            std::string addr = ScriptToAddress(out.scriptPubKey, params);
            std::string stype = ScriptToType(out.scriptPubKey);

            BindHash256(ins_out, 1, txid);
            sqlite3_bind_int(ins_out, 2, static_cast<int>(o));
            sqlite3_bind_int64(ins_out, 3, value);
            sqlite3_bind_blob(ins_out, 4, out.scriptPubKey.data(),
                              out.scriptPubKey.size(), SQLITE_STATIC);
            if (stype.empty()) {
                sqlite3_bind_null(ins_out, 5);
            } else {
                sqlite3_bind_text(ins_out, 5, stype.c_str(), -1,
                                  SQLITE_TRANSIENT);
            }
            if (addr.empty()) {
                sqlite3_bind_null(ins_out, 6);
            } else {
                sqlite3_bind_text(ins_out, 6, addr.c_str(), -1,
                                  SQLITE_TRANSIENT);
            }
            sqlite3_step(ins_out);
            sqlite3_reset(ins_out);
        }
    }

    // Pass 2: process inputs, mark spent, update address balances.
    // All outputs are now in tx_outputs so prevout lookups always succeed.
    sqlite3_stmt *q_prevout = m_db.Prepare(
        "SELECT value_sats, address FROM tx_outputs "
        "WHERE txid = ?1 AND vout = ?2");

    for (size_t txIdx = 0; txIdx < block.vtx.size(); txIdx++) {
        const CTransaction &tx = *block.vtx[txIdx];
        const TxId &txid = tx.GetId();
        const bool isCoinBase = tx.IsCoinBase();

        struct AddrAccum {
            int64_t received{0};
            int64_t sent{0};
            int utxo_created{0};
            int utxo_spent{0};
        };
        std::unordered_map<std::string, AddrAccum> addr_map;

        // Accumulate received for this tx's outputs
        for (const auto &out : tx.vout) {
            std::string addr = ScriptToAddress(out.scriptPubKey, params);
            if (!addr.empty()) {
                addr_map[addr].received += out.nValue / SATOSHI;
                addr_map[addr].utxo_created++;
            }
        }

        if (!isCoinBase) {
            for (size_t i = 0; i < tx.vin.size(); i++) {
                const COutPoint &prevout = tx.vin[i].prevout;

                int64_t value = 0;
                std::string addr;

                BindHash256(q_prevout, 1, prevout.GetTxId());
                sqlite3_bind_int(q_prevout, 2, prevout.GetN());
                if (sqlite3_step(q_prevout) == SQLITE_ROW) {
                    value = sqlite3_column_int64(q_prevout, 0);
                    const char *a = reinterpret_cast<const char *>(
                        sqlite3_column_text(q_prevout, 1));
                    if (a) {
                        addr = a;
                    }
                }
                sqlite3_reset(q_prevout);

                BindHash256(ins_in, 1, txid);
                sqlite3_bind_int(ins_in, 2, static_cast<int>(i));
                BindHash256(ins_in, 3, prevout.GetTxId());
                sqlite3_bind_int(ins_in, 4, prevout.GetN());
                sqlite3_bind_int64(ins_in, 5, value);
                if (addr.empty()) {
                    sqlite3_bind_null(ins_in, 6);
                } else {
                    sqlite3_bind_text(ins_in, 6, addr.c_str(), -1,
                                      SQLITE_TRANSIENT);
                    addr_map[addr].sent += value;
                    addr_map[addr].utxo_spent++;
                }
                sqlite3_step(ins_in);
                sqlite3_reset(ins_in);

                BindHash256(upd_spent, 1, txid);
                sqlite3_bind_int(upd_spent, 2, static_cast<int>(i));
                BindHash256(upd_spent, 3, prevout.GetTxId());
                sqlite3_bind_int(upd_spent, 4, prevout.GetN());
                sqlite3_step(upd_spent);
                sqlite3_reset(upd_spent);
            }
        }

        for (const auto &[addr, acc] : addr_map) {
            int utxo_delta = acc.utxo_created - acc.utxo_spent;
            m_addr_index.UpdateForTx(addr, height, txid.begin(),
                                     acc.received, acc.sent, utxo_delta);
        }
    }

    if (!m_db.CommitTransaction()) {
        LogPrintf("ERROR: CBlockAnalytics::ConnectBlock: failed to commit "
                  "at height %d\n",
                  pindex->nHeight);
    }
}

void CBlockAnalytics::DisconnectBlock(const CBlock &block,
                                      const CBlockIndex *pindex,
                                      const CChainParams &params) {
    if (!m_db.BeginTransaction()) {
        LogPrintf("ERROR: CBlockAnalytics::DisconnectBlock: failed to begin "
                  "transaction at height %d\n",
                  pindex->nHeight);
        return;
    }

    sqlite3_stmt *unspend = m_db.Prepare(
        "UPDATE tx_outputs SET spent = 0, spent_txid = NULL, spent_vin = NULL "
        "WHERE txid = ?1 AND vout = ?2");

    sqlite3_stmt *del_inputs = m_db.Prepare(
        "DELETE FROM tx_inputs WHERE txid = ?1");

    sqlite3_stmt *del_outputs = m_db.Prepare(
        "DELETE FROM tx_outputs WHERE txid = ?1");

    sqlite3_stmt *del_tx = m_db.Prepare(
        "DELETE FROM transactions WHERE txid = ?1");

    const int height = pindex->nHeight;

    // Process in reverse order for consistency
    for (auto it = block.vtx.rbegin(); it != block.vtx.rend(); ++it) {
        const CTransaction &tx = **it;
        const TxId &txid = tx.GetId();
        const bool isCoinBase = tx.IsCoinBase();

        // Rebuild per-address accumulators to reverse address index
        struct AddrAccum {
            int64_t received{0};
            int64_t sent{0};
            int utxo_created{0};
            int utxo_spent{0};
        };
        std::unordered_map<std::string, AddrAccum> addr_map;

        // Accumulate output addresses
        for (size_t o = 0; o < tx.vout.size(); o++) {
            const CTxOut &out = tx.vout[o];
            std::string addr = ScriptToAddress(out.scriptPubKey, params);
            if (!addr.empty()) {
                int64_t value = out.nValue / SATOSHI;
                addr_map[addr].received += value;
                addr_map[addr].utxo_created++;
            }
        }

        // Restore spent outputs and accumulate input addresses
        if (!isCoinBase) {
            for (size_t i = 0; i < tx.vin.size(); i++) {
                const COutPoint &prevout = tx.vin[i].prevout;

                // Restore the spent output
                BindHash256(unspend, 1, prevout.GetTxId());
                sqlite3_bind_int(unspend, 2, prevout.GetN());
                sqlite3_step(unspend);
                sqlite3_reset(unspend);

                // Get value/address from our own tx_inputs table
                sqlite3_stmt *q = m_db.Prepare(
                    "SELECT value_sats, address FROM tx_inputs "
                    "WHERE txid = ?1 AND vin = ?2");
                BindHash256(q, 1, txid);
                sqlite3_bind_int(q, 2, static_cast<int>(i));
                if (sqlite3_step(q) == SQLITE_ROW) {
                    int64_t value = sqlite3_column_int64(q, 0);
                    const char *a = reinterpret_cast<const char *>(
                        sqlite3_column_text(q, 1));
                    if (a) {
                        std::string addr(a);
                        addr_map[addr].sent += value;
                        addr_map[addr].utxo_spent++;
                    }
                }
                sqlite3_reset(q);
            }
        }

        // Reverse address effects
        for (const auto &[addr, acc] : addr_map) {
            int utxo_delta = acc.utxo_created - acc.utxo_spent;
            m_addr_index.UndoForTx(addr, height, txid.begin(),
                                   acc.received, acc.sent, utxo_delta);
        }

        // Delete rows
        BindHash256(del_inputs, 1, txid);
        sqlite3_step(del_inputs);
        sqlite3_reset(del_inputs);

        BindHash256(del_outputs, 1, txid);
        sqlite3_step(del_outputs);
        sqlite3_reset(del_outputs);

        BindHash256(del_tx, 1, txid);
        sqlite3_step(del_tx);
        sqlite3_reset(del_tx);
    }

    if (!m_db.CommitTransaction()) {
        LogPrintf("ERROR: CBlockAnalytics::DisconnectBlock: failed to commit "
                  "at height %d\n",
                  pindex->nHeight);
    }
}
