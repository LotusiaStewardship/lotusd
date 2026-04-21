// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_ADDRESS_HANDLER_H
#define BITCOIN_API_ADDRESS_HANDLER_H

#include <api/api_util.h>
#include <httpserver.h>

#include <string>
#include <vector>

namespace util {
class Ref;
}

namespace api {

bool HandleGetAddress(const util::Ref &ctx, HTTPRequest *req,
                      const std::vector<std::string> &parts,
                      const QueryParams &qp);

bool HandleGetUtxos(const util::Ref &ctx, HTTPRequest *req,
                    const std::vector<std::string> &parts,
                    const QueryParams &qp);

// POST /api/v1/addresses/batch/summary
// POST /api/v1/addresses/batch/utxos
// POST /api/v1/addresses/batch/txs
//
// Body (JSON): {"addresses": ["addr1", "addr2", ...]}
//   - max 100 addresses per call
//   - /txs additionally accepts {"limit": int, "offset": int}, default 25/0,
//     applied per address
//
// Response: {"data": { "<addr>": <single-address payload>, ... }}
//
// Per-address payloads mirror the corresponding singleton endpoint
// (/addresses/<addr>, /addresses/<addr>/utxos, /addresses/<addr>/txs)
// but without pagination envelopes — callers asking for batches usually
// know they want the latest N rows for each address rather than walking
// every page.
bool HandleBatchAddressSummary(const util::Ref &ctx, HTTPRequest *req,
                               const std::vector<std::string> &parts,
                               const QueryParams &qp);

bool HandleBatchAddressUtxos(const util::Ref &ctx, HTTPRequest *req,
                             const std::vector<std::string> &parts,
                             const QueryParams &qp);

bool HandleBatchAddressTxs(const util::Ref &ctx, HTTPRequest *req,
                           const std::vector<std::string> &parts,
                           const QueryParams &qp);

} // namespace api

#endif // BITCOIN_API_ADDRESS_HANDLER_H
