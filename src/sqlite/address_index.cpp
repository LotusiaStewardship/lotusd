// Copyright (c) 2024 The Logos Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sqlite/address_index.h>

#include <sqlite3.h>

CAddressIndex::CAddressIndex(CSqliteWrapper &db) : m_db(db) {}

void CAddressIndex::UpdateForTx(const std::string &address, int height,
                                const uint8_t *txid_data, int64_t received,
                                int64_t sent, int utxo_delta) {
    int64_t delta = received - sent;

    // Upsert address_balances — O(1) amortized via ON CONFLICT
    sqlite3_stmt *stmt = m_db.Prepare(
        "INSERT INTO address_balances("
        "address, balance_sats, received_sats, sent_sats, "
        "tx_count, utxo_count, first_height, last_height"
        ") VALUES(?1, ?2, ?3, ?4, 1, ?5, ?6, ?6) "
        "ON CONFLICT(address) DO UPDATE SET "
        "balance_sats = balance_sats + ?2, "
        "received_sats = received_sats + ?3, "
        "sent_sats = sent_sats + ?4, "
        "tx_count = tx_count + 1, "
        "utxo_count = utxo_count + ?5, "
        "last_height = MAX(last_height, ?6)");

    sqlite3_bind_text(stmt, 1, address.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, delta);
    sqlite3_bind_int64(stmt, 3, received);
    sqlite3_bind_int64(stmt, 4, sent);
    sqlite3_bind_int(stmt, 5, utxo_delta);
    sqlite3_bind_int(stmt, 6, height);
    sqlite3_step(stmt);
    sqlite3_reset(stmt);

    // Insert history entry
    sqlite3_stmt *hist = m_db.Prepare(
        "INSERT OR REPLACE INTO address_history("
        "address, block_height, txid, net_value"
        ") VALUES(?1, ?2, ?3, ?4)");

    sqlite3_bind_text(hist, 1, address.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(hist, 2, height);
    sqlite3_bind_blob(hist, 3, txid_data, 32, SQLITE_STATIC);
    sqlite3_bind_int64(hist, 4, delta);
    sqlite3_step(hist);
    sqlite3_reset(hist);
}

int64_t CAddressIndex::GetBalance(const std::string &address) const {
    sqlite3_stmt *stmt = const_cast<CSqliteWrapper &>(m_db).Prepare(
        "SELECT balance_sats FROM address_balances WHERE address = ?1");
    sqlite3_bind_text(stmt, 1, address.c_str(), -1, SQLITE_TRANSIENT);

    int64_t bal = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        bal = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_reset(stmt);
    return bal;
}

int64_t CAddressIndex::GetReceived(const std::string &address) const {
    sqlite3_stmt *stmt = const_cast<CSqliteWrapper &>(m_db).Prepare(
        "SELECT received_sats FROM address_balances WHERE address = ?1");
    sqlite3_bind_text(stmt, 1, address.c_str(), -1, SQLITE_TRANSIENT);

    int64_t val = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        val = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_reset(stmt);
    return val;
}

int CAddressIndex::GetTxCount(const std::string &address) const {
    sqlite3_stmt *stmt = const_cast<CSqliteWrapper &>(m_db).Prepare(
        "SELECT tx_count FROM address_balances WHERE address = ?1");
    sqlite3_bind_text(stmt, 1, address.c_str(), -1, SQLITE_TRANSIENT);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_reset(stmt);
    return count;
}

std::vector<AddressHistoryEntry>
CAddressIndex::GetHistory(const std::string &address, int page,
                          int pageSize) const {
    sqlite3_stmt *stmt = const_cast<CSqliteWrapper &>(m_db).Prepare(
        "SELECT address, block_height, txid, net_value "
        "FROM address_history "
        "WHERE address = ?1 "
        "ORDER BY block_height DESC "
        "LIMIT ?2 OFFSET ?3");

    sqlite3_bind_text(stmt, 1, address.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, pageSize);
    sqlite3_bind_int(stmt, 3, (page - 1) * pageSize);

    std::vector<AddressHistoryEntry> result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AddressHistoryEntry entry;
        entry.address = reinterpret_cast<const char *>(
            sqlite3_column_text(stmt, 0));
        entry.block_height = sqlite3_column_int(stmt, 1);

        const void *txid = sqlite3_column_blob(stmt, 2);
        int txidLen = sqlite3_column_bytes(stmt, 2);
        if (txid && txidLen > 0) {
            entry.txid.assign(static_cast<const uint8_t *>(txid),
                              static_cast<const uint8_t *>(txid) + txidLen);
        }
        entry.net_value = sqlite3_column_int64(stmt, 3);
        result.push_back(std::move(entry));
    }

    sqlite3_reset(stmt);
    return result;
}
