// Copyright (c) 2024 The Logos Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sqlite/schema.h>

#include <sqlite3.h>
#include <stdexcept>
#include <string>

namespace sqlite_schema {

const char *UTXO_TABLE = R"(
CREATE TABLE IF NOT EXISTS utxos (
    txid        BLOB NOT NULL,
    vout        INTEGER NOT NULL,
    height      INTEGER NOT NULL,
    coinbase    INTEGER NOT NULL DEFAULT 0,
    amount      INTEGER NOT NULL,
    script      BLOB NOT NULL,
    PRIMARY KEY (txid, vout)
) WITHOUT ROWID;
)";

const char *BLOCK_INDEX_TABLE = R"(
CREATE TABLE IF NOT EXISTS block_index (
    hash            BLOB NOT NULL UNIQUE,
    prev_hash       BLOB NOT NULL,
    height          INTEGER NOT NULL,
    n_file          INTEGER NOT NULL DEFAULT 0,
    data_pos        INTEGER NOT NULL DEFAULT 0,
    undo_pos        INTEGER NOT NULL DEFAULT 0,
    status          INTEGER NOT NULL DEFAULT 0,
    n_tx            INTEGER NOT NULL DEFAULT 0,
    n_size          INTEGER NOT NULL DEFAULT 0,
    n_bits          INTEGER NOT NULL DEFAULT 0,
    n_time          INTEGER NOT NULL DEFAULT 0,
    n_reserved      INTEGER NOT NULL DEFAULT 0,
    n_nonce         INTEGER NOT NULL DEFAULT 0,
    n_header_ver    INTEGER NOT NULL DEFAULT 0,
    n_height        INTEGER NOT NULL DEFAULT 0,
    hash_epoch      BLOB,
    hash_merkle     BLOB,
    hash_extmeta    BLOB,
    PRIMARY KEY (hash)
) WITHOUT ROWID;
)";

const char *BLOCK_FILE_INFO_TABLE = R"(
CREATE TABLE IF NOT EXISTS block_file_info (
    file_num    INTEGER PRIMARY KEY,
    data        BLOB NOT NULL
);
)";

const char *TRANSACTIONS_TABLE = R"(
CREATE TABLE IF NOT EXISTS transactions (
    txid            BLOB NOT NULL,
    block_height    INTEGER NOT NULL,
    block_pos       INTEGER NOT NULL,
    PRIMARY KEY (txid)
) WITHOUT ROWID;
)";

const char *TX_INPUTS_TABLE = R"(
CREATE TABLE IF NOT EXISTS tx_inputs (
    txid            BLOB NOT NULL,
    vin             INTEGER NOT NULL,
    prev_txid       BLOB,
    prev_vout       INTEGER,
    value_sats      INTEGER NOT NULL DEFAULT 0,
    address         TEXT,
    PRIMARY KEY (txid, vin)
) WITHOUT ROWID;
)";

const char *TX_OUTPUTS_TABLE = R"(
CREATE TABLE IF NOT EXISTS tx_outputs (
    txid            BLOB NOT NULL,
    vout            INTEGER NOT NULL,
    value_sats      INTEGER NOT NULL,
    script          BLOB NOT NULL DEFAULT X'',
    script_type     TEXT,
    address         TEXT,
    spent           INTEGER NOT NULL DEFAULT 0,
    spent_txid      BLOB,
    spent_vin       INTEGER,
    PRIMARY KEY (txid, vout)
) WITHOUT ROWID;
)";

const char *ADDRESS_BALANCES_TABLE = R"(
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
)";

const char *ADDRESS_HISTORY_TABLE = R"(
CREATE TABLE IF NOT EXISTS address_history (
    address         TEXT NOT NULL,
    block_height    INTEGER NOT NULL,
    txid            BLOB NOT NULL,
    net_value       INTEGER NOT NULL DEFAULT 0,
    PRIMARY KEY (address, block_height, txid)
) WITHOUT ROWID;
)";

const char *META_TABLE = R"(
CREATE TABLE IF NOT EXISTS meta (
    key     TEXT PRIMARY KEY,
    value   BLOB NOT NULL
);
)";

const char *ALL_INDEXES = R"(
CREATE INDEX IF NOT EXISTS idx_block_height ON block_index(n_height);
CREATE INDEX IF NOT EXISTS idx_utxo_height ON utxos(height);
CREATE INDEX IF NOT EXISTS idx_tx_block ON transactions(block_height);
CREATE INDEX IF NOT EXISTS idx_txout_addr_unspent ON tx_outputs(address)
    WHERE spent = 0 AND address IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_txout_address ON tx_outputs(address, value_sats DESC)
    WHERE address IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_txin_prevout ON tx_inputs(prev_txid, prev_vout)
    WHERE prev_txid IS NOT NULL;
CREATE INDEX IF NOT EXISTS idx_addr_bal_rank ON address_balances(balance_sats DESC);
CREATE INDEX IF NOT EXISTS idx_addr_hist ON address_history(address, block_height DESC);
CREATE INDEX IF NOT EXISTS idx_addr_hist_txid ON address_history(txid);
)";

static void ExecOrThrow(sqlite3 *db, const char *sql) {
    char *errmsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string err = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error("Schema exec failed: " + err);
    }
}

void CreateAllTables(sqlite3 *db) {
    ExecOrThrow(db, UTXO_TABLE);
    ExecOrThrow(db, BLOCK_INDEX_TABLE);
    ExecOrThrow(db, BLOCK_FILE_INFO_TABLE);
    ExecOrThrow(db, TRANSACTIONS_TABLE);
    ExecOrThrow(db, TX_INPUTS_TABLE);
    ExecOrThrow(db, TX_OUTPUTS_TABLE);
    ExecOrThrow(db, ADDRESS_BALANCES_TABLE);
    ExecOrThrow(db, ADDRESS_HISTORY_TABLE);
    ExecOrThrow(db, META_TABLE);
}

void CreateAllIndexes(sqlite3 *db) {
    ExecOrThrow(db, ALL_INDEXES);
}

} // namespace sqlite_schema
