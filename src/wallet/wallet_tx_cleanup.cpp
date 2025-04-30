// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2019 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <wallet/wallet.h>

#include <sync.h>
#include <util/time.h>
#include <logging.h>

/**
 * Delete transactions that are not in the mempool and have been in the wallet
 * for more than the specified number of seconds.
 * This function is automatically called every 10 seconds by a scheduler.
 * 
 * @param max_tx_age Maximum age of transactions in seconds
 * @return Number of transactions removed
 */
int CWallet::DeleteStuckTransactions(int64_t max_tx_age) {
    LOCK(cs_wallet);
    
    // Get current time
    int64_t now = GetTime();
    
    // Track txids to remove
    std::vector<TxId> txids_to_remove;
    
    // Loop through all wallet transactions
    for (const auto& [txid, wtx] : mapWallet) {
        // Skip transactions that are in the mempool
        if (wtx.InMempool()) {
            continue;
        }
        
        // Skip confirmed transactions
        if (wtx.GetDepthInMainChain() > 0) {
            continue;
        }

        // Skip abandoned transactions
        if (wtx.isAbandoned()) {
            continue;
        }

        // Skip transactions that are too new
        if (now - wtx.nTimeReceived < max_tx_age) {
            continue;
        }
        
        // This transaction is stuck - add it to our removal list
        txids_to_remove.push_back(txid);
    }
    
    // If no transactions need to be removed, return early
    if (txids_to_remove.empty()) {
        return 0;
    }
    
    // Remove the transactions
    std::vector<TxId> txids_removed;
    if (ZapSelectTx(txids_to_remove, txids_removed) != DBErrors::LOAD_OK) {
        // If there was an error, return 0
        WalletLogPrintf("DeleteStuckTransactions: Error removing transactions\n");
        return 0;
    }
    
    // Log the result
    WalletLogPrintf("DeleteStuckTransactions: Removed %d stuck transactions\n", txids_removed.size());
    
    // Return the number of transactions removed
    return txids_removed.size();
}

/**
 * Schedule the transaction cleanup task to run every 10 seconds.
 * This should be called during wallet initialization.
 */
void CWallet::ScheduleTransactionCleanup() {
    // Create a repeating task that runs every 10 seconds
    if (!m_tx_cleanup_timer) {
        m_tx_cleanup_timer = std::make_unique<boost::asio::deadline_timer>(
            m_io_service, boost::posix_time::seconds(10));
    } else {
        // Reset the existing timer's expiration
        m_tx_cleanup_timer->expires_from_now(boost::posix_time::seconds(10));
    }
    
    // Set up the timer callback
    m_tx_cleanup_timer->async_wait([this](const boost::system::error_code& error) {
        if (!error) {
            // Default to removing transactions older than 10 seconds
            DeleteStuckTransactions(10);
            
            // Reschedule the timer
            ScheduleTransactionCleanup();
        } else {
            // Log any errors
            WalletLogPrintf("Transaction cleanup timer error: %s\n", error.message());
        }
    });

    // Start the io_service if it's not already running
    if (!m_io_service.stopped()) {
        try {
            m_io_service.poll();
        } catch (const std::exception& e) {
            WalletLogPrintf("Error in transaction cleanup timer service: %s\n", e.what());
        }
    }
}