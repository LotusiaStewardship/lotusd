// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/address_handler.h>

#include <rpc/protocol.h>
#include <sqlite/block_tree_sqlite.h>
#include <sqlite3.h>
#include <sync.h>
#include <validation.h>

namespace api {

bool HandleGetAddress(const util::Ref &ctx, HTTPRequest *req,
                      const std::vector<std::string> &parts,
                      const QueryParams &qp) {
    // GET /api/v1/addresses/<address>
    // GET /api/v1/addresses/<address>/txs
    // GET /api/v1/addresses/<address>/utxos
    // GET /api/v1/addresses?rank=true&limit=N  (rich list)
    if (parts.size() < 2) {
        // Rich list
        auto *btree = dynamic_cast<CBlockTreeSqlite *>(pblocktree.get());
        if (!btree) {
            WriteError(req, HTTP_INTERNAL_SERVER_ERROR, "db_error",
                       "SQLite not available");
            return true;
        }
        CSqliteWrapper &db = btree->GetDb();

        int limit = qp.GetInt("limit", 20);
        int offset = qp.GetInt("offset", 0);
        limit = std::max(1, std::min(limit, 100));
        offset = std::max(0, offset);

        sqlite3_stmt *cnt = db.Prepare(
            "SELECT COUNT(*) FROM address_balances WHERE balance_sats > 0");
        int total = 0;
        if (sqlite3_step(cnt) == SQLITE_ROW) {
            total = sqlite3_column_int(cnt, 0);
        }
        sqlite3_reset(cnt);

        sqlite3_stmt *stmt = db.Prepare(
            "SELECT address, balance_sats, received_sats, sent_sats, "
            "tx_count, utxo_count "
            "FROM address_balances WHERE balance_sats > 0 "
            "ORDER BY balance_sats DESC LIMIT ?1 OFFSET ?2");
        sqlite3_bind_int(stmt, 1, limit);
        sqlite3_bind_int(stmt, 2, offset);

        UniValue arr(UniValue::VARR);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            UniValue obj(UniValue::VOBJ);
            const char *addr = reinterpret_cast<const char *>(
                sqlite3_column_text(stmt, 0));
            obj.pushKV("address", addr ? std::string(addr) : "");
            obj.pushKV("balance_sats", int64_t(sqlite3_column_int64(stmt, 1)));
            obj.pushKV("received_sats", int64_t(sqlite3_column_int64(stmt, 2)));
            obj.pushKV("sent_sats", int64_t(sqlite3_column_int64(stmt, 3)));
            obj.pushKV("tx_count", sqlite3_column_int(stmt, 4));
            obj.pushKV("utxo_count", sqlite3_column_int(stmt, 5));
            arr.push_back(obj);
        }
        sqlite3_reset(stmt);

        WriteSuccess(req, PaginatedResponse(arr, total, limit, offset));
        return true;
    }

    const std::string &address = parts[1];

    auto *btree = dynamic_cast<CBlockTreeSqlite *>(pblocktree.get());
    if (!btree) {
        WriteError(req, HTTP_INTERNAL_SERVER_ERROR, "db_error",
                   "SQLite not available");
        return true;
    }
    CSqliteWrapper &db = btree->GetDb();

    std::string subResource =
        (parts.size() >= 3) ? parts[2] : std::string();

    if (subResource == "txs") {
        int limit = qp.GetInt("limit", 25);
        int offset = qp.GetInt("offset", 0);
        limit = std::max(1, std::min(limit, 200));
        offset = std::max(0, offset);

        sqlite3_stmt *cnt = db.Prepare(
            "SELECT COUNT(*) FROM address_history WHERE address = ?1");
        sqlite3_bind_text(cnt, 1, address.c_str(), -1, SQLITE_TRANSIENT);
        int total = 0;
        if (sqlite3_step(cnt) == SQLITE_ROW) {
            total = sqlite3_column_int(cnt, 0);
        }
        sqlite3_reset(cnt);

        sqlite3_stmt *stmt = db.Prepare(
            "SELECT block_height, txid, net_value FROM address_history "
            "WHERE address = ?1 ORDER BY block_height DESC "
            "LIMIT ?2 OFFSET ?3");
        sqlite3_bind_text(stmt, 1, address.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);
        sqlite3_bind_int(stmt, 3, offset);

        UniValue arr(UniValue::VARR);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            UniValue obj(UniValue::VOBJ);
            obj.pushKV("block_height", sqlite3_column_int(stmt, 0));

            const uint8_t *txid_blob = static_cast<const uint8_t *>(
                sqlite3_column_blob(stmt, 1));
            if (txid_blob && sqlite3_column_bytes(stmt, 1) == 32) {
                uint256 txid;
                memcpy(txid.begin(), txid_blob, 32);
                obj.pushKV("txid", txid.GetHex());
            }

            obj.pushKV("net_value", int64_t(sqlite3_column_int64(stmt, 2)));
            arr.push_back(obj);
        }
        sqlite3_reset(stmt);

        WriteSuccess(req, PaginatedResponse(arr, total, limit, offset));
        return true;
    }

    if (subResource == "utxos") {
        int limit = qp.GetInt("limit", 50);
        int offset = qp.GetInt("offset", 0);
        limit = std::max(1, std::min(limit, 500));
        offset = std::max(0, offset);

        sqlite3_stmt *cnt = db.Prepare(
            "SELECT COUNT(*) FROM tx_outputs "
            "WHERE address = ?1 AND spent = 0");
        sqlite3_bind_text(cnt, 1, address.c_str(), -1, SQLITE_TRANSIENT);
        int total = 0;
        if (sqlite3_step(cnt) == SQLITE_ROW) {
            total = sqlite3_column_int(cnt, 0);
        }
        sqlite3_reset(cnt);

        sqlite3_stmt *stmt = db.Prepare(
            "SELECT txid, vout, value_sats FROM tx_outputs "
            "WHERE address = ?1 AND spent = 0 "
            "ORDER BY value_sats DESC LIMIT ?2 OFFSET ?3");
        sqlite3_bind_text(stmt, 1, address.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);
        sqlite3_bind_int(stmt, 3, offset);

        UniValue arr(UniValue::VARR);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            UniValue obj(UniValue::VOBJ);
            const uint8_t *txid_blob = static_cast<const uint8_t *>(
                sqlite3_column_blob(stmt, 0));
            if (txid_blob && sqlite3_column_bytes(stmt, 0) == 32) {
                uint256 txid;
                memcpy(txid.begin(), txid_blob, 32);
                obj.pushKV("txid", txid.GetHex());
            }
            obj.pushKV("vout", sqlite3_column_int(stmt, 1));
            obj.pushKV("value_sats", int64_t(sqlite3_column_int64(stmt, 2)));
            arr.push_back(obj);
        }
        sqlite3_reset(stmt);

        WriteSuccess(req, PaginatedResponse(arr, total, limit, offset));
        return true;
    }

    // Address summary
    sqlite3_stmt *stmt = db.Prepare(
        "SELECT balance_sats, received_sats, sent_sats, tx_count, "
        "utxo_count, first_height, last_height "
        "FROM address_balances WHERE address = ?1");
    sqlite3_bind_text(stmt, 1, address.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_reset(stmt);
        WriteError(req, HTTP_NOT_FOUND, "address_not_found",
                   "Address has no recorded activity");
        return true;
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", address);
    result.pushKV("balance_sats", int64_t(sqlite3_column_int64(stmt, 0)));
    result.pushKV("received_sats", int64_t(sqlite3_column_int64(stmt, 1)));
    result.pushKV("sent_sats", int64_t(sqlite3_column_int64(stmt, 2)));
    result.pushKV("tx_count", sqlite3_column_int(stmt, 3));
    result.pushKV("utxo_count", sqlite3_column_int(stmt, 4));
    result.pushKV("first_height", sqlite3_column_int(stmt, 5));
    result.pushKV("last_height", sqlite3_column_int(stmt, 6));
    sqlite3_reset(stmt);

    WriteSuccess(req, result);
    return true;
}

bool HandleGetUtxos(const util::Ref &ctx, HTTPRequest *req,
                    const std::vector<std::string> &parts,
                    const QueryParams &qp) {
    // GET /api/v1/utxos/<txid>/<vout>
    if (parts.size() < 3) {
        WriteError(req, HTTP_BAD_REQUEST, "missing_params",
                   "Usage: /api/v1/utxos/<txid>/<vout>");
        return true;
    }

    uint256 txid;
    if (!ParseHashFromHex(parts[1], txid)) {
        WriteError(req, HTTP_BAD_REQUEST, "invalid_txid",
                   "Invalid transaction ID");
        return true;
    }

    int vout;
    try {
        vout = std::stoi(parts[2]);
    } catch (...) {
        WriteError(req, HTTP_BAD_REQUEST, "invalid_vout",
                   "Invalid output index");
        return true;
    }

    auto *btree = dynamic_cast<CBlockTreeSqlite *>(pblocktree.get());
    if (!btree) {
        WriteError(req, HTTP_INTERNAL_SERVER_ERROR, "db_error",
                   "SQLite not available");
        return true;
    }
    CSqliteWrapper &db = btree->GetDb();

    sqlite3_stmt *stmt = db.Prepare(
        "SELECT value_sats, script_type, address, spent, spent_txid "
        "FROM tx_outputs WHERE txid = ?1 AND vout = ?2");
    sqlite3_bind_blob(stmt, 1, txid.begin(), 32, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, vout);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_reset(stmt);
        WriteError(req, HTTP_NOT_FOUND, "utxo_not_found",
                   "Output not found");
        return true;
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txid.GetHex());
    result.pushKV("vout", vout);
    result.pushKV("value_sats", int64_t(sqlite3_column_int64(stmt, 0)));

    const char *stype = reinterpret_cast<const char *>(
        sqlite3_column_text(stmt, 1));
    if (stype) {
        result.pushKV("script_type", std::string(stype));
    }

    const char *addr = reinterpret_cast<const char *>(
        sqlite3_column_text(stmt, 2));
    if (addr) {
        result.pushKV("address", std::string(addr));
    }

    result.pushKV("spent", sqlite3_column_int(stmt, 3) != 0);

    const uint8_t *stxid = static_cast<const uint8_t *>(
        sqlite3_column_blob(stmt, 4));
    if (stxid && sqlite3_column_bytes(stmt, 4) == 32) {
        uint256 hash;
        memcpy(hash.begin(), stxid, 32);
        result.pushKV("spent_txid", hash.GetHex());
    }
    sqlite3_reset(stmt);

    WriteSuccess(req, result);
    return true;
}

} // namespace api
