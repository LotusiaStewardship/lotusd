// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_NETWORK_HANDLER_H
#define BITCOIN_API_NETWORK_HANDLER_H

#include <api/api_util.h>
#include <httpserver.h>

#include <string>
#include <vector>

namespace util {
class Ref;
}

namespace api {

bool HandleGetNetworkInfo(const util::Ref &ctx, HTTPRequest *req,
                          const std::vector<std::string> &parts,
                          const QueryParams &qp);

bool HandleGetPeers(const util::Ref &ctx, HTTPRequest *req,
                    const std::vector<std::string> &parts,
                    const QueryParams &qp);

bool HandleGetNodeInfo(const util::Ref &ctx, HTTPRequest *req,
                       const std::vector<std::string> &parts,
                       const QueryParams &qp);

bool HandleGetNetworkNodes(const util::Ref &ctx, HTTPRequest *req,
                            const std::vector<std::string> &parts,
                            const QueryParams &qp);

} // namespace api

#endif // BITCOIN_API_NETWORK_HANDLER_H
