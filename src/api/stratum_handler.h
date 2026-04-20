// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_STRATUM_HANDLER_H
#define BITCOIN_API_STRATUM_HANDLER_H

#include <api/api_util.h>
#include <util/ref.h>

#include <string>
#include <vector>

class HTTPRequest;

namespace api {

bool HandleGetStratumInfo(const util::Ref &ctx, HTTPRequest *req,
                          const std::vector<std::string> &parts,
                          const QueryParams &qp);

bool HandleGetShareChainInfo(const util::Ref &ctx, HTTPRequest *req,
                             const std::vector<std::string> &parts,
                             const QueryParams &qp);

bool HandleGetStratumWorkers(const util::Ref &ctx, HTTPRequest *req,
                             const std::vector<std::string> &parts,
                             const QueryParams &qp);

bool HandleGetScryptChains(const util::Ref &ctx, HTTPRequest *req,
                           const std::vector<std::string> &parts,
                           const QueryParams &qp);

} // namespace api

#endif // BITCOIN_API_STRATUM_HANDLER_H
