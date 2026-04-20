// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_DASHBOARD_HANDLER_H
#define BITCOIN_API_DASHBOARD_HANDLER_H

#include <api/api_util.h>
#include <httpserver.h>

#include <string>
#include <vector>

namespace util {
class Ref;
}

namespace api {

bool HandleGetDashboard(const util::Ref &ctx, HTTPRequest *req,
                        const std::vector<std::string> &parts,
                        const QueryParams &qp);

// Serves the embedded SVG lotus favicon used by the dashboard. Wired to
// both /favicon.svg and /favicon.ico so legacy clients that hardcode the
// .ico path still get a usable icon.
bool HandleFavicon(HTTPRequest *req);

} // namespace api

#endif // BITCOIN_API_DASHBOARD_HANDLER_H
