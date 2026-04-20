// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/chain_handler.h>

#include <api/legacy_rpc_handler.h>
#include <blockindex.h>
#include <chain.h>
#include <chainparams.h>
#include <node/context.h>
#include <rpc/blockchain.h>
#include <rpc/protocol.h>
#include <sqlite/block_tree_sqlite.h>
#include <sqlite3.h>
#include <sync.h>
#include <util/ref.h>
#include <validation.h>

namespace api {

bool HandleGetChainInfo(const util::Ref &ctx, HTTPRequest *req,
                        const std::vector<std::string> &,
                        const QueryParams &) {
    UniValue result(UniValue::VOBJ);
    {
        LOCK(cs_main);
        const CBlockIndex *tip = ::ChainActive().Tip();
        if (!tip) {
            WriteError(req, HTTP_SERVICE_UNAVAILABLE, "not_ready",
                       "Chain not yet loaded");
            return true;
        }

        result.pushKV("chain",
                       Params().NetworkIDString());
        result.pushKV("height", tip->nHeight);
        result.pushKV("best_block_hash", tip->GetBlockHash().GetHex());
        result.pushKV("difficulty", GetDifficulty(tip));
        result.pushKV("median_time", int64_t(tip->GetMedianTimePast()));
        result.pushKV("chain_work", tip->nChainWork.GetHex());

        const auto &params = Params().GetConsensus();
        result.pushKV("initial_block_download",
                       ::ChainstateActive().IsInitialBlockDownload());
    }

    WriteSuccess(req, result);
    return true;
}

bool HandleGetChainTip(const util::Ref &ctx, HTTPRequest *req,
                       const std::vector<std::string> &,
                       const QueryParams &) {
    UniValue result(UniValue::VOBJ);
    {
        LOCK(cs_main);
        const CBlockIndex *tip = ::ChainActive().Tip();
        if (!tip) {
            WriteError(req, HTTP_SERVICE_UNAVAILABLE, "not_ready",
                       "Chain not yet loaded");
            return true;
        }

        result.pushKV("hash", tip->GetBlockHash().GetHex());
        result.pushKV("height", tip->nHeight);
        result.pushKV("time", int64_t(tip->GetBlockTime()));
        result.pushKV("n_tx", int64_t(tip->nTx));
        result.pushKV("size", int64_t(tip->nSize));

        if (tip->pprev) {
            result.pushKV("previous_hash",
                           tip->pprev->GetBlockHash().GetHex());
        }
    }

    WriteSuccess(req, result);
    return true;
}

static UniValue BlockIndexToJSON(const CBlockIndex *pindex,
                                 const CBlockIndex *tip) {
    UniValue obj(UniValue::VOBJ);
    obj.pushKV("hash", pindex->GetBlockHash().GetHex());
    obj.pushKV("height", pindex->nHeight);
    obj.pushKV("time", int64_t(pindex->GetBlockTime()));
    obj.pushKV("n_tx", int64_t(pindex->nTx));
    obj.pushKV("size", int64_t(pindex->nSize));
    obj.pushKV("difficulty", GetDifficulty(pindex));
    obj.pushKV("confirmations",
               tip ? tip->nHeight - pindex->nHeight + 1 : 0);
    if (pindex->pprev) {
        obj.pushKV("previous_hash", pindex->pprev->GetBlockHash().GetHex());
    }
    return obj;
}

bool HandleGetBlocks(const util::Ref &ctx, HTTPRequest *req,
                     const std::vector<std::string> &parts,
                     const QueryParams &qp) {
    // GET /api/v1/blocks?limit=N&offset=M  -> list blocks
    // GET /api/v1/blocks/<hash_or_height>   -> single block
    // GET /api/v1/blocks/<hash_or_height>/txs -> block transactions

    if (parts.size() >= 2) {
        // Single block by hash or height
        const std::string &id = parts[1];

        LOCK(cs_main);
        const CBlockIndex *tip = ::ChainActive().Tip();
        const CBlockIndex *pindex = nullptr;

        // If the string is all digits, treat as height; otherwise hash
        bool allDigits = !id.empty() &&
            std::all_of(id.begin(), id.end(), ::isdigit);
        if (allDigits) {
            try {
                int height = std::stoi(id);
                if (height >= 0 && height <= ::ChainActive().Height()) {
                    pindex = ::ChainActive()[height];
                }
            } catch (...) {}
        } else {
            uint256 rawHash;
            if (ParseHashFromHex(id, rawHash)) {
                pindex = LookupBlockIndex(BlockHash(rawHash));
            }
        }

        if (!pindex) {
            WriteError(req, HTTP_NOT_FOUND, "block_not_found",
                       "Block not found: " + id);
            return true;
        }

        if (parts.size() >= 3 && parts[2] == "txs") {
            // Block transactions from SQLite
            int limit = qp.GetInt("limit", 25);
            int offset = qp.GetInt("offset", 0);
            limit = std::max(1, std::min(limit, 500));
            offset = std::max(0, offset);

            auto *btree = dynamic_cast<CBlockTreeSqlite *>(pblocktree.get());
            if (!btree) {
                WriteError(req, HTTP_INTERNAL_SERVER_ERROR, "db_error",
                           "SQLite block tree not available");
                return true;
            }

            CSqliteWrapper &db = btree->GetDb();
            UniValue txArr(UniValue::VARR);
            int total = 0;

            sqlite3_stmt *cnt = db.Prepare(
                "SELECT COUNT(*) FROM transactions WHERE block_height = ?1");
            sqlite3_bind_int(cnt, 1, pindex->nHeight);
            if (sqlite3_step(cnt) == SQLITE_ROW) {
                total = sqlite3_column_int(cnt, 0);
            }
            sqlite3_reset(cnt);

            sqlite3_stmt *stmt = db.Prepare(
                "SELECT txid, block_pos FROM transactions "
                "WHERE block_height = ?1 "
                "ORDER BY block_pos LIMIT ?2 OFFSET ?3");
            sqlite3_bind_int(stmt, 1, pindex->nHeight);
            sqlite3_bind_int(stmt, 2, limit);
            sqlite3_bind_int(stmt, 3, offset);

            while (sqlite3_step(stmt) == SQLITE_ROW) {
                const uint8_t *txid_blob =
                    static_cast<const uint8_t *>(sqlite3_column_blob(stmt, 0));
                int txid_len = sqlite3_column_bytes(stmt, 0);
                int pos = sqlite3_column_int(stmt, 1);

                UniValue tx(UniValue::VOBJ);
                if (txid_blob && txid_len == 32) {
                    uint256 txid;
                    memcpy(txid.begin(), txid_blob, 32);
                    tx.pushKV("txid", txid.GetHex());
                }
                tx.pushKV("block_pos", pos);
                txArr.push_back(tx);
            }
            sqlite3_reset(stmt);

            WriteSuccess(req,
                         PaginatedResponse(txArr, total, limit, offset));
            return true;
        }

        UniValue block = BlockIndexToJSON(pindex, tip);
        WriteSuccess(req, block);
        return true;
    }

    // List blocks (most recent first)
    int limit = qp.GetInt("limit", 10);
    int offset = qp.GetInt("offset", 0);
    limit = std::max(1, std::min(limit, 100));
    offset = std::max(0, offset);

    UniValue blocks(UniValue::VARR);
    int total;
    {
        LOCK(cs_main);
        const CBlockIndex *tip = ::ChainActive().Tip();
        if (!tip) {
            WriteError(req, HTTP_SERVICE_UNAVAILABLE, "not_ready",
                       "Chain not yet loaded");
            return true;
        }
        total = tip->nHeight + 1;

        int startHeight = tip->nHeight - offset;
        for (int i = 0; i < limit && startHeight - i >= 0; i++) {
            const CBlockIndex *pindex = ::ChainActive()[startHeight - i];
            if (pindex) {
                blocks.push_back(BlockIndexToJSON(pindex, tip));
            }
        }
    }

    WriteSuccess(req, PaginatedResponse(blocks, total, limit, offset));
    return true;
}

bool HandleGetBestBlockHash(const util::Ref &ctx, HTTPRequest *req,
                            const std::vector<std::string> &,
                            const QueryParams &) {
    return ProxyReadOnlyRpc(ctx, req, "getbestblockhash",
                             UniValue(UniValue::VARR), "blockhash");
}

bool HandleGetBlockCount(const util::Ref &ctx, HTTPRequest *req,
                        const std::vector<std::string> &,
                        const QueryParams &) {
    return ProxyReadOnlyRpc(ctx, req, "getblockcount",
                             UniValue(UniValue::VARR), "count");
}

bool HandleGetBlockHash(const util::Ref &ctx, HTTPRequest *req,
                       const std::vector<std::string> &parts,
                       const QueryParams &) {
    if (parts.size() < 3) {
        WriteError(req, HTTP_BAD_REQUEST, "missing_height",
                   "Usage: /api/v1/chain/block-hash/<height>");
        return true;
    }
    int height = 0;
    try {
        height = std::stoi(parts[2]);
    } catch (...) {
        WriteError(req, HTTP_BAD_REQUEST, "invalid_height",
                   "Block height must be a non-negative integer");
        return true;
    }
    UniValue params(UniValue::VARR);
    params.push_back(height);
    return ProxyReadOnlyRpc(ctx, req, "getblockhash", params, "blockhash");
}

bool HandleGetBlockHeader(const util::Ref &ctx, HTTPRequest *req,
                          const std::vector<std::string> &parts,
                          const QueryParams &qp) {
    if (parts.size() < 3) {
        WriteError(req, HTTP_BAD_REQUEST, "missing_hash",
                   "Usage: /api/v1/chain/block-header/<hash>");
        return true;
    }
    UniValue params(UniValue::VARR);
    params.push_back(parts[2]);
    auto verboseOpt = qp.Get("verbose");
    bool verbose = !verboseOpt.has_value() || (*verboseOpt != "false" &&
                                                *verboseOpt != "0");
    params.push_back(verbose);
    return ProxyReadOnlyRpc(ctx, req, "getblockheader", params);
}

bool HandleGetChainTips(const util::Ref &ctx, HTTPRequest *req,
                       const std::vector<std::string> &,
                       const QueryParams &) {
    return ProxyReadOnlyRpc(ctx, req, "getchaintips",
                             UniValue(UniValue::VARR));
}

} // namespace api
