// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_TX_HANDLER_H
#define BITCOIN_API_TX_HANDLER_H

#include <api/api_util.h>
#include <httpserver.h>

#include <string>
#include <vector>

namespace util {
class Ref;
}

namespace api {

bool HandleGetTx(const util::Ref &ctx, HTTPRequest *req,
                 const std::vector<std::string> &parts,
                 const QueryParams &qp);

bool HandleGetMempool(const util::Ref &ctx, HTTPRequest *req,
                      const std::vector<std::string> &parts,
                      const QueryParams &qp);

bool HandleSendTx(const util::Ref &ctx, HTTPRequest *req,
                  const std::vector<std::string> &parts,
                  const QueryParams &qp);

/**
 * POST /api/v1/txs/broadcast — dispatches to `sendrawtransaction`.
 *
 * Accepts either:
 *   - a JSON body: `{ "hex": "<rawhex>", "maxfeerate": <optional float> }`
 *   - or the raw hex string as the request body.
 *
 * Returns the dispatcher's result wrapped as `{ "txid": "<hex>" }` so the
 * shape stays stable even if the underlying RPC ever changes.
 *
 * Unauthenticated: the network's consensus + standardness rules are the
 * gatekeeper for what can be relayed.
 */
bool HandleBroadcastTx(const util::Ref &ctx, HTTPRequest *req,
                       const std::vector<std::string> &parts,
                       const QueryParams &qp);

bool HandleDecodeTx(const util::Ref &ctx, HTTPRequest *req,
                    const std::vector<std::string> &parts,
                    const QueryParams &qp);

bool HandleGetMempoolHistory(const util::Ref &ctx, HTTPRequest *req,
                              const std::vector<std::string> &parts,
                              const QueryParams &qp);

// ---------------------------------------------------------------------------
// Thin REST aliases for read-only legacy RPCs.
// ---------------------------------------------------------------------------

/**
 * GET /api/v1/chain/txout/<txid>/<vout>[?include_mempool=true] → `gettxout`
 *
 * Distinct from `/api/v1/utxos/...` which is a SQLite-index lookup; this one
 * hits the live coin set, so it sees mempool spends and is authoritative for
 * "is this output currently unspent".
 */
bool HandleGetTxOut(const util::Ref &ctx, HTTPRequest *req,
                   const std::vector<std::string> &parts,
                   const QueryParams &qp);

/** GET /api/v1/mempool/raw[?verbose=true|false] → `getrawmempool` */
bool HandleGetRawMempool(const util::Ref &ctx, HTTPRequest *req,
                        const std::vector<std::string> &parts,
                        const QueryParams &qp);

/** GET /api/v1/mempool/info → `getmempoolinfo` */
bool HandleGetMempoolInfo(const util::Ref &ctx, HTTPRequest *req,
                         const std::vector<std::string> &parts,
                         const QueryParams &qp);

/** POST /api/v1/scripts/decode { "hex": "..." } → `decodescript` */
bool HandleDecodeScript(const util::Ref &ctx, HTTPRequest *req,
                       const std::vector<std::string> &parts,
                       const QueryParams &qp);

} // namespace api

#endif // BITCOIN_API_TX_HANDLER_H
