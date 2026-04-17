// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <sqlite/coins_view_sqlite.h>
#include <sqlite/schema.h>

#include <logging.h>
#include <primitives/blockhash.h>
#include <primitives/transaction.h>
#include <sqlite3.h>
#include <util/system.h>

#include <cassert>

// Bind a 32-byte hash (TxId, BlockHash) to a statement parameter.
static void BindHash256(sqlite3_stmt *stmt, int idx, const uint256 &hash) {
    sqlite3_bind_blob(stmt, idx, hash.begin(), 32, SQLITE_STATIC);
}

// Read a 32-byte hash from a column.
static uint256 ColumnHash256(sqlite3_stmt *stmt, int col) {
    const void *data = sqlite3_column_blob(stmt, col);
    int len = sqlite3_column_bytes(stmt, col);
    uint256 out;
    if (data && len == 32) {
        memcpy(out.begin(), data, 32);
    }
    return out;
}

CCoinsViewSqlite::CCoinsViewSqlite(const fs::path &db_path, size_t nCacheSize,
                                   bool fMemory, bool fWipe)
    : m_db_path(db_path), m_is_memory(fMemory) {
    m_db = std::make_unique<CSqliteWrapper>(db_path, fMemory, fWipe);
    InitSchema();
}

CCoinsViewSqlite::~CCoinsViewSqlite() = default;

void CCoinsViewSqlite::InitSchema() {
    sqlite_schema::CreateAllTables(m_db->GetHandle());
    sqlite_schema::CreateAllIndexes(m_db->GetHandle());
}

bool CCoinsViewSqlite::GetCoin(const COutPoint &outpoint, Coin &coin) const {
    static const std::string sql =
        "SELECT height, coinbase, amount, script FROM utxos "
        "WHERE txid = ?1 AND vout = ?2";

    sqlite3_stmt *stmt = const_cast<CSqliteWrapper *>(m_db.get())->Prepare(sql);

    BindHash256(stmt, 1, outpoint.GetTxId());
    sqlite3_bind_int(stmt, 2, outpoint.GetN());

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_reset(stmt);
        return false;
    }

    uint32_t height = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
    bool coinbase = sqlite3_column_int(stmt, 1) != 0;

    Amount amount = static_cast<int64_t>(sqlite3_column_int64(stmt, 2)) * SATOSHI;

    const void *scriptData = sqlite3_column_blob(stmt, 3);
    int scriptLen = sqlite3_column_bytes(stmt, 3);
    CScript script;
    if (scriptData && scriptLen > 0) {
        const uint8_t *p = static_cast<const uint8_t *>(scriptData);
        script.assign(p, p + scriptLen);
    }

    coin = Coin(CTxOut(amount, std::move(script)), height, coinbase);

    sqlite3_reset(stmt);
    return true;
}

bool CCoinsViewSqlite::HaveCoin(const COutPoint &outpoint) const {
    static const std::string sql =
        "SELECT 1 FROM utxos WHERE txid = ?1 AND vout = ?2";

    sqlite3_stmt *stmt = const_cast<CSqliteWrapper *>(m_db.get())->Prepare(sql);

    BindHash256(stmt, 1, outpoint.GetTxId());
    sqlite3_bind_int(stmt, 2, outpoint.GetN());

    int rc = sqlite3_step(stmt);
    bool found = (rc == SQLITE_ROW);
    sqlite3_reset(stmt);
    return found;
}

BlockHash CCoinsViewSqlite::GetBestBlock() const {
    static const std::string sql =
        "SELECT value FROM meta WHERE key = 'best_block'";

    sqlite3_stmt *stmt = const_cast<CSqliteWrapper *>(m_db.get())->Prepare(sql);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_reset(stmt);
        return BlockHash();
    }

    BlockHash hash(ColumnHash256(stmt, 0));
    sqlite3_reset(stmt);
    return hash;
}

std::vector<BlockHash> CCoinsViewSqlite::GetHeadBlocks() const {
    static const std::string sql =
        "SELECT value FROM meta WHERE key = 'head_blocks'";

    sqlite3_stmt *stmt = const_cast<CSqliteWrapper *>(m_db.get())->Prepare(sql);

    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_reset(stmt);
        return {};
    }

    const void *data = sqlite3_column_blob(stmt, 0);
    int len = sqlite3_column_bytes(stmt, 0);

    std::vector<BlockHash> result;
    if (data && len >= 64) {
        BlockHash h1, h2;
        memcpy(h1.begin(), data, 32);
        memcpy(h2.begin(), static_cast<const uint8_t *>(data) + 32, 32);
        result.push_back(h1);
        result.push_back(h2);
    }

    sqlite3_reset(stmt);
    return result;
}

bool CCoinsViewSqlite::BatchWrite(CCoinsMap &mapCoins,
                                  const BlockHash &hashBlock) {
    assert(!hashBlock.IsNull());

    // Retrieve old tip for two-phase commit protocol
    BlockHash old_tip = GetBestBlock();
    if (old_tip.IsNull()) {
        std::vector<BlockHash> old_heads = GetHeadBlocks();
        if (old_heads.size() == 2) {
            assert(old_heads[0] == hashBlock);
            old_tip = old_heads[1];
        }
    }

    // Single IMMEDIATE transaction for the entire batch — atomic
    if (!m_db->BeginTransaction()) {
        return false;
    }

    // Phase 1: Mark in-progress (erase best_block, write head_blocks)
    {
        sqlite3_stmt *del =
            m_db->Prepare("DELETE FROM meta WHERE key = 'best_block'");
        sqlite3_step(del);
        sqlite3_reset(del);

        uint8_t buf[64];
        memcpy(buf, hashBlock.begin(), 32);
        memcpy(buf + 32, old_tip.begin(), 32);

        sqlite3_stmt *ins = m_db->Prepare(
            "INSERT OR REPLACE INTO meta(key, value) VALUES('head_blocks', ?1)");
        sqlite3_bind_blob(ins, 1, buf, 64, SQLITE_STATIC);
        sqlite3_step(ins);
        sqlite3_reset(ins);
    }

    // Phase 2: Apply coin changes
    sqlite3_stmt *insert_stmt = m_db->Prepare(
        "INSERT OR REPLACE INTO utxos(txid, vout, height, coinbase, amount, "
        "script) VALUES(?1, ?2, ?3, ?4, ?5, ?6)");

    sqlite3_stmt *erase_stmt =
        m_db->Prepare("DELETE FROM utxos WHERE txid = ?1 AND vout = ?2");

    size_t count = 0;
    size_t changed = 0;

    for (auto it = mapCoins.begin(); it != mapCoins.end();) {
        if (it->second.flags & CCoinsCacheEntry::DIRTY) {
            if (it->second.coin.IsSpent()) {
                BindHash256(erase_stmt, 1, it->first.GetTxId());
                sqlite3_bind_int(erase_stmt, 2, it->first.GetN());
                sqlite3_step(erase_stmt);
                sqlite3_reset(erase_stmt);
            } else {
                const Coin &coin = it->second.coin;
                const CTxOut &out = coin.GetTxOut();

                BindHash256(insert_stmt, 1, it->first.GetTxId());
                sqlite3_bind_int(insert_stmt, 2, it->first.GetN());
                sqlite3_bind_int(insert_stmt, 3, coin.GetHeight());
                sqlite3_bind_int(insert_stmt, 4, coin.IsCoinBase() ? 1 : 0);
                sqlite3_bind_int64(insert_stmt, 5, out.nValue / SATOSHI);
                sqlite3_bind_blob(insert_stmt, 6, out.scriptPubKey.data(),
                                  out.scriptPubKey.size(), SQLITE_STATIC);

                sqlite3_step(insert_stmt);
                sqlite3_reset(insert_stmt);
            }
            changed++;
        }
        count++;
        auto itOld = it++;
        mapCoins.erase(itOld);
    }

    // Phase 3: Mark complete (erase head_blocks, write best_block)
    {
        sqlite3_stmt *del =
            m_db->Prepare("DELETE FROM meta WHERE key = 'head_blocks'");
        sqlite3_step(del);
        sqlite3_reset(del);

        sqlite3_stmt *ins = m_db->Prepare(
            "INSERT OR REPLACE INTO meta(key, value) VALUES('best_block', ?1)");
        BindHash256(ins, 1, hashBlock);
        sqlite3_step(ins);
        sqlite3_reset(ins);
    }

    bool ret = m_db->CommitTransaction();

    LogPrint(BCLog::COINDB,
             "SQLite: committed %u changed transaction outputs (out of %u) to "
             "coin database\n",
             (unsigned int)changed, (unsigned int)count);
    return ret;
}

CCoinsViewCursor *CCoinsViewSqlite::Cursor() const {
    BlockHash bestBlock = GetBestBlock();

    // Allocate a new statement (NOT from the cache) since the cursor holds it
    sqlite3_stmt *stmt = nullptr;
    int rc = sqlite3_prepare_v2(
        m_db->GetHandle(),
        "SELECT txid, vout, height, coinbase, amount, script FROM utxos", -1,
        &stmt, nullptr);

    if (rc != SQLITE_OK) {
        return nullptr;
    }

    return new CCoinsViewSqliteCursor(bestBlock, stmt);
}

size_t CCoinsViewSqlite::EstimateSize() const {
    return m_db->EstimateSize();
}

bool CCoinsViewSqlite::Upgrade() {
    // No legacy format to upgrade from — SQLite is always current.
    return true;
}

void CCoinsViewSqlite::ResizeCache(size_t new_cache_size) {
    // SQLite cache is configured via PRAGMA, not by recreating the DB.
    int pages = static_cast<int>(new_cache_size / 4096);
    if (pages < 1000) {
        pages = 1000;
    }
    m_db->ExecSQL("PRAGMA cache_size = -" + std::to_string(pages * 4));
}

// CCoinsViewSqliteCursor implementation

CCoinsViewSqliteCursor::CCoinsViewSqliteCursor(const BlockHash &hashBlock,
                                               sqlite3_stmt *stmt)
    : CCoinsViewCursor(hashBlock), m_stmt(stmt), m_valid(false) {
    Advance();
}

CCoinsViewSqliteCursor::~CCoinsViewSqliteCursor() {
    if (m_stmt) {
        sqlite3_finalize(m_stmt);
    }
}

void CCoinsViewSqliteCursor::Advance() {
    m_valid = (sqlite3_step(m_stmt) == SQLITE_ROW);
}

bool CCoinsViewSqliteCursor::Valid() const {
    return m_valid;
}

void CCoinsViewSqliteCursor::Next() {
    Advance();
}

bool CCoinsViewSqliteCursor::GetKey(COutPoint &key) const {
    if (!m_valid) {
        return false;
    }
    TxId txid(ColumnHash256(m_stmt, 0));
    uint32_t n = static_cast<uint32_t>(sqlite3_column_int(m_stmt, 1));
    key = COutPoint(txid, n);
    return true;
}

bool CCoinsViewSqliteCursor::GetValue(Coin &coin) const {
    if (!m_valid) {
        return false;
    }

    uint32_t height = static_cast<uint32_t>(sqlite3_column_int(m_stmt, 2));
    bool coinbase = sqlite3_column_int(m_stmt, 3) != 0;
    Amount amount = static_cast<int64_t>(sqlite3_column_int64(m_stmt, 4)) * SATOSHI;

    const void *scriptData = sqlite3_column_blob(m_stmt, 5);
    int scriptLen = sqlite3_column_bytes(m_stmt, 5);
    CScript script;
    if (scriptData && scriptLen > 0) {
        const uint8_t *p = static_cast<const uint8_t *>(scriptData);
        script.assign(p, p + scriptLen);
    }

    coin = Coin(CTxOut(amount, std::move(script)), height, coinbase);
    return true;
}

unsigned int CCoinsViewSqliteCursor::GetValueSize() const {
    if (!m_valid) {
        return 0;
    }
    // Approximate: 4 (height) + 1 (coinbase) + 8 (amount) + script length
    return 13 + sqlite3_column_bytes(m_stmt, 5);
}
