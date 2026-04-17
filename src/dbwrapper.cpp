// Copyright (c) 2012-2016 The Bitcoin Core developers
// Copyright (c) 2024 The Logos Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <dbwrapper.h>

#include <logging.h>
#include <random.h>

#include <algorithm>
#include <cstdint>

static void ExecSQL(sqlite3 *db, const char *sql) {
    char *err = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        throw dbwrapper_error("SQLite exec failed: " + msg);
    }
}

CDBWrapper::CDBWrapper(const fs::path &path, size_t nCacheSize, bool fMemory,
                       bool fWipe, bool obfuscate)
    : m_db(nullptr), m_name{fs::PathToString(path.stem())},
      m_read_stmt(nullptr), m_exists_stmt(nullptr), m_write_stmt(nullptr),
      m_erase_stmt(nullptr) {

    std::string dbpath;
    if (fMemory) {
        dbpath = ":memory:";
    } else {
        if (fWipe) {
            LogPrintf("Wiping SQLite DB in %s\n", fs::PathToString(path));
            fs::path sqlitePath = path;
            sqlitePath += ".sqlite";
            fs::remove(sqlitePath);
            fs::path walPath = sqlitePath;
            walPath += "-wal";
            fs::path shmPath = sqlitePath;
            shmPath += "-shm";
            fs::remove(walPath);
            fs::remove(shmPath);
            // Remove old database directory if it exists
            if (fs::exists(path) && fs::is_directory(path)) {
                fs::remove_all(path);
            }
        }

        // Use .sqlite extension for files (path was originally a directory
        // The path becomes a .sqlite file.
        fs::path parentDir = path.parent_path();
        if (!parentDir.empty()) {
            TryCreateDirectories(parentDir);
        }
        fs::path sqlitePath = path;
        sqlitePath += ".sqlite";
        dbpath = fs::PathToString(sqlitePath);
        LogPrintf("Opening SQLite DB in %s\n", dbpath);
    }

    int rc = sqlite3_open(dbpath.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        throw dbwrapper_error("Failed to open SQLite DB: " +
                              std::string(sqlite3_errmsg(m_db)));
    }

    // Performance pragmas
    int cachePages = std::max((int)(nCacheSize / 4096), 256);
    std::string cachePragma =
        "PRAGMA cache_size = -" + std::to_string(cachePages) + ";";
    ExecSQL(m_db, "PRAGMA journal_mode = WAL;");
    ExecSQL(m_db, "PRAGMA synchronous = NORMAL;");
    ExecSQL(m_db, cachePragma.c_str());
    ExecSQL(m_db, "PRAGMA mmap_size = 268435456;");
    ExecSQL(m_db, "PRAGMA temp_store = MEMORY;");
    ExecSQL(m_db, "PRAGMA busy_timeout = 5000;");

    ExecSQL(m_db,
            "CREATE TABLE IF NOT EXISTS kv ("
            "  key BLOB PRIMARY KEY NOT NULL,"
            "  value BLOB NOT NULL"
            ") WITHOUT ROWID;");

    InitStatements();

    LogPrintf("Opened SQLite DB successfully\n");

    obfuscate_key = std::vector<uint8_t>(OBFUSCATE_KEY_NUM_BYTES, '\000');

    bool key_exists = Read(OBFUSCATE_KEY_KEY, obfuscate_key);

    if (!key_exists && obfuscate && IsEmpty()) {
        std::vector<uint8_t> new_key = CreateObfuscateKey();
        Write(OBFUSCATE_KEY_KEY, new_key);
        obfuscate_key = new_key;
        LogPrintf("Wrote new obfuscate key for %s: %s\n",
                  fs::PathToString(path), HexStr(obfuscate_key));
    }

    LogPrintf("Using obfuscation key for %s: %s\n", fs::PathToString(path),
              HexStr(obfuscate_key));
}

void CDBWrapper::InitStatements() {
    sqlite3_prepare_v2(m_db, "SELECT value FROM kv WHERE key = ?", -1,
                       &m_read_stmt, nullptr);
    sqlite3_prepare_v2(m_db, "SELECT 1 FROM kv WHERE key = ?", -1,
                       &m_exists_stmt, nullptr);
    sqlite3_prepare_v2(
        m_db, "INSERT OR REPLACE INTO kv (key, value) VALUES (?, ?)", -1,
        &m_write_stmt, nullptr);
    sqlite3_prepare_v2(m_db, "DELETE FROM kv WHERE key = ?", -1,
                       &m_erase_stmt, nullptr);
}

CDBWrapper::~CDBWrapper() {
    if (m_read_stmt) sqlite3_finalize(m_read_stmt);
    if (m_exists_stmt) sqlite3_finalize(m_exists_stmt);
    if (m_write_stmt) sqlite3_finalize(m_write_stmt);
    if (m_erase_stmt) sqlite3_finalize(m_erase_stmt);
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool CDBWrapper::WriteBatch(CDBBatch &batch, bool fSync) {
    ExecSQL(m_db, "BEGIN IMMEDIATE;");

    for (const auto &op : batch.ops) {
        if (op.is_erase) {
            sqlite3_reset(m_erase_stmt);
            sqlite3_bind_blob(m_erase_stmt, 1, op.key.data(), op.key.size(),
                              SQLITE_STATIC);
            int rc = sqlite3_step(m_erase_stmt);
            if (rc != SQLITE_DONE) {
                ExecSQL(m_db, "ROLLBACK;");
                dbwrapper_private::HandleError(rc, m_db);
                return false;
            }
        } else {
            sqlite3_reset(m_write_stmt);
            sqlite3_bind_blob(m_write_stmt, 1, op.key.data(), op.key.size(),
                              SQLITE_STATIC);
            sqlite3_bind_blob(m_write_stmt, 2, op.value.data(),
                              op.value.size(), SQLITE_STATIC);
            int rc = sqlite3_step(m_write_stmt);
            if (rc != SQLITE_DONE) {
                ExecSQL(m_db, "ROLLBACK;");
                dbwrapper_private::HandleError(rc, m_db);
                return false;
            }
        }
    }

    if (fSync) {
        ExecSQL(m_db, "COMMIT;");
        ExecSQL(m_db, "PRAGMA wal_checkpoint(PASSIVE);");
    } else {
        ExecSQL(m_db, "COMMIT;");
    }
    return true;
}

size_t CDBWrapper::DynamicMemoryUsage() const {
    sqlite3_int64 highwater = 0;
    sqlite3_db_status(m_db, SQLITE_DBSTATUS_CACHE_USED, nullptr,
                      (int *)&highwater, 0);
    return (size_t)highwater;
}

const std::string CDBWrapper::OBFUSCATE_KEY_KEY("\000obfuscate_key", 14);
const unsigned int CDBWrapper::OBFUSCATE_KEY_NUM_BYTES = 8;

std::vector<uint8_t> CDBWrapper::CreateObfuscateKey() const {
    uint8_t buff[OBFUSCATE_KEY_NUM_BYTES];
    GetRandBytes(buff, OBFUSCATE_KEY_NUM_BYTES);
    return std::vector<uint8_t>(&buff[0], &buff[OBFUSCATE_KEY_NUM_BYTES]);
}

bool CDBWrapper::IsEmpty() {
    std::unique_ptr<CDBIterator> it(NewIterator());
    it->SeekToFirst();
    return !(it->Valid());
}

CDBIterator *CDBWrapper::NewIterator() {
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(m_db,
                       "SELECT key, value FROM kv WHERE key >= ? ORDER BY key",
                       -1, &stmt, nullptr);
    return new CDBIterator(*this, stmt);
}

CDBIterator::CDBIterator(const CDBWrapper &_parent, sqlite3_stmt *_pStmt)
    : parent(_parent), pStmt(_pStmt), m_valid(false) {}

CDBIterator::~CDBIterator() {
    if (pStmt) {
        sqlite3_finalize(pStmt);
    }
}

bool CDBIterator::Valid() const {
    return m_valid;
}

void CDBIterator::SeekToFirst() {
    sqlite3_reset(pStmt);
    // Bind empty blob so key >= '' matches everything
    sqlite3_bind_blob(pStmt, 1, "", 0, SQLITE_STATIC);
    int rc = sqlite3_step(pStmt);
    m_valid = (rc == SQLITE_ROW);
}

void CDBIterator::Next() {
    int rc = sqlite3_step(pStmt);
    m_valid = (rc == SQLITE_ROW);
}

namespace dbwrapper_private {

void HandleError(int rc, sqlite3 *db) {
    if (rc == SQLITE_OK || rc == SQLITE_DONE || rc == SQLITE_ROW) {
        return;
    }
    std::string errmsg = "Fatal SQLite error (code " + std::to_string(rc) + ")";
    if (db) {
        errmsg += ": " + std::string(sqlite3_errmsg(db));
    }
    LogPrintf("%s\n", errmsg);
    throw dbwrapper_error(errmsg);
}

const std::vector<uint8_t> &GetObfuscateKey(const CDBWrapper &w) {
    return w.obfuscate_key;
}
}; // namespace dbwrapper_private
