// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_CHAIN_HANDLER_H
#define BITCOIN_API_CHAIN_HANDLER_H

#include <api/api_util.h>
#include <httpserver.h>

#include <string>
#include <vector>

namespace util {
class Ref;
}

namespace api {

bool HandleGetChainInfo(const util::Ref &ctx, HTTPRequest *req,
                        const std::vector<std::string> &parts,
                        const QueryParams &qp);

bool HandleGetChainTip(const util::Ref &ctx, HTTPRequest *req,
                       const std::vector<std::string> &parts,
                       const QueryParams &qp);

bool HandleGetBlocks(const util::Ref &ctx, HTTPRequest *req,
                     const std::vector<std::string> &parts,
                     const QueryParams &qp);

// ---------------------------------------------------------------------------
// Thin REST aliases for read-only legacy RPCs. Each handler proxies a
// single, hard-coded RPC method so wallets can fetch chain primitives
// without holding `rpcuser:rpcpassword`.
// ---------------------------------------------------------------------------

/** GET /api/v1/chain/best-block-hash → `getbestblockhash` */
bool HandleGetBestBlockHash(const util::Ref &ctx, HTTPRequest *req,
                            const std::vector<std::string> &parts,
                            const QueryParams &qp);

/** GET /api/v1/chain/block-count → `getblockcount` */
bool HandleGetBlockCount(const util::Ref &ctx, HTTPRequest *req,
                        const std::vector<std::string> &parts,
                        const QueryParams &qp);

/** GET /api/v1/chain/block-hash/<height> → `getblockhash <height>` */
bool HandleGetBlockHash(const util::Ref &ctx, HTTPRequest *req,
                       const std::vector<std::string> &parts,
                       const QueryParams &qp);

/** GET /api/v1/chain/block-header/<hash>[?verbose=true|false] → `getblockheader` */
bool HandleGetBlockHeader(const util::Ref &ctx, HTTPRequest *req,
                          const std::vector<std::string> &parts,
                          const QueryParams &qp);

/** GET /api/v1/chain/tips → `getchaintips` */
bool HandleGetChainTips(const util::Ref &ctx, HTTPRequest *req,
                       const std::vector<std::string> &parts,
                       const QueryParams &qp);

} // namespace api

#endif // BITCOIN_API_CHAIN_HANDLER_H
