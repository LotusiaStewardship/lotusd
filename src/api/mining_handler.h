// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_MINING_HANDLER_H
#define BITCOIN_API_MINING_HANDLER_H

#include <api/api_util.h>
#include <httpserver.h>

#include <string>
#include <vector>

namespace util {
class Ref;
}

namespace api {

bool HandleGetMiningInfo(const util::Ref &ctx, HTTPRequest *req,
                         const std::vector<std::string> &parts,
                         const QueryParams &qp);

/** GET /api/v1/mining/estimatefee — wraps the read-only `estimatefee` RPC. */
bool HandleEstimateFee(const util::Ref &ctx, HTTPRequest *req,
                       const std::vector<std::string> &parts,
                       const QueryParams &qp);

/**
 * GET /api/v1/mining/networkhashps[?nblocks=N&height=H]
 *   Wraps `getnetworkhashps`. Defaults match the RPC (120 blocks, tip).
 */
bool HandleGetNetworkHashPS(const util::Ref &ctx, HTTPRequest *req,
                            const std::vector<std::string> &parts,
                            const QueryParams &qp);

/** GET /api/v1/mining/difficulty — wraps the read-only `getdifficulty` RPC. */
bool HandleGetDifficultyRpc(const util::Ref &ctx, HTTPRequest *req,
                            const std::vector<std::string> &parts,
                            const QueryParams &qp);

} // namespace api

#endif // BITCOIN_API_MINING_HANDLER_H
