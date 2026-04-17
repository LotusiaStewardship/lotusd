// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SQLITE_WRAPPER_H
#define BITCOIN_SQLITE_WRAPPER_H

#include <fs.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

struct sqlite3;
struct sqlite3_stmt;

/**
 * Low-level SQLite3 wrapper optimized for blockchain storage.
 *
 * Performance design:
 * - WAL journal mode (concurrent readers, single writer)
 * - Memory-mapped I/O for reads (256MB default)
 * - Prepared statement cache (avoids SQL reparsing)
 * - NORMAL synchronous (fsync on commit only)
 * - 64MB page cache
 * - Busy timeout with exponential backoff
 */
class CSqliteWrapper {
public:
    CSqliteWrapper(const fs::path &path, bool fMemory = false,
                   bool fWipe = false);
    ~CSqliteWrapper();

    CSqliteWrapper(const CSqliteWrapper &) = delete;
    CSqliteWrapper &operator=(const CSqliteWrapper &) = delete;

    bool ExecSQL(const std::string &sql);

    /**
     * Get or create a prepared statement from the cache.
     * The returned pointer is owned by this wrapper; do NOT finalize it.
     * Thread-safe: statement cache is protected by m_stmt_mutex.
     */
    sqlite3_stmt *Prepare(const std::string &sql);

    bool BeginTransaction();
    bool CommitTransaction();
    bool RollbackTransaction();

    bool IsInTransaction() const;

    sqlite3 *GetHandle() { return m_db; }
    const fs::path &GetPath() const { return m_path; }

    /**
     * Estimate database size in bytes (page_count * page_size).
     */
    size_t EstimateSize() const;

private:
    void ConfigureForPerformance();
    void Cleanup() noexcept;

    sqlite3 *m_db{nullptr};
    fs::path m_path;
    bool m_is_memory;

    mutable std::mutex m_stmt_mutex;
    std::unordered_map<std::string, sqlite3_stmt *> m_stmt_cache;
};

/**
 * RAII guard for SQLite transactions.
 * Commits on destruction unless Abort() was called or an exception is in
 * flight.
 */
class CSqliteTransaction {
public:
    explicit CSqliteTransaction(CSqliteWrapper &db);
    ~CSqliteTransaction();

    CSqliteTransaction(const CSqliteTransaction &) = delete;
    CSqliteTransaction &operator=(const CSqliteTransaction &) = delete;

    void Commit();
    void Abort();

private:
    CSqliteWrapper &m_db;
    bool m_committed{false};
    bool m_aborted{false};
};

#endif // BITCOIN_SQLITE_WRAPPER_H
