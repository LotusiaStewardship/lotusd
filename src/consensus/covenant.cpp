// Copyright (c) 2025 The Lotus Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <consensus/covenant.h>

#include <coins.h>
#include <primitives/transaction.h>
#include <script/script.h>
#include <script/standard.h>

#include <map>
#include <vector>

bool IsCovenantScript(const CScript &script) {
    // Covenant scripts come in two forms:
    
    // 1. Simple pattern: 91 bytes
    //    <32 bytes> OP_DROP <8 bytes> OP_DROP <20 bytes> OP_DROP OP_DUP OP_HASH160 <20 bytes> OP_EQUALVERIFY OP_CHECKSIG
    if (script.size() == 91) {
        // Quick pattern check for simple covenant
        bool isSimpleCovenant = 
            (script[0] == 0x20 &&      // Push 32 bytes
             script[33] == 0x75 &&     // OP_DROP
             script[34] == 0x08 &&     // Push 8 bytes
             script[43] == 0x75 &&     // OP_DROP
             script[44] == 0x14 &&     // Push 20 bytes
             script[65] == 0x75 &&     // OP_DROP
             script[66] == 0x76 &&     // OP_DUP
             script[67] == 0xa9 &&     // OP_HASH160
             script[68] == 0x14 &&     // Push 20 bytes
             script[89] == 0x88 &&     // OP_EQUALVERIFY
             script[90] == 0xac);      // OP_CHECKSIG
        
        if (isSimpleCovenant) {
            return true;
        }
    }
    
    // 2. Full covenant: Uses introspection opcodes (OP_CAT, OP_OUTPUTBYTECODE, etc.)
    //    These scripts self-validate balance conservation
    //    Must start with genesis ID push and contain covenant opcodes
    
    if (script.size() < 33) {
        return false;
    }
    
    // Check if script starts with 32-byte genesis push
    if (script[0] != 0x20) {
        return false;
    }
    
    // Scan for covenant introspection opcodes
    bool hasIntrospection = false;
    for (size_t i = 33; i < script.size(); i++) {
        uint8_t opcode = script[i];
        // Check for introspection opcodes (0xc0-0xc8)
        if (opcode >= 0xc0 && opcode <= 0xc8) {
            hasIntrospection = true;
            break;
        }
        // Check for OP_CAT (covenant typically uses this)
        if (opcode == 0x7e) {
            hasIntrospection = true;
            break;
        }
    }
    
    // If it starts with genesis and has introspection, it's a covenant
    return hasIntrospection;
}

std::vector<uint8_t> ExtractCovenantGenesis(const CScript &script) {
    if (!IsCovenantScript(script)) {
        return std::vector<uint8_t>();
    }

    // Genesis ID is bytes 1-32 (after the 0x20 push opcode)
    return std::vector<uint8_t>(script.begin() + 1, script.begin() + 33);
}

int64_t ExtractCovenantBalance(const CScript &script) {
    if (!IsCovenantScript(script)) {
        return 0;
    }

    // For simple 91-byte pattern, balance is at fixed position
    if (script.size() == 91 && script[34] == 0x08) {
        // Balance is bytes 35-42 (8 bytes after genesis + OP_DROP + 0x08)
        // Stored as big-endian
        int64_t balance = 0;
        for (int i = 0; i < 8; i++) {
            balance = (balance << 8) | static_cast<uint8_t>(script[35 + i]);
        }
        return balance;
    }
    
    // For complex covenant scripts, balance is enforced by the script itself
    // using introspection opcodes. We don't extract it - the script validates it.
    // Return 0 to indicate "script-enforced" (not consensus-enforced by us)
    return 0;
}

bool CheckCovenantRules(const CTransaction &tx, const CCoinsViewCache &inputs,
                       int nHeight) {
    // Only enforce after activation height
    if (nHeight < COVENANT_ACTIVATION_HEIGHT) {
        return true;
    }

    // Map: genesis_id → (input_sum, output_sum)
    std::map<std::vector<uint8_t>, std::pair<int64_t, int64_t>> tokenBalances;

    // Track which genesis IDs have complex covenants (self-validating)
    std::map<std::vector<uint8_t>, bool> hasComplexCovenant;

    // Sum input balances by genesis ID (only for SIMPLE covenants)
    for (const CTxIn &txin : tx.vin) {
        const Coin &coin = inputs.AccessCoin(txin.prevout);
        if (coin.IsSpent()) {
            continue;
        }

        const CScript &scriptPubKey = coin.GetTxOut().scriptPubKey;
        if (IsCovenantScript(scriptPubKey)) {
            std::vector<uint8_t> genesis = ExtractCovenantGenesis(scriptPubKey);
            int64_t balance = ExtractCovenantBalance(scriptPubKey);

            // If balance is 0, this is a complex covenant (self-validating)
            if (balance == 0 && scriptPubKey.size() != 91) {
                hasComplexCovenant[genesis] = true;
            }

            if (tokenBalances.find(genesis) == tokenBalances.end()) {
                tokenBalances[genesis] = std::make_pair(0, 0);
            }
            tokenBalances[genesis].first += balance;
        }
    }

    // Sum output balances by genesis ID (only for SIMPLE covenants)
    for (const CTxOut &txout : tx.vout) {
        if (IsCovenantScript(txout.scriptPubKey)) {
            std::vector<uint8_t> genesis =
                ExtractCovenantGenesis(txout.scriptPubKey);
            int64_t balance = ExtractCovenantBalance(txout.scriptPubKey);

            // If balance is 0, this is a complex covenant (self-validating)
            if (balance == 0 && txout.scriptPubKey.size() != 91) {
                hasComplexCovenant[genesis] = true;
            }

            if (tokenBalances.find(genesis) == tokenBalances.end()) {
                // Output without corresponding input - this is a genesis
                // (token creation)
                // Allow this, but track it
                tokenBalances[genesis] = std::make_pair(0, 0);
            }
            tokenBalances[genesis].second += balance;
        }
    }

    // Verify balance conservation for each token
    for (const auto &entry : tokenBalances) {
        const std::vector<uint8_t> &genesis = entry.first;
        int64_t inputSum = entry.second.first;
        int64_t outputSum = entry.second.second;

        // If this genesis uses complex covenants, skip consensus validation
        // The script itself enforces balance conservation via introspection opcodes
        if (hasComplexCovenant[genesis]) {
            continue;
        }

        // Special case: If inputSum == 0, this is token genesis (creation)
        // Allow any output amount for genesis
        if (inputSum == 0) {
            continue;
        }

        // For SIMPLE covenant transfers, enforce strict balance conservation
        if (inputSum != outputSum) {
            // Balance not conserved!
            return false;
        }
    }

    return true;
}

