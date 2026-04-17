// Copyright (c) 2024 The Logos Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SQLITE_COINS_VIEW_SQLITE_H
#define BITCOIN_SQLITE_COINS_VIEW_SQLITE_H

#include <coins.h>
#include <sqlite/sqlite_wrapper.h>

#include <memory>

struct sqlite3_stmt;

/**
 * CCoinsView backed by SQLite.
 *
 * Drop-in replacement for CCoinsViewDB (LevelDB). All prepared statements
 * are cached for the lifetime of the object. BatchWrite uses a single
 * IMMEDIATE transaction for crash-consistent atomic updates.
 *
 * Performance characteristics vs LevelDB:
 * - GetCoin: single indexed lookup via prepared statement (~2-5us)
 * - HaveCoin: EXISTS query, avoids full row read (~1-3us)
 * - BatchWrite: all changes in one transaction, WAL-mode append
 * - Cursor: streaming SELECT, no snapshot overhead
 */
class CCoinsViewSqlite final : public CCoinsView {
public:
    explicit CCoinsViewSqlite(const fs::path &db_path, size_t nCacheSize = 0,
                              bool fMemory = false, bool fWipe = false);
    ~CCoinsViewSqlite() override;

    bool GetCoin(const COutPoint &outpoint, Coin &coin) const override;
    bool HaveCoin(const COutPoint &outpoint) const override;
    BlockHash GetBestBlock() const override;
    std::vector<BlockHash> GetHeadBlocks() const override;
    bool BatchWrite(CCoinsMap &mapCoins, const BlockHash &hashBlock) override;
    CCoinsViewCursor *Cursor() const override;
    size_t EstimateSize() const override;

    bool Upgrade();
    void ResizeCache(size_t new_cache_size);

    CSqliteWrapper &GetDb() { return *m_db; }

private:
    std::unique_ptr<CSqliteWrapper> m_db;
    fs::path m_db_path;
    bool m_is_memory;

    void InitSchema();
};

/**
 * Cursor for iterating over the SQLite UTXO set.
 * Steps through all rows in the utxos table via a single prepared statement.
 */
class CCoinsViewSqliteCursor : public CCoinsViewCursor {
public:
    CCoinsViewSqliteCursor(const BlockHash &hashBlock, sqlite3_stmt *stmt);
    ~CCoinsViewSqliteCursor() override;

    bool GetKey(COutPoint &key) const override;
    bool GetValue(Coin &coin) const override;
    unsigned int GetValueSize() const override;
    bool Valid() const override;
    void Next() override;

private:
    sqlite3_stmt *m_stmt;
    bool m_valid;
    void Advance();
};

#endif // BITCOIN_SQLITE_COINS_VIEW_SQLITE_H
