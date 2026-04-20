// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sqlite/sqlite_wrapper.h>

#include <logging.h>
#include <sqlite3.h>
#include <util/system.h>

#include <stdexcept>

CSqliteWrapper::CSqliteWrapper(const fs::path &path, bool fMemory, bool fWipe)
    : m_path(path), m_is_memory(fMemory) {

    std::string pathStr = fs::PathToString(path);

    if (fWipe && !fMemory) {
        fs::remove(path);
        fs::remove(fs::PathFromString(pathStr + "-wal"));
        fs::remove(fs::PathFromString(pathStr + "-shm"));
    }

    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                SQLITE_OPEN_FULLMUTEX;
    if (fMemory) {
        flags |= SQLITE_OPEN_MEMORY;
    } else {
        TryCreateDirectories(path.parent_path());
    }

    int rc = sqlite3_open_v2(pathStr.c_str(), &m_db, flags, nullptr);
    if (rc != SQLITE_OK) {
        std::string err = m_db ? sqlite3_errmsg(m_db) : "unknown";
        if (m_db) {
            sqlite3_close(m_db);
            m_db = nullptr;
        }
        throw std::runtime_error("Failed to open SQLite database " +
                                 pathStr + ": " + err);
    }

    ConfigureForPerformance();
}

CSqliteWrapper::~CSqliteWrapper() {
    Cleanup();
}

void CSqliteWrapper::ConfigureForPerformance() {
    // WAL mode: allows concurrent readers during writes
    ExecSQL("PRAGMA journal_mode = WAL");

    // NORMAL sync: fsync only on WAL checkpoint, not every commit.
    // Safe with WAL — a power loss may lose the last transaction but cannot
    // corrupt the database.
    ExecSQL("PRAGMA synchronous = NORMAL");

    // 64MB page cache (~16k pages at 4KB each)
    ExecSQL("PRAGMA cache_size = -65536");

    // 256MB memory-mapped I/O for reads — bypasses the page cache for
    // sequential scans and large UTXO lookups
    ExecSQL("PRAGMA mmap_size = 268435456");

    // 64MB WAL size limit before auto-checkpoint
    ExecSQL("PRAGMA journal_size_limit = 67108864");

    // Keep temp tables in memory
    ExecSQL("PRAGMA temp_store = MEMORY");

    // 30 second busy timeout with internal retry
    sqlite3_busy_timeout(m_db, 30000);
}

bool CSqliteWrapper::ExecSQL(const std::string &sql) {
    char *errmsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        LogPrintf("SQLite exec error: %s (sql: %.100s)\n",
                  errmsg ? errmsg : "unknown", sql);
        sqlite3_free(errmsg);
        return false;
    }
    return true;
}

sqlite3_stmt *CSqliteWrapper::Prepare(const std::string &sql) {
    std::lock_guard<std::mutex> lock(m_stmt_mutex);

    auto it = m_stmt_cache.find(sql);
    if (it != m_stmt_cache.end()) {
        sqlite3_reset(it->second);
        sqlite3_clear_bindings(it->second);
        return it->second;
    }

    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(m_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare SQL: " +
                                 std::string(sqlite3_errmsg(m_db)) +
                                 " (sql: " + sql + ")");
    }

    m_stmt_cache[sql] = stmt;
    return stmt;
}

bool CSqliteWrapper::BeginTransaction() {
    // Serialize with any other writer on this wrapper. SQLite only allows
    // one open transaction per connection, and we share this wrapper across
    // block-index writes (CBlockTreeSqlite::WriteBatchSync), block
    // analytics (CBlockAnalytics::ConnectBlock, both from the validation
    // thread and the stats backfill thread), and the periodic collector's
    // own batches — they would otherwise race and produce "cannot start a
    // transaction within a transaction" failures.
    m_txn_mutex.lock();
    if (!ExecSQL("BEGIN IMMEDIATE")) {
        m_txn_mutex.unlock();
        return false;
    }
    return true;
}

bool CSqliteWrapper::CommitTransaction() {
    bool ok = ExecSQL("COMMIT");
    m_txn_mutex.unlock();
    return ok;
}

bool CSqliteWrapper::RollbackTransaction() {
    bool ok = ExecSQL("ROLLBACK");
    m_txn_mutex.unlock();
    return ok;
}

bool CSqliteWrapper::IsInTransaction() const {
    return sqlite3_get_autocommit(m_db) == 0;
}

size_t CSqliteWrapper::EstimateSize() const {
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(
        m_db, "SELECT page_count * page_size FROM pragma_page_count(), "
              "pragma_page_size()",
        -1, &stmt, nullptr);
    if (rc != SQLITE_OK || sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return 0;
    }
    size_t sz = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    sqlite3_finalize(stmt);
    return sz;
}

void CSqliteWrapper::Cleanup() noexcept {
    {
        std::lock_guard<std::mutex> lock(m_stmt_mutex);
        for (auto &[sql, stmt] : m_stmt_cache) {
            sqlite3_finalize(stmt);
        }
        m_stmt_cache.clear();
    }

    if (m_db) {
        // Flush WAL to main database file before closing
        int nLog = 0, nCkpt = 0;
        int rc = sqlite3_wal_checkpoint_v2(
            m_db, nullptr, SQLITE_CHECKPOINT_TRUNCATE, &nLog, &nCkpt);
        if (rc != SQLITE_OK) {
            LogPrintf("SQLite WAL checkpoint warning: %s\n",
                      sqlite3_errstr(rc));
        }

        rc = sqlite3_close(m_db);
        if (rc != SQLITE_OK) {
            LogPrintf("SQLite close error: %s\n", sqlite3_errstr(rc));
        }
        m_db = nullptr;
    }
}

// CSqliteTransaction implementation

CSqliteTransaction::CSqliteTransaction(CSqliteWrapper &db) : m_db(db) {
    if (!m_db.BeginTransaction()) {
        throw std::runtime_error("Failed to begin SQLite transaction");
    }
}

CSqliteTransaction::~CSqliteTransaction() {
    if (!m_committed && !m_aborted) {
        // Auto-rollback on exception / forgotten commit
        m_db.RollbackTransaction();
    }
}

void CSqliteTransaction::Commit() {
    if (!m_committed && !m_aborted) {
        if (!m_db.CommitTransaction()) {
            throw std::runtime_error("Failed to commit SQLite transaction");
        }
        m_committed = true;
    }
}

void CSqliteTransaction::Abort() {
    if (!m_committed && !m_aborted) {
        m_db.RollbackTransaction();
        m_aborted = true;
    }
}
