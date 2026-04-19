// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/tx_handler.h>

#include <chainparams.h>
#include <config.h>
#include <core_io.h>
#include <node/context.h>
#include <primitives/transaction.h>
#include <rpc/blockchain.h>
#include <rpc/protocol.h>
#include <sqlite/block_tree_sqlite.h>
#include <sqlite3.h>
#include <streams.h>
#include <sync.h>
#include <txmempool.h>
#include <util/ref.h>
#include <util/strencodings.h>
#include <validation.h>
#include <consensus/validation.h>

namespace api {

static UniValue TxOutputToJSON(sqlite3_stmt *stmt) {
    UniValue out(UniValue::VOBJ);
    out.pushKV("vout", sqlite3_column_int(stmt, 0));
    out.pushKV("value_sats", int64_t(sqlite3_column_int64(stmt, 1)));

    const char *stype = reinterpret_cast<const char *>(
        sqlite3_column_text(stmt, 2));
    if (stype) {
        out.pushKV("script_type", std::string(stype));
    }

    const char *addr = reinterpret_cast<const char *>(
        sqlite3_column_text(stmt, 3));
    if (addr) {
        out.pushKV("address", std::string(addr));
    }

    out.pushKV("spent", sqlite3_column_int(stmt, 4) != 0);

    const uint8_t *stxid = static_cast<const uint8_t *>(
        sqlite3_column_blob(stmt, 5));
    if (stxid && sqlite3_column_bytes(stmt, 5) == 32) {
        uint256 hash;
        memcpy(hash.begin(), stxid, 32);
        out.pushKV("spent_txid", hash.GetHex());
    }

    return out;
}

static UniValue TxInputToJSON(sqlite3_stmt *stmt) {
    UniValue in(UniValue::VOBJ);
    in.pushKV("vin", sqlite3_column_int(stmt, 0));

    const uint8_t *ptxid = static_cast<const uint8_t *>(
        sqlite3_column_blob(stmt, 1));
    if (ptxid && sqlite3_column_bytes(stmt, 1) == 32) {
        uint256 hash;
        memcpy(hash.begin(), ptxid, 32);
        in.pushKV("prev_txid", hash.GetHex());
    }

    in.pushKV("prev_vout", sqlite3_column_int(stmt, 2));
    in.pushKV("value_sats", int64_t(sqlite3_column_int64(stmt, 3)));

    const char *addr = reinterpret_cast<const char *>(
        sqlite3_column_text(stmt, 4));
    if (addr) {
        in.pushKV("address", std::string(addr));
    }

    return in;
}

bool HandleGetTx(const util::Ref &ctx, HTTPRequest *req,
                 const std::vector<std::string> &parts,
                 const QueryParams &qp) {
    // GET /api/v1/txs/<txid>
    // GET /api/v1/txs/<txid>/inputs
    // GET /api/v1/txs/<txid>/outputs
    if (parts.size() < 2) {
        WriteError(req, HTTP_BAD_REQUEST, "missing_txid",
                   "Usage: /api/v1/txs/<txid>");
        return true;
    }

    uint256 txid;
    if (!ParseHashFromHex(parts[1], txid)) {
        WriteError(req, HTTP_BAD_REQUEST, "invalid_txid",
                   "Invalid transaction ID");
        return true;
    }

    auto *btree = dynamic_cast<CBlockTreeSqlite *>(pblocktree.get());
    if (!btree) {
        WriteError(req, HTTP_INTERNAL_SERVER_ERROR, "db_error",
                   "SQLite not available");
        return true;
    }
    CSqliteWrapper &db = btree->GetDb();

    // Look up the transaction
    sqlite3_stmt *stmt = db.Prepare(
        "SELECT block_height, block_pos FROM transactions WHERE txid = ?1");
    sqlite3_bind_blob(stmt, 1, txid.begin(), 32, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_reset(stmt);
        WriteError(req, HTTP_NOT_FOUND, "tx_not_found",
                   "Transaction not found");
        return true;
    }

    int block_height = sqlite3_column_int(stmt, 0);
    int block_pos = sqlite3_column_int(stmt, 1);
    sqlite3_reset(stmt);

    std::string subResource =
        (parts.size() >= 3) ? parts[2] : std::string();

    if (subResource == "inputs") {
        sqlite3_stmt *q = db.Prepare(
            "SELECT vin, prev_txid, prev_vout, value_sats, address "
            "FROM tx_inputs WHERE txid = ?1 ORDER BY vin");
        sqlite3_bind_blob(q, 1, txid.begin(), 32, SQLITE_STATIC);

        UniValue arr(UniValue::VARR);
        while (sqlite3_step(q) == SQLITE_ROW) {
            arr.push_back(TxInputToJSON(q));
        }
        sqlite3_reset(q);

        WriteSuccess(req, arr);
        return true;
    }

    if (subResource == "outputs") {
        sqlite3_stmt *q = db.Prepare(
            "SELECT vout, value_sats, script_type, address, spent, spent_txid "
            "FROM tx_outputs WHERE txid = ?1 ORDER BY vout");
        sqlite3_bind_blob(q, 1, txid.begin(), 32, SQLITE_STATIC);

        UniValue arr(UniValue::VARR);
        while (sqlite3_step(q) == SQLITE_ROW) {
            arr.push_back(TxOutputToJSON(q));
        }
        sqlite3_reset(q);

        WriteSuccess(req, arr);
        return true;
    }

    // Full tx summary
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txid.GetHex());
    result.pushKV("block_height", block_height);
    result.pushKV("block_pos", block_pos);

    {
        LOCK(cs_main);
        const CBlockIndex *tip = ::ChainActive().Tip();
        if (tip) {
            result.pushKV("confirmations",
                           tip->nHeight - block_height + 1);
        }
    }

    // Count inputs and outputs
    sqlite3_stmt *qin = db.Prepare(
        "SELECT COUNT(*), COALESCE(SUM(value_sats),0) "
        "FROM tx_inputs WHERE txid = ?1");
    sqlite3_bind_blob(qin, 1, txid.begin(), 32, SQLITE_STATIC);
    if (sqlite3_step(qin) == SQLITE_ROW) {
        result.pushKV("input_count", sqlite3_column_int(qin, 0));
        result.pushKV("input_value_sats", int64_t(sqlite3_column_int64(qin, 1)));
    }
    sqlite3_reset(qin);

    sqlite3_stmt *qout = db.Prepare(
        "SELECT COUNT(*), COALESCE(SUM(value_sats),0) "
        "FROM tx_outputs WHERE txid = ?1");
    sqlite3_bind_blob(qout, 1, txid.begin(), 32, SQLITE_STATIC);
    if (sqlite3_step(qout) == SQLITE_ROW) {
        result.pushKV("output_count", sqlite3_column_int(qout, 0));
        result.pushKV("output_value_sats", int64_t(sqlite3_column_int64(qout, 1)));
    }
    sqlite3_reset(qout);

    const UniValue &inValUV = result["input_value_sats"];
    const UniValue &outValUV = result["output_value_sats"];
    if (inValUV.isNum() && outValUV.isNum()) {
        int64_t inVal = inValUV.get_int64();
        int64_t outVal = outValUV.get_int64();
        if (inVal > 0) {
            result.pushKV("fee_sats", inVal - outVal);
        }
    }

    WriteSuccess(req, result);
    return true;
}

bool HandleGetMempool(const util::Ref &ctx, HTTPRequest *req,
                      const std::vector<std::string> &parts,
                      const QueryParams &qp) {
    NodeContext *node =
        ctx.Has<NodeContext>() ? &ctx.Get<NodeContext>() : nullptr;
    if (!node || !node->mempool) {
        WriteError(req, HTTP_NOT_FOUND, "no_mempool",
                   "Mempool not available");
        return true;
    }

    CTxMemPool &mempool = *node->mempool;
    int limit = qp.GetInt("limit", 50);
    limit = std::max(1, std::min(limit, 500));

    UniValue result(UniValue::VOBJ);
    {
        LOCK(mempool.cs);
        result.pushKV("size", int64_t(mempool.size()));
        result.pushKV("bytes", int64_t(mempool.GetTotalTxSize()));

        UniValue txArr(UniValue::VARR);
        int count = 0;
        for (auto it = mempool.mapTx.begin();
             it != mempool.mapTx.end() && count < limit; ++it, ++count) {
            UniValue tx(UniValue::VOBJ);
            tx.pushKV("txid", it->GetTx().GetId().GetHex());
            tx.pushKV("size", int64_t(it->GetTxSize()));
            tx.pushKV("fee_sats",
                       int64_t(it->GetFee() / SATOSHI));
            tx.pushKV("time", int64_t(it->GetTime().count()));
            txArr.push_back(tx);
        }
        result.pushKV("transactions", txArr);
    }

    WriteSuccess(req, result);
    return true;
}

bool HandleSendTx(const util::Ref &ctx, HTTPRequest *req,
                  const std::vector<std::string> &parts,
                  const QueryParams &qp) {
    std::string body = req->ReadBody();
    if (body.empty()) {
        WriteError(req, HTTP_BAD_REQUEST, "empty_body",
                   "Request body must contain raw transaction hex");
        return true;
    }

    UniValue parsed;
    if (parsed.read(body) && parsed.isObject()) {
        const UniValue &hexVal = find_value(parsed, "hex");
        if (hexVal.isStr()) {
            body = hexVal.get_str();
        }
    }

    if (!IsHex(body)) {
        WriteError(req, HTTP_BAD_REQUEST, "invalid_hex",
                   "Transaction must be hex-encoded");
        return true;
    }

    CMutableTransaction mtx;
    try {
        CDataStream ssData(ParseHex(body), SER_NETWORK, PROTOCOL_VERSION);
        ssData >> mtx;
    } catch (const std::exception &e) {
        WriteError(req, HTTP_BAD_REQUEST, "decode_failed",
                   std::string("Failed to decode transaction: ") + e.what());
        return true;
    }

    CTransactionRef tx = MakeTransactionRef(std::move(mtx));
    const TxId &txid = tx->GetId();

    NodeContext *node =
        ctx.Has<NodeContext>() ? &ctx.Get<NodeContext>() : nullptr;
    if (!node || !node->mempool) {
        WriteError(req, HTTP_INTERNAL_SERVER_ERROR, "no_mempool",
                   "Mempool not available");
        return true;
    }

    TxValidationState state;
    bool accepted = AcceptToMemoryPool(
        GetConfig(), *node->mempool, state, std::move(tx),
        false /* bypass_limits */);

    if (!accepted) {
        WriteError(req, HTTP_BAD_REQUEST, "rejected",
                   state.GetRejectReason());
        return true;
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txid.GetHex());
    result.pushKV("accepted", true);
    WriteJSON(req, 201, result);
    return true;
}

bool HandleDecodeTx(const util::Ref &ctx, HTTPRequest *req,
                    const std::vector<std::string> &parts,
                    const QueryParams &qp) {
    std::string body = req->ReadBody();
    if (body.empty()) {
        WriteError(req, HTTP_BAD_REQUEST, "empty_body",
                   "Request body must contain raw transaction hex");
        return true;
    }

    UniValue parsed;
    if (parsed.read(body) && parsed.isObject()) {
        const UniValue &hexVal = find_value(parsed, "hex");
        if (hexVal.isStr()) {
            body = hexVal.get_str();
        }
    }

    if (!IsHex(body)) {
        WriteError(req, HTTP_BAD_REQUEST, "invalid_hex",
                   "Transaction must be hex-encoded");
        return true;
    }

    CMutableTransaction mtx;
    try {
        CDataStream ssData(ParseHex(body), SER_NETWORK, PROTOCOL_VERSION);
        ssData >> mtx;
    } catch (const std::exception &e) {
        WriteError(req, HTTP_BAD_REQUEST, "decode_failed",
                   std::string("Failed to decode: ") + e.what());
        return true;
    }

    CTransaction tx(std::move(mtx));
    UniValue result(UniValue::VOBJ);
    TxToUniv(tx, BlockHash(), result);
    WriteSuccess(req, result);
    return true;
}

bool HandleGetMempoolHistory(const util::Ref &, HTTPRequest *req,
                              const std::vector<std::string> &,
                              const QueryParams &qp) {
    auto periodOpt = qp.Get("period");
    std::string period = periodOpt.value_or("day");

    int points;
    if (period == "week") points = 7 * 24;
    else if (period == "month") points = 31 * 24;
    else if (period == "quarter") points = 90 * 24;
    else if (period == "year") points = 365;
    else { period = "day"; points = 24 * 12; }

    auto *btree = dynamic_cast<CBlockTreeSqlite *>(pblocktree.get());
    if (!btree) {
        WriteError(req, HTTP_INTERNAL_SERVER_ERROR, "db_error",
                   "SQLite not available");
        return true;
    }
    CSqliteWrapper &db = btree->GetDb();

    sqlite3_stmt *stmt = db.Prepare(
        "SELECT snapshot_ts, tx_count, total_bytes "
        "FROM mempool_snapshots "
        "ORDER BY snapshot_ts DESC LIMIT ?1");
    sqlite3_bind_int(stmt, 1, points);

    UniValue series(UniValue::VARR);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        UniValue point(UniValue::VOBJ);
        point.pushKV("ts", int64_t(sqlite3_column_int64(stmt, 0)));
        point.pushKV("tx_count", sqlite3_column_int(stmt, 1));
        point.pushKV("total_bytes", int64_t(sqlite3_column_int64(stmt, 2)));
        series.push_back(point);
    }
    sqlite3_reset(stmt);

    UniValue reversed(UniValue::VARR);
    for (int i = (int)series.size() - 1; i >= 0; i--) {
        reversed.push_back(series[i]);
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("period", period);
    result.pushKV("series", reversed);
    WriteSuccess(req, result);
    return true;
}

} // namespace api
