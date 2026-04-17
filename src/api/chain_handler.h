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

} // namespace api

#endif // BITCOIN_API_CHAIN_HANDLER_H
