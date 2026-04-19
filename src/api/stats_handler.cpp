// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/stats_handler.h>

#include <chain.h>
#include <chainparams.h>
#include <logging.h>
#include <node/context.h>
#include <pow/pow.h>
#include <rpc/blockchain.h>
#include <rpc/protocol.h>
#include <sqlite/block_tree_sqlite.h>
#include <sqlite3.h>
#include <sync.h>
#include <txmempool.h>
#include <util/ref.h>
#include <util/time.h>
#include <validation.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace api {

static double EstimateHashrate(int nBlocks) {
    LOCK(cs_main);
    const CBlockIndex *tip = ::ChainActive().Tip();
    if (!tip || tip->nHeight < nBlocks) {
        return 0.0;
    }

    const CBlockIndex *past = tip;
    for (int i = 0; i < nBlocks && past->pprev; i++) {
        past = past->pprev;
    }

    int64_t timeDiff = tip->GetBlockTime() - past->GetBlockTime();
    if (timeDiff <= 0) {
        return 0.0;
    }

    arith_uint256 work = tip->nChainWork - past->nChainWork;
    return (work == arith_uint256()) ? 0.0 : work.getdouble() / double(timeDiff);
}

static bool HandleStatsCards(const util::Ref &ctx, HTTPRequest *req,
                             const QueryParams &) {
    NodeContext *node =
        ctx.Has<NodeContext>() ? &ctx.Get<NodeContext>() : nullptr;

    UniValue result(UniValue::VOBJ);
    {
        LOCK(cs_main);
        const CBlockIndex *tip = ::ChainActive().Tip();
        if (!tip) {
            WriteError(req, HTTP_SERVICE_UNAVAILABLE, "not_ready",
                       "Chain not yet loaded");
            return true;
        }
        result.pushKV("tip_height", tip->nHeight);
        result.pushKV("difficulty", GetDifficulty(tip));
    }

    result.pushKV("hashrate", EstimateHashrate(120));

    if (node && node->mempool) {
        LOCK(node->mempool->cs);
        result.pushKV("mempool_count", int64_t(node->mempool->size()));
        result.pushKV("mempool_bytes",
                       int64_t(node->mempool->GetTotalTxSize()));
    } else {
        result.pushKV("mempool_count", int64_t(0));
        result.pushKV("mempool_bytes", int64_t(0));
    }

    auto *btree = dynamic_cast<CBlockTreeSqlite *>(pblocktree.get());
    if (btree) {
        CSqliteWrapper &db = btree->GetDb();

        // Total supply from address balances (sum of all balances)
        sqlite3_stmt *qsup = db.Prepare(
            "SELECT COALESCE(SUM(balance_sats), 0) FROM address_balances");
        int64_t totalSupply = 0;
        if (sqlite3_step(qsup) == SQLITE_ROW) {
            totalSupply = sqlite3_column_int64(qsup, 0);
        }
        sqlite3_reset(qsup);
        result.pushKV("total_supply_sats", totalSupply);

        // Burned sats: OP_RETURN outputs have script_type IS NULL and
        // address IS NULL (ScriptToType returns empty for NULL_DATA).
        // Also include any output with no spendable address as burned.
        sqlite3_stmt *qburn = db.Prepare(
            "SELECT COALESCE(SUM(value_sats), 0) FROM tx_outputs "
            "WHERE script_type IS NULL AND address IS NULL AND value_sats > 0");
        int64_t burnedSats = 0;
        if (sqlite3_step(qburn) == SQLITE_ROW) {
            burnedSats = sqlite3_column_int64(qburn, 0);
        }
        sqlite3_reset(qburn);
        result.pushKV("burned_sats", burnedSats);
    }

    WriteSuccess(req, result);
    return true;
}

static int PeriodToPoints(const std::string &period) {
    if (period == "week") return 7 * 24;
    if (period == "month") return 31 * 24;
    if (period == "quarter") return 90 * 24;
    if (period == "year") return 365;
    return 24 * 12; // "day" default: every 5 min for 24h
}

static bool HandleStatsCharts(const util::Ref &, HTTPRequest *req,
                              const QueryParams &qp) {
    auto periodOpt = qp.Get("period");
    std::string period = periodOpt.value_or("day");
    int points = PeriodToPoints(period);

    auto *btree = dynamic_cast<CBlockTreeSqlite *>(pblocktree.get());
    if (!btree) {
        WriteError(req, HTTP_INTERNAL_SERVER_ERROR, "db_error",
                   "SQLite not available");
        return true;
    }
    CSqliteWrapper &db = btree->GetDb();

    sqlite3_stmt *stmt = db.Prepare(
        "SELECT snapshot_ts, block_height, hashrate, difficulty, "
        "mempool_count, total_supply, burned_supply "
        "FROM chain_stats_snapshots "
        "ORDER BY snapshot_ts DESC LIMIT ?1");
    sqlite3_bind_int(stmt, 1, points);

    UniValue series(UniValue::VARR);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        UniValue point(UniValue::VOBJ);
        point.pushKV("ts", int64_t(sqlite3_column_int64(stmt, 0)));
        point.pushKV("block_height", sqlite3_column_int(stmt, 1));
        point.pushKV("hashrate", sqlite3_column_double(stmt, 2));
        point.pushKV("difficulty", sqlite3_column_double(stmt, 3));
        point.pushKV("mempool_count", sqlite3_column_int(stmt, 4));
        point.pushKV("total_supply_sats",
                     int64_t(sqlite3_column_int64(stmt, 5)));
        point.pushKV("burned_supply_sats",
                     int64_t(sqlite3_column_int64(stmt, 6)));
        series.push_back(point);
    }
    sqlite3_reset(stmt);

    // Reverse so oldest is first
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

bool HandleGetStats(const util::Ref &ctx, HTTPRequest *req,
                    const std::vector<std::string> &parts,
                    const QueryParams &qp) {
    // GET /api/v1/stats/cards
    // GET /api/v1/stats/charts?period=day|week|month|quarter|year
    if (parts.size() >= 2) {
        if (parts[1] == "cards") {
            return HandleStatsCards(ctx, req, qp);
        }
        if (parts[1] == "charts") {
            return HandleStatsCharts(ctx, req, qp);
        }
    }

    WriteError(req, HTTP_NOT_FOUND, "not_found",
               "Usage: /api/v1/stats/cards or /api/v1/stats/charts");
    return true;
}

// --- Stats collector: periodic snapshot writer with startup backfill ---

static constexpr int SNAPSHOT_INTERVAL_SECS = 300; // 5 minutes
static constexpr int MAX_SNAPSHOTS = 365 * 24 * 12; // ~1 year of 5-min intervals

static std::thread g_collector_thread;
static std::mutex g_collector_mutex;
static std::condition_variable g_collector_cv;
static std::atomic<bool> g_collector_running{false};

static double DifficultyFromBits(uint32_t nBits) {
    int nShift = (nBits >> 24) & 0xff;
    double dDiff = double(0x0000ffff) / double(nBits & 0x00ffffff);
    while (nShift < 29) { dDiff *= 256.0; nShift++; }
    while (nShift > 29) { dDiff /= 256.0; nShift--; }
    return dDiff;
}

static void WriteOneSnapshot(CSqliteWrapper &db, int64_t ts, int height,
                              double hashrate, double difficulty,
                              int64_t mempoolCount, int64_t mempoolBytes,
                              int64_t totalSupply, int64_t burnedSupply) {
    sqlite3_stmt *ins = db.Prepare(
        "INSERT OR IGNORE INTO chain_stats_snapshots "
        "(snapshot_ts, block_height, hashrate, difficulty, "
        "mempool_count, mempool_bytes, total_supply, burned_supply) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
    sqlite3_bind_int64(ins, 1, ts);
    sqlite3_bind_int(ins, 2, height);
    sqlite3_bind_double(ins, 3, hashrate);
    sqlite3_bind_double(ins, 4, difficulty);
    sqlite3_bind_int64(ins, 5, mempoolCount);
    sqlite3_bind_int64(ins, 6, mempoolBytes);
    sqlite3_bind_int64(ins, 7, totalSupply);
    sqlite3_bind_int64(ins, 8, burnedSupply);
    sqlite3_step(ins);
    sqlite3_reset(ins);
}

// Backfill chain_stats_snapshots from block_index when the table is empty.
// Generates one snapshot per hour from historical block timestamps.
static void BackfillFromBlockIndex(CSqliteWrapper &db) {
    sqlite3_stmt *check =
        db.Prepare("SELECT 1 FROM chain_stats_snapshots LIMIT 1");
    bool hasData = (sqlite3_step(check) == SQLITE_ROW);
    sqlite3_reset(check);
    if (hasData) {
        return;
    }

    LogPrintf("API: chain_stats_snapshots is empty, backfilling from "
              "block_index...\n");

    // Current supply for the latest snapshot point
    int64_t totalSupply = 0, burnedSupply = 0;
    {
        sqlite3_stmt *qs = db.Prepare(
            "SELECT COALESCE(SUM(balance_sats), 0) FROM address_balances");
        if (sqlite3_step(qs) == SQLITE_ROW) {
            totalSupply = sqlite3_column_int64(qs, 0);
        }
        sqlite3_reset(qs);

        sqlite3_stmt *qb = db.Prepare(
            "SELECT COALESCE(SUM(value_sats), 0) FROM tx_outputs "
            "WHERE script_type = 'nulldata' AND value_sats > 0");
        if (sqlite3_step(qb) == SQLITE_ROW) {
            burnedSupply = sqlite3_column_int64(qb, 0);
        }
        sqlite3_reset(qb);
    }

    // Sample one block per hour (every ~30 blocks at 2-min target).
    // Walk through block_index by time, picking one block per 3600s window.
    sqlite3_stmt *blks = db.Prepare(
        "SELECT n_height, n_time, n_bits FROM block_index "
        "WHERE n_height > 0 AND n_time > 0 "
        "ORDER BY n_height ASC");

    db.BeginTransaction();

    int64_t lastSnapshotTs = 0;
    int count = 0;
    while (sqlite3_step(blks) == SQLITE_ROW) {
        int height = sqlite3_column_int(blks, 0);
        int64_t blockTime = sqlite3_column_int64(blks, 1);
        uint32_t nBits = static_cast<uint32_t>(sqlite3_column_int(blks, 2));

        // One snapshot per hour
        if (blockTime - lastSnapshotTs < 3600 && lastSnapshotTs > 0) {
            continue;
        }
        lastSnapshotTs = blockTime;

        double diff = DifficultyFromBits(nBits);
        // Rough hashrate estimate: difficulty * 2^32 / target_spacing
        // For Lotus with 120s target: hashrate ≈ diff * 2^32 / 120
        double hashrate = diff * 4294967296.0 / 120.0;

        // Supply data is only accurate for current state; historical
        // snapshots get 0 (charts will show supply only for recent data).
        WriteOneSnapshot(db, blockTime, height, hashrate, diff,
                         0, 0, 0, 0);
        count++;
    }
    sqlite3_reset(blks);

    // Write one "now" snapshot with accurate supply data
    {
        LOCK(cs_main);
        const CBlockIndex *tip = ::ChainActive().Tip();
        if (tip) {
            double diff = GetDifficulty(tip);
            double hashrate = EstimateHashrate(120);
            WriteOneSnapshot(db, GetTime(), tip->nHeight, hashrate, diff,
                             0, 0, totalSupply, burnedSupply);
            count++;
        }
    }

    db.CommitTransaction();
    LogPrintf("API: backfilled %d chain_stats_snapshots from block_index\n",
              count);
}

// Seed mempool_snapshots with a single entry so the table isn't empty.
static void BackfillMempoolSnapshots(CSqliteWrapper &db,
                                     const util::Ref &ctx) {
    sqlite3_stmt *check =
        db.Prepare("SELECT 1 FROM mempool_snapshots LIMIT 1");
    bool hasData = (sqlite3_step(check) == SQLITE_ROW);
    sqlite3_reset(check);
    if (hasData) {
        return;
    }

    NodeContext *node =
        ctx.Has<NodeContext>() ? &ctx.Get<NodeContext>() : nullptr;
    int64_t mempoolCount = 0, mempoolBytes = 0;
    if (node && node->mempool) {
        LOCK(node->mempool->cs);
        mempoolCount = int64_t(node->mempool->size());
        mempoolBytes = int64_t(node->mempool->GetTotalTxSize());
    }

    sqlite3_stmt *ins = db.Prepare(
        "INSERT OR IGNORE INTO mempool_snapshots "
        "(snapshot_ts, tx_count, total_bytes) VALUES (?1, ?2, ?3)");
    sqlite3_bind_int64(ins, 1, GetTime());
    sqlite3_bind_int64(ins, 2, mempoolCount);
    sqlite3_bind_int64(ins, 3, mempoolBytes);
    sqlite3_step(ins);
    sqlite3_reset(ins);
    LogPrintf("API: seeded initial mempool_snapshot\n");
}

static void TakeCurrentSnapshot(CSqliteWrapper &db, const util::Ref &ctx) {
    int64_t now = GetTime();
    int blockHeight = 0;
    double difficulty = 0.0;
    double hashrate = 0.0;
    {
        LOCK(cs_main);
        const CBlockIndex *tip = ::ChainActive().Tip();
        if (!tip) return;
        blockHeight = tip->nHeight;
        difficulty = GetDifficulty(tip);
    }
    hashrate = EstimateHashrate(120);

    NodeContext *node =
        ctx.Has<NodeContext>() ? &ctx.Get<NodeContext>() : nullptr;
    int64_t mempoolCount = 0, mempoolBytes = 0;
    if (node && node->mempool) {
        LOCK(node->mempool->cs);
        mempoolCount = int64_t(node->mempool->size());
        mempoolBytes = int64_t(node->mempool->GetTotalTxSize());
    }

    int64_t totalSupply = 0, burnedSupply = 0;
    {
        sqlite3_stmt *qsup = db.Prepare(
            "SELECT COALESCE(SUM(balance_sats), 0) FROM address_balances");
        if (sqlite3_step(qsup) == SQLITE_ROW) {
            totalSupply = sqlite3_column_int64(qsup, 0);
        }
        sqlite3_reset(qsup);

        sqlite3_stmt *qburn = db.Prepare(
            "SELECT COALESCE(SUM(value_sats), 0) FROM tx_outputs "
            "WHERE script_type = 'nulldata' AND value_sats > 0");
        if (sqlite3_step(qburn) == SQLITE_ROW) {
            burnedSupply = sqlite3_column_int64(qburn, 0);
        }
        sqlite3_reset(qburn);
    }

    // Chain stats snapshot (REPLACE to overwrite if same second)
    sqlite3_stmt *ins = db.Prepare(
        "INSERT OR REPLACE INTO chain_stats_snapshots "
        "(snapshot_ts, block_height, hashrate, difficulty, "
        "mempool_count, mempool_bytes, total_supply, burned_supply) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)");
    sqlite3_bind_int64(ins, 1, now);
    sqlite3_bind_int(ins, 2, blockHeight);
    sqlite3_bind_double(ins, 3, hashrate);
    sqlite3_bind_double(ins, 4, difficulty);
    sqlite3_bind_int64(ins, 5, mempoolCount);
    sqlite3_bind_int64(ins, 6, mempoolBytes);
    sqlite3_bind_int64(ins, 7, totalSupply);
    sqlite3_bind_int64(ins, 8, burnedSupply);
    sqlite3_step(ins);
    sqlite3_reset(ins);

    // Mempool snapshot
    sqlite3_stmt *mins = db.Prepare(
        "INSERT OR REPLACE INTO mempool_snapshots "
        "(snapshot_ts, tx_count, total_bytes) "
        "VALUES (?1, ?2, ?3)");
    sqlite3_bind_int64(mins, 1, now);
    sqlite3_bind_int64(mins, 2, mempoolCount);
    sqlite3_bind_int64(mins, 3, mempoolBytes);
    sqlite3_step(mins);
    sqlite3_reset(mins);
}

static void PruneOldSnapshots(CSqliteWrapper &db) {
    sqlite3_stmt *del1 = db.Prepare(
        "DELETE FROM chain_stats_snapshots "
        "WHERE snapshot_ts < (SELECT MIN(snapshot_ts) FROM "
        "(SELECT snapshot_ts FROM chain_stats_snapshots "
        "ORDER BY snapshot_ts DESC LIMIT ?1))");
    sqlite3_bind_int(del1, 1, MAX_SNAPSHOTS);
    sqlite3_step(del1);
    sqlite3_reset(del1);

    sqlite3_stmt *del2 = db.Prepare(
        "DELETE FROM mempool_snapshots "
        "WHERE snapshot_ts < (SELECT MIN(snapshot_ts) FROM "
        "(SELECT snapshot_ts FROM mempool_snapshots "
        "ORDER BY snapshot_ts DESC LIMIT ?1))");
    sqlite3_bind_int(del2, 1, MAX_SNAPSHOTS);
    sqlite3_step(del2);
    sqlite3_reset(del2);
}

static void CollectorLoop(const util::Ref &ctx) {
    // Wait until the chain tip is available and block index is loaded.
    // Poll every 2 seconds up to 5 minutes; this avoids competing with
    // startup I/O and the HTTP work queue.
    for (int waited = 0; waited < 300 && g_collector_running.load();
         waited += 2) {
        {
            std::unique_lock<std::mutex> lock(g_collector_mutex);
            g_collector_cv.wait_for(lock, std::chrono::seconds(2),
                [] { return !g_collector_running.load(); });
        }
        LOCK(cs_main);
        const CBlockIndex *tip = ::ChainActive().Tip();
        if (tip && tip->nHeight > 0) {
            break;
        }
    }
    if (!g_collector_running.load()) {
        return;
    }

    // Backfill empty tables on first-start after upgrade
    auto *btree = dynamic_cast<CBlockTreeSqlite *>(pblocktree.get());
    if (btree) {
        CSqliteWrapper &db = btree->GetDb();
        BackfillFromBlockIndex(db);
        BackfillMempoolSnapshots(db, ctx);
    }

    // Regular snapshot loop
    while (g_collector_running.load()) {
        {
            std::unique_lock<std::mutex> lock(g_collector_mutex);
            g_collector_cv.wait_for(lock,
                std::chrono::seconds(SNAPSHOT_INTERVAL_SECS),
                [] { return !g_collector_running.load(); });
        }
        if (!g_collector_running.load()) {
            break;
        }

        btree = dynamic_cast<CBlockTreeSqlite *>(pblocktree.get());
        if (!btree) {
            continue;
        }
        CSqliteWrapper &db = btree->GetDb();

        TakeCurrentSnapshot(db, ctx);
        PruneOldSnapshots(db);
    }
}

void StartStatsCollector(const util::Ref &ctx) {
    g_collector_running.store(true);
    g_collector_thread = std::thread(CollectorLoop, std::cref(ctx));
}

void StopStatsCollector() {
    g_collector_running.store(false);
    g_collector_cv.notify_all();
    if (g_collector_thread.joinable()) {
        g_collector_thread.join();
    }
}

} // namespace api
