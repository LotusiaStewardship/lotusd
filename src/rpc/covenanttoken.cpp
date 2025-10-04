// Copyright (c) 2025 The Lotus Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <rpc/blockchain.h>

#include <amount.h>
#include <chain.h>
#include <chainparams.h>
#include <coins.h>
#include <config.h>
#include <core_io.h>
#include <hash.h>
#include <key_io.h>
#include <node/context.h>
#include <policy/policy.h>
#include <primitives/transaction.h>
#include <rpc/server.h>
#include <rpc/util.h>
#include <script/standard.h>
#include <streams.h>
#include <txdb.h>
#include <txmempool.h>
#include <uint256.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <validation.h>

#include <cstdint>
#include <memory>
#include <vector>

/**
 * Parse covenant token script and extract token data
 * Format: <32_bytes> OP_DROP <8_bytes> OP_DROP <20_bytes> OP_DROP OP_DUP OP_HASH160 <20_bytes> OP_EQUALVERIFY OP_CHECKSIG
 */
struct CovenantTokenData {
    uint256 genesisId;    // 32 bytes - token genesis ID
    int64_t balance;      // 8 bytes - token balance
    uint160 ownerPkh;     // 20 bytes - owner public key hash
    bool valid;

    CovenantTokenData() : valid(false) {}

    static CovenantTokenData ParseScript(const CScript &script) {
        CovenantTokenData data;
        data.valid = false;

        // Check if script matches covenant token pattern
        std::vector<std::vector<uint8_t>> vSolutions;
        TxoutType type = Solver(script, vSolutions);
        
        if (type != TxoutType::COVENANT_TOKEN || script.size() != 91) {
            return data;
        }

        // Extract genesis ID (32 bytes starting at position 1)
        std::vector<uint8_t> genesisBytes(script.begin() + 1, script.begin() + 33);
        data.genesisId = uint256(genesisBytes);

        // Extract balance (8 bytes starting at position 35)
        std::vector<uint8_t> balanceBytes(script.begin() + 35, script.begin() + 43);
        // Convert little-endian bytes to int64_t
        data.balance = 0;
        for (size_t i = 0; i < 8; i++) {
            data.balance |= (int64_t)balanceBytes[i] << (i * 8);
        }

        // Extract owner PKH (20 bytes starting at position 45)
        std::vector<uint8_t> pkhBytes(script.begin() + 45, script.begin() + 65);
        data.ownerPkh = uint160(pkhBytes);

        data.valid = true;
        return data;
    }
};

/**
 * Get token information from a transaction output
 */
static RPCHelpMan gettokeninfo() {
    return RPCHelpMan{
        "gettokeninfo",
        "Decode and return covenant token information from a transaction output.\n",
        {
            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction id"},
            {"n", RPCArg::Type::NUM, RPCArg::Optional::NO, "The output number (vout index)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "genesisid", "The token genesis ID"},
                {RPCResult::Type::NUM, "balance", "The token balance"},
                {RPCResult::Type::STR, "owner", "The owner's Lotus address"},
                {RPCResult::Type::STR_HEX, "ownerpubkeyhash", "The owner's public key hash"},
                {RPCResult::Type::BOOL, "valid", "Whether this is a valid covenant token"},
            }
        },
        RPCExamples{
            HelpExampleCli("gettokeninfo", "\"mytxid\" 1") +
            HelpExampleRpc("gettokeninfo", "\"mytxid\", 1")
        },
        [&](const RPCHelpMan &self, const Config &config,
            const JSONRPCRequest &request) -> UniValue {
            
            LOCK(cs_main);

            TxId txid(ParseHashV(request.params[0], "txid"));
            int n = request.params[1].get_int();
            if (n < 0) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid vout index");
            }

            CTransactionRef tx;
            BlockHash hashBlock;
            if (!GetTransaction(txid, tx, config.GetChainParams().GetConsensus(), hashBlock)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not found");
            }

            if (n >= (int)tx->vout.size()) {
                throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid vout index");
            }

            const CTxOut &txout = tx->vout[n];
            CovenantTokenData tokenData = CovenantTokenData::ParseScript(txout.scriptPubKey);

            UniValue result(UniValue::VOBJ);
            result.pushKV("valid", tokenData.valid);

            if (tokenData.valid) {
                result.pushKV("genesisid", tokenData.genesisId.GetHex());
                result.pushKV("balance", tokenData.balance);
                result.pushKV("ownerpubkeyhash", tokenData.ownerPkh.GetHex());
                
                // Convert owner PKH to address
                CTxDestination dest = PKHash(tokenData.ownerPkh);
                std::string address = EncodeDestination(dest, config.GetChainParams());
                result.pushKV("owner", address);
            }

            return result;
        },
    };
}

/**
 * List all covenant tokens held by an address
 */
static RPCHelpMan listtokensbyaddress() {
    return RPCHelpMan{
        "listtokensbyaddress",
        "List all covenant tokens owned by a specific Lotus address.\n"
        "This scans the UTXO set for covenant token outputs belonging to the address.\n",
        {
            {"address", RPCArg::Type::STR, RPCArg::Optional::NO, "The Lotus address to query"},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "Array of token holdings",
            {
                {RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::STR_HEX, "txid", "The transaction ID"},
                    {RPCResult::Type::NUM, "vout", "The output index"},
                    {RPCResult::Type::STR_HEX, "genesisid", "The token genesis ID"},
                    {RPCResult::Type::NUM, "balance", "The token balance"},
                    {RPCResult::Type::NUM, "confirmations", "Number of confirmations"},
                }},
            }
        },
        RPCExamples{
            HelpExampleCli("listtokensbyaddress", "\"lotusaddress\"") +
            HelpExampleRpc("listtokensbyaddress", "\"lotusaddress\"")
        },
        [&](const RPCHelpMan &self, const Config &config,
            const JSONRPCRequest &request) -> UniValue {
            
            LOCK(cs_main);

            CTxDestination dest = DecodeDestination(request.params[0].get_str(), config.GetChainParams());
            if (!IsValidDestination(dest)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid Lotus address");
            }

            const PKHash *pkhash = boost::get<PKHash>(&dest);
            if (!pkhash) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Address must be P2PKH");
            }
            uint160 targetPkh = static_cast<uint160>(*pkhash);

            UniValue results(UniValue::VARR);
            CCoinsViewCache &coins = ::ChainstateActive().CoinsTip();
            
            // Note: This is a simplified implementation that would need optimization
            // for production. A proper implementation would use an index.
            std::vector<COutPoint> vOutpoints;
            
            // In a real implementation, you'd iterate through the UTXO set
            // For now, return a message explaining this needs the full node UTXO scan
            UniValue info(UniValue::VOBJ);
            info.pushKV("notice", "Full UTXO scan not implemented in this RPC. Use scantxoutset with descriptor for full functionality.");
            info.pushKV("address", request.params[0].get_str());
            results.push_back(info);

            return results;
        },
    };
}

/**
 * Get genesis token creation data
 */
static RPCHelpMan gettokengenesis() {
    return RPCHelpMan{
        "gettokengenesis",
        "Get the genesis (creation) transaction information for a covenant token.\n",
        {
            {"genesisid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The token genesis ID (32-byte hash)"},
        },
        RPCResult{
            RPCResult::Type::OBJ, "", "",
            {
                {RPCResult::Type::STR_HEX, "genesisid", "The token genesis ID"},
                {RPCResult::Type::STR_HEX, "txid", "The genesis transaction ID"},
                {RPCResult::Type::NUM, "vout", "The genesis output index"},
                {RPCResult::Type::NUM, "initialbalance", "The initial token balance"},
                {RPCResult::Type::STR, "creator", "The creator's address"},
                {RPCResult::Type::NUM, "blockheight", "Block height of genesis"},
                {RPCResult::Type::STR_HEX, "blockhash", "Block hash containing genesis"},
            }
        },
        RPCExamples{
            HelpExampleCli("gettokengenesis", "\"43e3ea60862c0da6a81b961a2af9b8f0040a394a16869ad718a8f14cb94969f5\"") +
            HelpExampleRpc("gettokengenesis", "\"43e3ea60862c0da6a81b961a2af9b8f0040a394a16869ad718a8f14cb94969f5\"")
        },
        [&](const RPCHelpMan &self, const Config &config,
            const JSONRPCRequest &request) -> UniValue {
            
            LOCK(cs_main);

            uint256 genesisId = ParseHashV(request.params[0], "genesisid");

            // Search for the genesis transaction
            // The genesis ID is typically the hash of the genesis transaction + output
            // This is a simplified implementation - production would need an index
            
            UniValue result(UniValue::VOBJ);
            result.pushKV("genesisid", genesisId.GetHex());
            result.pushKV("notice", "Genesis transaction lookup requires a token index. This is a placeholder implementation.");
            
            return result;
        },
    };
}

/**
 * Scan for covenant tokens in a transaction
 */
static RPCHelpMan scantokens() {
    return RPCHelpMan{
        "scantokens",
        "Scan a transaction for covenant token outputs.\n",
        {
            {"txid", RPCArg::Type::STR_HEX, RPCArg::Optional::NO, "The transaction ID to scan"},
        },
        RPCResult{
            RPCResult::Type::ARR, "", "Array of token outputs found",
            {
                {RPCResult::Type::OBJ, "", "",
                {
                    {RPCResult::Type::NUM, "vout", "The output index"},
                    {RPCResult::Type::STR_HEX, "genesisid", "The token genesis ID"},
                    {RPCResult::Type::NUM, "balance", "The token balance"},
                    {RPCResult::Type::STR, "owner", "The owner's address"},
                }},
            }
        },
        RPCExamples{
            HelpExampleCli("scantokens", "\"mytxid\"") +
            HelpExampleRpc("scantokens", "\"mytxid\"")
        },
        [&](const RPCHelpMan &self, const Config &config,
            const JSONRPCRequest &request) -> UniValue {
            
            LOCK(cs_main);

            TxId txid(ParseHashV(request.params[0], "txid"));

            CTransactionRef tx;
            BlockHash hashBlock;
            if (!GetTransaction(txid, tx, config.GetChainParams().GetConsensus(), hashBlock)) {
                throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Transaction not found");
            }

            UniValue results(UniValue::VARR);

            for (size_t i = 0; i < tx->vout.size(); i++) {
                const CTxOut &txout = tx->vout[i];
                CovenantTokenData tokenData = CovenantTokenData::ParseScript(txout.scriptPubKey);

                if (tokenData.valid) {
                    UniValue token(UniValue::VOBJ);
                    token.pushKV("vout", (int)i);
                    token.pushKV("genesisid", tokenData.genesisId.GetHex());
                    token.pushKV("balance", tokenData.balance);
                    token.pushKV("ownerpubkeyhash", tokenData.ownerPkh.GetHex());
                    
                    CTxDestination dest = PKHash(tokenData.ownerPkh);
                    std::string address = EncodeDestination(dest, config.GetChainParams());
                    token.pushKV("owner", address);

                    results.push_back(token);
                }
            }

            return results;
        },
    };
}

void RegisterCovenantTokenRPCCommands(CRPCTable &t) {
    // clang-format off
    static const CRPCCommand commands[] = {
        //  category              actor (function)
        //  --------------------  ----------------------
        { "covenanttoken",        gettokeninfo,           },
        { "covenanttoken",        listtokensbyaddress,    },
        { "covenanttoken",        gettokengenesis,        },
        { "covenanttoken",        scantokens,             },
    };
    // clang-format on
    for (const auto &c : commands) {
        t.appendCommand(c.name, &c);
    }
}

