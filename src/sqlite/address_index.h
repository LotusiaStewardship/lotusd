// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SQLITE_ADDRESS_INDEX_H
#define BITCOIN_SQLITE_ADDRESS_INDEX_H

#include <sqlite/sqlite_wrapper.h>

#include <cstdint>
#include <string>
#include <vector>

struct AddressHistoryEntry {
    std::string address;
    int block_height;
    std::vector<uint8_t> txid;
    int64_t net_value;
};

/**
 * Address index with denormalized balance aggregates.
 *
 * Maintains address_balances (O(1) balance lookup) and address_history
 * (paginated tx list) updated atomically during block connect/disconnect.
 *
 * Designed for addresses with 100k+ transactions — all balance queries
 * are O(1) via the denormalized table, never scan tx_outputs.
 */
class CAddressIndex {
public:
    explicit CAddressIndex(CSqliteWrapper &db);

    /**
     * Update address tables for a transaction's effects.
     * Call within the block's transaction, after outputs/inputs are written.
     *
     * @param address   The address being affected.
     * @param height    Block height.
     * @param txid_data Raw 32-byte txid.
     * @param received  Sats received by this address in this tx.
     * @param sent      Sats sent by this address in this tx.
     * @param utxo_delta Net change in UTXO count (+created, -spent).
     */
    void UpdateForTx(const std::string &address, int height,
                     const uint8_t *txid_data, int64_t received,
                     int64_t sent, int utxo_delta);

    /**
     * Reverse the effects of UpdateForTx during block disconnect.
     * Same parameters as the original call for this address+tx.
     */
    void UndoForTx(const std::string &address, int height,
                   const uint8_t *txid_data, int64_t received,
                   int64_t sent, int utxo_delta);

    int64_t GetBalance(const std::string &address) const;
    int64_t GetReceived(const std::string &address) const;
    int GetTxCount(const std::string &address) const;

    std::vector<AddressHistoryEntry> GetHistory(const std::string &address,
                                                int page, int pageSize) const;

private:
    CSqliteWrapper &m_db;
};

#endif // BITCOIN_SQLITE_ADDRESS_INDEX_H
