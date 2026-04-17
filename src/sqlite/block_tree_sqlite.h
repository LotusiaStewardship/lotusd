// Copyright (c) 2024 The Logos Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SQLITE_BLOCK_TREE_SQLITE_H
#define BITCOIN_SQLITE_BLOCK_TREE_SQLITE_H

#include <blockfileinfo.h>
#include <primitives/block.h>
#include <sqlite/sqlite_wrapper.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct BlockHash;
class CBlockIndex;

namespace Consensus {
struct Params;
}

/**
 * Block tree database backed by SQLite.
 * Replaces CBlockTreeDB (LevelDB) with column-based storage for efficient
 * querying by height, hash, and status.
 */
class CBlockTreeSqlite {
public:
    explicit CBlockTreeSqlite(const fs::path &db_path,
                              size_t nCacheSize = 0,
                              bool fMemory = false, bool fWipe = false);

    bool WriteBatchSync(
        const std::vector<std::pair<int, const CBlockFileInfo *>> &fileInfo,
        int nLastFile,
        const std::vector<const CBlockIndex *> &blockinfo);

    bool ReadBlockFileInfo(int nFile, CBlockFileInfo &info);
    bool ReadLastBlockFile(int &nFile);
    bool WriteReindexing(bool fReindexing);
    bool IsReindexing() const;
    bool WriteFlag(const std::string &name, bool fValue);
    bool ReadFlag(const std::string &name, bool &fValue);

    bool LoadBlockIndexGuts(
        const Consensus::Params &params,
        std::function<CBlockIndex *(const BlockHash &)> insertBlockIndex);

    bool Upgrade(const Consensus::Params &params);

    bool IsEmpty();
    CSqliteWrapper &GetDb() { return *m_db; }

private:
    std::unique_ptr<CSqliteWrapper> m_db;
    void InitSchema();
};

#endif // BITCOIN_SQLITE_BLOCK_TREE_SQLITE_H
