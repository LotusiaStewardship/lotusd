// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_UTIL_H
#define BITCOIN_API_UTIL_H

#include <httpserver.h>
#include <uint256.h>
#include <univalue.h>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace api {

struct QueryParams {
    std::unordered_map<std::string, std::string> params;

    std::optional<std::string> Get(const std::string &key) const;
    int GetInt(const std::string &key, int defaultVal) const;
    int64_t GetInt64(const std::string &key, int64_t defaultVal) const;
};

QueryParams ParseQueryString(const std::string &uri);

std::vector<std::string> SplitPath(const std::string &path);

// JSON response helpers
void WriteJSON(HTTPRequest *req, int status, const UniValue &body);
void WriteSuccess(HTTPRequest *req, const UniValue &data);
void WriteError(HTTPRequest *req, int httpStatus, const std::string &code,
                const std::string &message);

UniValue PaginatedResponse(const UniValue &items, int total, int limit,
                           int offset);

UniValue CursorPaginatedResponse(const UniValue &items,
                                 const std::string &nextCursor, bool hasMore);

// Hex/binary helpers
bool ParseHashFromHex(const std::string &hex, uint256 &out);

} // namespace api

#endif // BITCOIN_API_UTIL_H
