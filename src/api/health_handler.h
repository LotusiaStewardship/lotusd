// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_HEALTH_HANDLER_H
#define BITCOIN_API_HEALTH_HANDLER_H

#include <api/api_util.h>
#include <httpserver.h>

#include <string>
#include <vector>

namespace util {
class Ref;
}

namespace api {

// GET /api/v1/health
//
// Lightweight liveness probe for browsers and light wallets. Always returns
// 200 once the chain is loaded, even during initial block download (the
// `ibd` flag is the signal). Returns 503 only when the validation interface
// has not yet produced a tip (i.e. the node has just started).
//
// Response body (200):
//   {
//     "status":          "ok" | "syncing",
//     "height":          int,
//     "best_block_hash": "<hex>",
//     "median_time":     int (unix seconds),
//     "ibd":             bool,
//     "uptime_seconds":  int,
//     "version":         int (CLIENT_VERSION),
//     "subversion":      string (BIP14 user agent)
//   }
bool HandleGetHealth(const util::Ref &ctx, HTTPRequest *req,
                     const std::vector<std::string> &parts,
                     const QueryParams &qp);

} // namespace api

#endif // BITCOIN_API_HEALTH_HANDLER_H
