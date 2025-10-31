// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MOCKTXGEN_H
#define BITCOIN_MOCKTXGEN_H

#include <amount.h>
#include <primitives/transaction.h>

#include <vector>

/**
 * Generate random transactions for testing
 * Uses previous coinbase outputs as inputs
 */
std::vector<CTransactionRef> GenerateRandomTransactions(int count, int currentHeight);

/**
 * Get a random mock script for coinbase payout
 */
class CScript;
CScript GetRandomMockScript();

/**
 * Register a coinbase transaction in the cache for later signing
 */
void RegisterMockCoinbase(const CTransactionRef& tx);

#endif // BITCOIN_MOCKTXGEN_H

