// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SQLITE_BLOCK_ANALYTICS_H
#define BITCOIN_SQLITE_BLOCK_ANALYTICS_H

#include <sqlite/address_index.h>
#include <sqlite/sqlite_wrapper.h>

#include <memory>

class CBlock;
class CBlockIndex;
class CChainParams;

/**
 * Populates analytics tables (transactions, tx_inputs, tx_outputs,
 * address_balances, address_history) during block connect/disconnect.
 *
 * Operates on blocks/index.sqlite alongside CBlockTreeSqlite.
 * All writes for a single block happen in one SQLite transaction.
 * Prevout data for inputs is resolved from our own tx_outputs table,
 * so this can be called after coins have been spent from the view.
 */
class CBlockAnalytics {
public:
    explicit CBlockAnalytics(CSqliteWrapper &db);

    /**
     * Record all transactions, inputs, outputs, and address effects
     * for a newly connected block.
     * Must NOT be called when fJustCheck is true.
     */
    void ConnectBlock(const CBlock &block, const CBlockIndex *pindex,
                      const CChainParams &params);

    /**
     * Reverse all analytics data for a disconnected block.
     * Restores spent flags, reverses address balances, and deletes
     * transaction/input/output rows.
     */
    void DisconnectBlock(const CBlock &block, const CBlockIndex *pindex,
                         const CChainParams &params);

private:
    CSqliteWrapper &m_db;
    CAddressIndex m_addr_index;
};

extern std::unique_ptr<CBlockAnalytics> g_block_analytics;

#endif // BITCOIN_SQLITE_BLOCK_ANALYTICS_H
