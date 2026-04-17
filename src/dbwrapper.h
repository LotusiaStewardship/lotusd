// Copyright (c) 2012-2016 The Bitcoin Core developers
// Copyright (c) 2024 The Logos Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_DBWRAPPER_H
#define BITCOIN_DBWRAPPER_H

#include <clientversion.h>
#include <fs.h>
#include <serialize.h>
#include <streams.h>
#include <util/strencodings.h>
#include <util/system.h>

#include <sqlite3.h>

#include <cstddef>
#include <string>
#include <vector>

static const size_t DBWRAPPER_PREALLOC_KEY_SIZE = 64;
static const size_t DBWRAPPER_PREALLOC_VALUE_SIZE = 1024;

class dbwrapper_error : public std::runtime_error {
public:
    explicit dbwrapper_error(const std::string &msg)
        : std::runtime_error(msg) {}
};

class CDBWrapper;

namespace dbwrapper_private {

void HandleError(int rc, sqlite3 *db = nullptr);

const std::vector<uint8_t> &GetObfuscateKey(const CDBWrapper &w);
}; // namespace dbwrapper_private

/**
 * Batch of changes queued to be written to a CDBWrapper.
 * Stores serialized key/value pairs and deletions, applied atomically.
 */
class CDBBatch {
    friend class CDBWrapper;

private:
    const CDBWrapper &parent;

    struct Op {
        bool is_erase;
        std::string key;
        std::string value;
    };
    std::vector<Op> ops;

    CDataStream ssKey;
    CDataStream ssValue;

    size_t size_estimate;

public:
    explicit CDBBatch(const CDBWrapper &_parent)
        : parent(_parent), ssKey(SER_DISK, CLIENT_VERSION),
          ssValue(SER_DISK, CLIENT_VERSION), size_estimate(0) {}

    void Clear() {
        ops.clear();
        size_estimate = 0;
    }

    template <typename K, typename V>
    void Write(const K &key, const V &value) {
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        std::string sKey(ssKey.data(), ssKey.size());

        ssValue.reserve(DBWRAPPER_PREALLOC_VALUE_SIZE);
        ssValue << value;
        ssValue.Xor(dbwrapper_private::GetObfuscateKey(parent));
        std::string sValue(ssValue.data(), ssValue.size());

        size_estimate += 3 + sKey.size() + sValue.size();
        ops.push_back({false, std::move(sKey), std::move(sValue)});
        ssKey.clear();
        ssValue.clear();
    }

    template <typename K> void Erase(const K &key) {
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        std::string sKey(ssKey.data(), ssKey.size());

        size_estimate += 2 + sKey.size();
        ops.push_back({true, std::move(sKey), {}});
        ssKey.clear();
    }

    size_t SizeEstimate() const { return size_estimate; }
};

class CDBIterator {
private:
    const CDBWrapper &parent;
    sqlite3_stmt *pStmt;
    bool m_valid;

public:
    CDBIterator(const CDBWrapper &_parent, sqlite3_stmt *_pStmt);
    ~CDBIterator();

    bool Valid() const;
    void SeekToFirst();
    void Next();

    template <typename K> void Seek(const K &key) {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;
        sqlite3_reset(pStmt);
        sqlite3_bind_blob(pStmt, 1, ssKey.data(), ssKey.size(),
                          SQLITE_TRANSIENT);
        int rc = sqlite3_step(pStmt);
        m_valid = (rc == SQLITE_ROW);
    }

    template <typename K> bool GetKey(K &key) {
        const void *data = sqlite3_column_blob(pStmt, 0);
        int len = sqlite3_column_bytes(pStmt, 0);
        try {
            CDataStream ssKey((const char *)data, (const char *)data + len,
                              SER_DISK, CLIENT_VERSION);
            ssKey >> key;
        } catch (const std::exception &) {
            return false;
        }
        return true;
    }

    template <typename V> bool GetValue(V &value) {
        const void *data = sqlite3_column_blob(pStmt, 1);
        int len = sqlite3_column_bytes(pStmt, 1);
        try {
            CDataStream ssValue((const char *)data, (const char *)data + len,
                                SER_DISK, CLIENT_VERSION);
            ssValue.Xor(dbwrapper_private::GetObfuscateKey(parent));
            ssValue >> value;
        } catch (const std::exception &) {
            return false;
        }
        return true;
    }

    unsigned int GetValueSize() {
        return sqlite3_column_bytes(pStmt, 1);
    }
};

class CDBWrapper {
    friend const std::vector<uint8_t> &
    dbwrapper_private::GetObfuscateKey(const CDBWrapper &w);

private:
    sqlite3 *m_db;

    std::string m_name;

    std::vector<uint8_t> obfuscate_key;

    static const std::string OBFUSCATE_KEY_KEY;

    static const unsigned int OBFUSCATE_KEY_NUM_BYTES;

    std::vector<uint8_t> CreateObfuscateKey() const;

    sqlite3_stmt *m_read_stmt;
    sqlite3_stmt *m_exists_stmt;
    sqlite3_stmt *m_write_stmt;
    sqlite3_stmt *m_erase_stmt;

    void InitStatements();

public:
    /**
     * @param[in] path        Location in the filesystem where database will be
     *                        stored.
     * @param[in] nCacheSize  Configures SQLite cache size.
     * @param[in] fMemory     If true, use in-memory database.
     * @param[in] fWipe       If true, remove all existing data.
     * @param[in] obfuscate   If true, store data obfuscated via simple XOR.
     */
    CDBWrapper(const fs::path &path, size_t nCacheSize, bool fMemory = false,
               bool fWipe = false, bool obfuscate = false);
    ~CDBWrapper();

    CDBWrapper(const CDBWrapper &) = delete;
    CDBWrapper &operator=(const CDBWrapper &) = delete;

    template <typename K, typename V> bool Read(const K &key, V &value) const {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;

        sqlite3_reset(m_read_stmt);
        sqlite3_bind_blob(m_read_stmt, 1, ssKey.data(), ssKey.size(),
                          SQLITE_TRANSIENT);

        int rc = sqlite3_step(m_read_stmt);
        if (rc != SQLITE_ROW) {
            sqlite3_reset(m_read_stmt);
            if (rc == SQLITE_DONE) {
                return false;
            }
            LogPrintf("SQLite read failure: %s\n", sqlite3_errmsg(m_db));
            dbwrapper_private::HandleError(rc, m_db);
        }

        const void *blob = sqlite3_column_blob(m_read_stmt, 0);
        int blobLen = sqlite3_column_bytes(m_read_stmt, 0);

        try {
            CDataStream ssValue((const char *)blob, (const char *)blob + blobLen,
                                SER_DISK, CLIENT_VERSION);
            ssValue.Xor(obfuscate_key);
            ssValue >> value;
        } catch (const std::exception &) {
            sqlite3_reset(m_read_stmt);
            return false;
        }
        sqlite3_reset(m_read_stmt);
        return true;
    }

    template <typename K, typename V>
    bool Write(const K &key, const V &value, bool fSync = false) {
        CDBBatch batch(*this);
        batch.Write(key, value);
        return WriteBatch(batch, fSync);
    }

    template <typename K> bool Exists(const K &key) const {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        ssKey.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey << key;

        sqlite3_reset(m_exists_stmt);
        sqlite3_bind_blob(m_exists_stmt, 1, ssKey.data(), ssKey.size(),
                          SQLITE_TRANSIENT);

        int rc = sqlite3_step(m_exists_stmt);
        sqlite3_reset(m_exists_stmt);

        if (rc == SQLITE_ROW) {
            return true;
        }
        if (rc == SQLITE_DONE) {
            return false;
        }
        LogPrintf("SQLite read failure: %s\n", sqlite3_errmsg(m_db));
        dbwrapper_private::HandleError(rc, m_db);
        return false;
    }

    template <typename K> bool Erase(const K &key, bool fSync = false) {
        CDBBatch batch(*this);
        batch.Erase(key);
        return WriteBatch(batch, fSync);
    }

    bool WriteBatch(CDBBatch &batch, bool fSync = false);

    size_t DynamicMemoryUsage() const;

    CDBIterator *NewIterator();

    bool IsEmpty();

    template <typename K>
    size_t EstimateSize(const K &key_begin, const K &key_end) const {
        CDataStream ssKey1(SER_DISK, CLIENT_VERSION),
            ssKey2(SER_DISK, CLIENT_VERSION);
        ssKey1.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey2.reserve(DBWRAPPER_PREALLOC_KEY_SIZE);
        ssKey1 << key_begin;
        ssKey2 << key_end;

        sqlite3_stmt *stmt;
        sqlite3_prepare_v2(
            m_db,
            "SELECT SUM(LENGTH(key) + LENGTH(value)) FROM kv "
            "WHERE key >= ? AND key < ?",
            -1, &stmt, nullptr);
        sqlite3_bind_blob(stmt, 1, ssKey1.data(), ssKey1.size(),
                          SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 2, ssKey2.data(), ssKey2.size(),
                          SQLITE_TRANSIENT);
        size_t result = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return result;
    }

    template <typename K>
    void CompactRange(const K &key_begin, const K &key_end) const {
        // No-op for SQLite — the VACUUM equivalent is too expensive
        // for hot-path usage. Rely on WAL and auto-vacuum instead.
    }
};

#endif // BITCOIN_DBWRAPPER_H
