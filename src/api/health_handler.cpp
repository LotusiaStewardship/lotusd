// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/health_handler.h>

#include <chain.h>
#include <clientversion.h>
#include <config.h>
#include <net.h>
#include <rpc/protocol.h>
#include <sync.h>
#include <util/system.h>
#include <util/time.h>
#include <validation.h>

namespace api {

bool HandleGetHealth(const util::Ref & /*ctx*/, HTTPRequest *req,
                     const std::vector<std::string> & /*parts*/,
                     const QueryParams & /*qp*/) {
    UniValue result(UniValue::VOBJ);

    bool ibd = false;
    const CBlockIndex *tip = nullptr;
    {
        LOCK(cs_main);
        tip = ::ChainActive().Tip();
        ibd = ::ChainstateActive().IsInitialBlockDownload();
    }

    if (!tip) {
        WriteError(req, HTTP_SERVICE_UNAVAILABLE, "not_ready",
                   "Chain not yet loaded");
        return true;
    }

    result.pushKV("status", ibd ? "syncing" : "ok");
    result.pushKV("height", tip->nHeight);
    result.pushKV("best_block_hash", tip->GetBlockHash().GetHex());
    result.pushKV("median_time", int64_t(tip->GetMedianTimePast()));
    result.pushKV("ibd", ibd);
    result.pushKV("uptime_seconds", GetTime() - GetStartupTime());
    result.pushKV("version", CLIENT_VERSION);
    result.pushKV("subversion", userAgent(GetConfig()));

    WriteSuccess(req, result);
    return true;
}

} // namespace api
