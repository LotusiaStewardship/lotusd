// Copyright (c) 2024 The Logos Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SQLITE_BLOCK_TREE_DB_H
#define BITCOIN_SQLITE_BLOCK_TREE_DB_H

#include <blockfileinfo.h>
#include <consensus/params.h>
#include <primitives/blockhash.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

class CBlockIndex;

/**
 * Abstract interface for the block tree database.
 * Abstract interface for the block tree database.
 */
class IBlockTreeDB {
public:
    virtual ~IBlockTreeDB() = default;

    virtual bool WriteBatchSync(
        const std::vector<std::pair<int, const CBlockFileInfo *>> &fileInfo,
        int nLastFile,
        const std::vector<const CBlockIndex *> &blockinfo) = 0;

    virtual bool ReadBlockFileInfo(int nFile, CBlockFileInfo &info) = 0;
    virtual bool ReadLastBlockFile(int &nFile) = 0;
    virtual bool WriteReindexing(bool fReindexing) = 0;
    virtual bool IsReindexing() const = 0;
    virtual bool WriteFlag(const std::string &name, bool fValue) = 0;
    virtual bool ReadFlag(const std::string &name, bool &fValue) = 0;

    virtual bool LoadBlockIndexGuts(
        const Consensus::Params &params,
        std::function<CBlockIndex *(const BlockHash &)> insertBlockIndex) = 0;

    virtual bool Upgrade(const Consensus::Params &params) = 0;
};

#endif // BITCOIN_SQLITE_BLOCK_TREE_DB_H
