// Copyright (c) 2024 The Logos Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SQLITE_BLOCK_TREE_SQLITE_H
#define BITCOIN_SQLITE_BLOCK_TREE_SQLITE_H

#include <blockfileinfo.h>
#include <primitives/block.h>
#include <sqlite/block_tree_db.h>
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
 * Block tree database backed by SQLite with column-based storage for efficient
 * querying by height, hash, and status.
 */
class CBlockTreeSqlite : public IBlockTreeDB {
public:
    explicit CBlockTreeSqlite(const fs::path &db_path,
                              size_t nCacheSize = 0,
                              bool fMemory = false, bool fWipe = false);

    bool WriteBatchSync(
        const std::vector<std::pair<int, const CBlockFileInfo *>> &fileInfo,
        int nLastFile,
        const std::vector<const CBlockIndex *> &blockinfo) override;

    bool ReadBlockFileInfo(int nFile, CBlockFileInfo &info) override;
    bool ReadLastBlockFile(int &nFile) override;
    bool WriteReindexing(bool fReindexing) override;
    bool IsReindexing() const override;
    bool WriteFlag(const std::string &name, bool fValue) override;
    bool ReadFlag(const std::string &name, bool &fValue) override;

    bool LoadBlockIndexGuts(
        const Consensus::Params &params,
        std::function<CBlockIndex *(const BlockHash &)> insertBlockIndex)
        override;

    bool Upgrade(const Consensus::Params &params) override;

    bool IsEmpty();
    CSqliteWrapper &GetDb() { return *m_db; }

private:
    std::unique_ptr<CSqliteWrapper> m_db;
    void InitSchema();
};

#endif // BITCOIN_SQLITE_BLOCK_TREE_SQLITE_H
