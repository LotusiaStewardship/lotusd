// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_EVENTS_HANDLER_H
#define BITCOIN_API_EVENTS_HANDLER_H

#include <api/api_util.h>
#include <httpserver.h>
#include <validationinterface.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace util {
class Ref;
}

namespace api {

class EventsSubscriber;

void StartEvents();
void StopEvents();

bool HandleGetEvents(const util::Ref &ctx, HTTPRequest *req,
                     const std::vector<std::string> &parts,
                     const QueryParams &qp);

} // namespace api

#endif // BITCOIN_API_EVENTS_HANDLER_H
