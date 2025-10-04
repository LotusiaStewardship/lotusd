// Copyright (c) 2025 The Lotus Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_COVENANT_H
#define BITCOIN_CONSENSUS_COVENANT_H

#include <cstdint>
#include <map>
#include <vector>

class CScript;
class CTransaction;
class CCoinsViewCache;

/**
 * Covenant token consensus rules
 * Enforces balance conservation for covenant tokens
 */

/**
 * Extract genesis ID from covenant token script
 * @param script The covenant token scriptPubKey
 * @return Genesis ID (32 bytes) or empty if not a covenant token
 */
std::vector<uint8_t> ExtractCovenantGenesis(const CScript &script);

/**
 * Extract token balance from covenant token script
 * @param script The covenant token scriptPubKey
 * @return Token balance (8 bytes, big-endian)
 */
int64_t ExtractCovenantBalance(const CScript &script);

/**
 * Check if a script is a covenant token script
 * @param script The scriptPubKey to check
 * @return true if it matches the 91-byte covenant pattern
 */
bool IsCovenantScript(const CScript &script);

/**
 * Validate covenant token balance conservation rules
 * For each unique genesis ID in the transaction:
 * - Sum all input balances with that genesis
 * - Sum all output balances with that genesis
 * - Verify input_sum == output_sum (conservation of balance)
 * 
 * @param tx The transaction to validate
 * @param inputs View of coins being spent
 * @param nHeight Block height (for activation check)
 * @return true if all covenant rules are satisfied, false otherwise
 */
bool CheckCovenantRules(const CTransaction &tx, const CCoinsViewCache &inputs,
                       int nHeight);

/**
 * Activation height for covenant validation
 * Before this height, covenant scripts are accepted but not validated
 * After this height, balance conservation is enforced by consensus
 */
static const int COVENANT_ACTIVATION_HEIGHT = 1134000;

#endif // BITCOIN_CONSENSUS_COVENANT_H

