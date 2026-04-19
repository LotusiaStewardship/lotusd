// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/api_util.h>
#include <rpc/protocol.h>
#include <util/strencodings.h>

#include <algorithm>
#include <sstream>

namespace api {

std::optional<std::string> QueryParams::Get(const std::string &key) const {
    auto it = params.find(key);
    if (it != params.end()) {
        return it->second;
    }
    return std::nullopt;
}

int QueryParams::GetInt(const std::string &key, int defaultVal) const {
    auto v = Get(key);
    if (!v) {
        return defaultVal;
    }
    try {
        return std::stoi(*v);
    } catch (...) {
        return defaultVal;
    }
}

int64_t QueryParams::GetInt64(const std::string &key,
                              int64_t defaultVal) const {
    auto v = Get(key);
    if (!v) {
        return defaultVal;
    }
    try {
        return std::stoll(*v);
    } catch (...) {
        return defaultVal;
    }
}

static std::string UrlDecode(const std::string &str) {
    std::string result;
    result.reserve(str.size());
    for (size_t i = 0; i < str.size(); i++) {
        if (str[i] == '%' && i + 2 < str.size()) {
            int hi = HexDigit(str[i + 1]);
            int lo = HexDigit(str[i + 2]);
            if (hi >= 0 && lo >= 0) {
                result += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        } else if (str[i] == '+') {
            result += ' ';
            continue;
        }
        result += str[i];
    }
    return result;
}

QueryParams ParseQueryString(const std::string &uri) {
    QueryParams qp;
    auto pos = uri.find('?');
    if (pos == std::string::npos) {
        return qp;
    }
    std::string qs = uri.substr(pos + 1);
    std::istringstream stream(qs);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        auto eq = pair.find('=');
        if (eq == std::string::npos) {
            qp.params[UrlDecode(pair)] = "";
        } else {
            qp.params[UrlDecode(pair.substr(0, eq))] =
                UrlDecode(pair.substr(eq + 1));
        }
    }
    return qp;
}

std::vector<std::string> SplitPath(const std::string &path) {
    std::vector<std::string> parts;
    std::string clean = path;
    // Strip query string
    auto qpos = clean.find('?');
    if (qpos != std::string::npos) {
        clean = clean.substr(0, qpos);
    }
    // Strip trailing slash
    while (!clean.empty() && clean.back() == '/') {
        clean.pop_back();
    }
    if (clean.empty()) {
        return parts;
    }
    std::istringstream stream(clean);
    std::string segment;
    while (std::getline(stream, segment, '/')) {
        if (!segment.empty()) {
            parts.push_back(segment);
        }
    }
    return parts;
}

static thread_local CapturedResponse *tl_capture = nullptr;

void StartCapture(CapturedResponse *out) { tl_capture = out; }
void StopCapture() { tl_capture = nullptr; }
bool IsCapturing() { return tl_capture != nullptr; }

void WriteJSON(HTTPRequest *req, int status, const UniValue &body) {
    std::string json = body.write() + "\n";
    if (tl_capture) {
        tl_capture->body = json;
        tl_capture->status = status;
        return;
    }
    req->WriteHeader("Content-Type", "application/json");
    req->WriteHeader("Access-Control-Allow-Origin", "*");
    req->WriteReply(status, json);
}

void WriteSuccess(HTTPRequest *req, const UniValue &data) {
    WriteJSON(req, HTTP_OK, data);
}

void WriteError(HTTPRequest *req, int httpStatus, const std::string &code,
                const std::string &message) {
    UniValue err(UniValue::VOBJ);
    err.pushKV("error", code);
    err.pushKV("message", message);
    err.pushKV("status", httpStatus);
    WriteJSON(req, httpStatus, err);
}

UniValue PaginatedResponse(const UniValue &items, int total, int limit,
                           int offset) {
    UniValue resp(UniValue::VOBJ);
    resp.pushKV("data", items);
    UniValue pag(UniValue::VOBJ);
    pag.pushKV("total", total);
    pag.pushKV("limit", limit);
    pag.pushKV("offset", offset);
    pag.pushKV("has_more", offset + limit < total);
    resp.pushKV("pagination", pag);
    return resp;
}

UniValue CursorPaginatedResponse(const UniValue &items,
                                 const std::string &nextCursor, bool hasMore) {
    UniValue resp(UniValue::VOBJ);
    resp.pushKV("data", items);
    UniValue pag(UniValue::VOBJ);
    if (!nextCursor.empty()) {
        pag.pushKV("next_cursor", nextCursor);
    }
    pag.pushKV("has_more", hasMore);
    resp.pushKV("pagination", pag);
    return resp;
}

bool ParseHashFromHex(const std::string &hex, uint256 &out) {
    if (!IsHex(hex) || hex.size() != 64) {
        return false;
    }
    out.SetHex(hex);
    return true;
}

} // namespace api
