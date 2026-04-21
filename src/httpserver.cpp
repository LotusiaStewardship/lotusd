// Copyright (c) 2015-2016 The Bitcoin Core developers
// Copyright (c) 2018-2019 The Bitcoin developers
// Copyright (c) 2026 Lotusia — cpp-httplib rewrite
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <httpserver.h>

#include <chainparamsbase.h>
#include <config.h>
#include <logging.h>
#include <netbase.h>
#include <node/ui_interface.h>
#include <rpc/protocol.h>
#include <shutdown.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <util/threadnames.h>
#include <util/translation.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnarrowing"
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <httplib/httplib.h>
#pragma GCC diagnostic pop

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

// ─── Internal types ─────────────────────────────────────────────────────────────

struct HTTPPathHandler {
    HTTPPathHandler(std::string _prefix, bool _exactMatch,
                    HTTPRequestHandler _handler)
        : prefix(std::move(_prefix)), exactMatch(_exactMatch),
          handler(std::move(_handler)) {}
    std::string prefix;
    bool exactMatch;
    HTTPRequestHandler handler;
};

struct HTTPRequest::Impl {
    // ── Live-request fields (cpp-httplib backed) ────────────────────────
    // Pointers (not references) so we can also represent an in-memory
    // request that has no underlying httplib::Request / Response.
    const httplib::Request *req{nullptr};
    httplib::Response *res{nullptr};
    std::unordered_map<std::string, std::string> extraHeaders;
    bool bodyConsumed{false};

    // ── In-memory mode ──────────────────────────────────────────────────
    // When inMemory == true, all I/O methods on HTTPRequest read/write
    // the buffers below instead of touching cpp-httplib. This is what the
    // NNG RestCall tunnel and the cache background-refresh thread use to
    // dispatch handlers without a real client connection.
    bool inMemory{false};
    HTTPRequest::RequestMethod imMethod{HTTPRequest::GET};
    std::string imUri;
    std::string imBody;
    std::string imBodyContentType;
    int imStatus{0};
    std::string imResponseBody;
    bool imReplySent{false};

    Impl() = default;
    Impl(const httplib::Request &r, httplib::Response &s)
        : req(&r), res(&s) {}
};

// ─── Module state ───────────────────────────────────────────────────────────────

static std::unique_ptr<httplib::Server> g_httpServer;
static std::thread g_thread_http;
static std::vector<HTTPPathHandler> pathHandlers;
static std::vector<CSubNet> rpc_allow_subnets;
static Config *g_config = nullptr;

// Bind endpoints resolved at init time
static std::vector<std::pair<std::string, int>> g_endpoints;

// ─── ACL ────────────────────────────────────────────────────────────────────────

static bool ClientAllowed(const CNetAddr &netaddr) {
    if (!netaddr.IsValid()) {
        return false;
    }
    for (const CSubNet &subnet : rpc_allow_subnets) {
        if (subnet.Match(netaddr)) {
            return true;
        }
    }
    return false;
}

static bool InitHTTPAllowList() {
    rpc_allow_subnets.clear();
    CNetAddr localv4;
    CNetAddr localv6;
    LookupHost("127.0.0.1", localv4, false);
    LookupHost("::1", localv6, false);
    rpc_allow_subnets.push_back(CSubNet(localv4, 8));
    rpc_allow_subnets.push_back(CSubNet(localv6));
    for (const std::string &strAllow : gArgs.GetArgs("-rpcallowip")) {
        CSubNet subnet;
        LookupSubNet(strAllow, subnet);
        if (!subnet.IsValid()) {
            uiInterface.ThreadSafeMessageBox(
                strprintf(
                    Untranslated("Invalid -rpcallowip subnet specification: "
                                 "%s. Valid are a single IP (e.g. 1.2.3.4), a "
                                 "network/netmask (e.g. 1.2.3.4/255.255.255.0) "
                                 "or a network/CIDR (e.g. 1.2.3.4/24)."),
                    strAllow),
                "", CClientUIInterface::MSG_ERROR);
            return false;
        }
        rpc_allow_subnets.push_back(subnet);
    }
    std::string strAllowed;
    for (const CSubNet &subnet : rpc_allow_subnets) {
        strAllowed += subnet.ToString() + " ";
    }
    LogPrint(BCLog::HTTP, "Allowing HTTP connections from: %s\n", strAllowed);
    return true;
}

// ─── Helpers ────────────────────────────────────────────────────────────────────

std::string RequestMethodString(HTTPRequest::RequestMethod m) {
    switch (m) {
        case HTTPRequest::GET:
            return "GET";
        case HTTPRequest::POST:
            return "POST";
        case HTTPRequest::HEAD:
            return "HEAD";
        case HTTPRequest::PUT:
            return "PUT";
        case HTTPRequest::OPTIONS:
            return "OPTIONS";
        default:
            return "unknown";
    }
}

static HTTPRequest::RequestMethod MethodFromString(const std::string &m) {
    if (m == "GET") return HTTPRequest::GET;
    if (m == "POST") return HTTPRequest::POST;
    if (m == "HEAD") return HTTPRequest::HEAD;
    if (m == "PUT") return HTTPRequest::PUT;
    if (m == "OPTIONS") return HTTPRequest::OPTIONS;
    return HTTPRequest::UNKNOWN;
}

// ─── Generic request dispatcher ─────────────────────────────────────────────────

static void DispatchHTTPRequest(const httplib::Request &httplibReq,
                                httplib::Response &httplibRes) {
    auto impl = std::make_unique<HTTPRequest::Impl>(httplibReq, httplibRes);
    auto hreq = std::make_unique<HTTPRequest>(std::move(impl));

    if (!ClientAllowed(hreq->GetPeer())) {
        LogPrint(BCLog::HTTP,
                 "HTTP request from %s rejected: Client network is not allowed "
                 "RPC access\n",
                 hreq->GetPeer().ToString());
        hreq->WriteReply(HTTP_FORBIDDEN);
        return;
    }

    if (hreq->GetRequestMethod() == HTTPRequest::UNKNOWN) {
        LogPrint(BCLog::HTTP,
                 "HTTP request from %s rejected: Unknown HTTP request method\n",
                 hreq->GetPeer().ToString());
        hreq->WriteReply(HTTP_BAD_METHOD);
        return;
    }

    LogPrint(BCLog::HTTP, "Received a %s request for %s from %s\n",
             RequestMethodString(hreq->GetRequestMethod()),
             SanitizeString(hreq->GetURI(), SAFE_CHARS_URI).substr(0, 100),
             hreq->GetPeer().ToString());

    std::string strURI = hreq->GetURI();
    std::string path;
    const HTTPPathHandler *matched = nullptr;

    for (const auto &ph : pathHandlers) {
        bool hit = false;
        if (ph.exactMatch) {
            hit = (strURI == ph.prefix);
        } else {
            hit = (strURI.substr(0, ph.prefix.size()) == ph.prefix);
        }
        if (hit) {
            path = strURI.substr(ph.prefix.size());
            matched = &ph;
            break;
        }
    }

    if (matched) {
        // Any exception escaping a handler would otherwise destroy the
        // HTTPRequest with replySent=false, producing a noisy
        // "~HTTPRequest: Unhandled request" log line and a generic
        // cpp-httplib 500 response. Catch here and reply cleanly.
        try {
            matched->handler(*g_config, hreq.get(), path);
        } catch (const std::exception &e) {
            LogPrintf("HTTP handler exception (%s): %s\n",
                      SanitizeString(strURI, SAFE_CHARS_URI).substr(0, 100),
                      e.what());
            try {
                hreq->WriteReply(HTTP_INTERNAL_SERVER_ERROR,
                                 std::string("Internal server error: ") +
                                     e.what());
            } catch (...) {
            }
        } catch (...) {
            LogPrintf("HTTP handler unknown exception (%s)\n",
                      SanitizeString(strURI, SAFE_CHARS_URI).substr(0, 100));
            try {
                hreq->WriteReply(HTTP_INTERNAL_SERVER_ERROR,
                                 "Internal server error");
            } catch (...) {
            }
        }
    } else {
        hreq->WriteReply(HTTP_NOT_FOUND);
    }
}

// ─── Bind addresses ─────────────────────────────────────────────────────────────

static bool ResolveBindAddresses() {
    int http_port = gArgs.GetArg("-rpcport", BaseParams().RPCPort());
    g_endpoints.clear();

    if (!(gArgs.IsArgSet("-rpcallowip") && gArgs.IsArgSet("-rpcbind"))) {
        g_endpoints.emplace_back("::1", http_port);
        g_endpoints.emplace_back("127.0.0.1", http_port);
        if (gArgs.IsArgSet("-rpcallowip")) {
            LogPrintf("WARNING: option -rpcallowip was specified without "
                      "-rpcbind; this doesn't usually make sense\n");
        }
        if (gArgs.IsArgSet("-rpcbind")) {
            LogPrintf("WARNING: option -rpcbind was ignored because "
                      "-rpcallowip was not specified, refusing to allow "
                      "everyone to connect\n");
        }
    } else if (gArgs.IsArgSet("-rpcbind")) {
        for (const std::string &strRPCBind : gArgs.GetArgs("-rpcbind")) {
            int port = http_port;
            std::string host;
            SplitHostPort(strRPCBind, port, host);
            g_endpoints.emplace_back(host, port);
        }
    }

    return !g_endpoints.empty();
}

// ─── Public API ─────────────────────────────────────────────────────────────────

bool InitHTTPServer(Config &config) {
    if (!InitHTTPAllowList()) {
        return false;
    }

    if (!ResolveBindAddresses()) {
        LogPrintf("Unable to resolve any endpoint for RPC server\n");
        return false;
    }

    g_config = &config;

    g_httpServer = std::make_unique<httplib::Server>();

    int timeout =
        gArgs.GetArg("-rpcservertimeout", DEFAULT_HTTP_SERVER_TIMEOUT);
    g_httpServer->set_read_timeout(timeout);
    g_httpServer->set_write_timeout(timeout);
    g_httpServer->set_idle_interval(timeout);

    int rpcThreads =
        std::max((long)gArgs.GetArg("-rpcthreads", DEFAULT_HTTP_THREADS), 1L);

    g_httpServer->new_task_queue =
        [rpcThreads] { return new httplib::ThreadPool(rpcThreads); };

    g_httpServer->set_pre_routing_handler(
        [](const httplib::Request &req, httplib::Response &res) {
            (void)req;
            (void)res;
            if (ShutdownRequested()) {
                res.status = 503;
                res.set_content("Server shutting down", "text/plain");
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });

    // Catch-all handler: dispatch through our pathHandlers routing table
    g_httpServer->Get(".*", DispatchHTTPRequest);
    g_httpServer->Post(".*", DispatchHTTPRequest);
    g_httpServer->Put(".*", DispatchHTTPRequest);
    g_httpServer->Options(".*", DispatchHTTPRequest);

    LogPrintf("🌐 HTTP: %d worker threads\n", rpcThreads);
    return true;
}

bool UpdateHTTPServerLogging(bool) {
    return true;
}

void StartHTTPServer() {
    LogPrint(BCLog::HTTP, "Starting HTTP server\n");

    g_thread_http = std::thread([] {
        util::ThreadRename("http");

        for (const auto &ep : g_endpoints) {
            LogPrint(BCLog::HTTP, "Binding RPC on address %s port %i\n",
                     ep.first, ep.second);
        }

        // Bind on the first endpoint; cpp-httplib supports one listen call
        const auto &primary = g_endpoints.front();

        CNetAddr addr;
        if (primary.first.empty() ||
            (LookupHost(primary.first, addr, false) && addr.IsBindAny())) {
            // RPC bound to all interfaces — ensure firewall is configured
        }

        const char *host =
            primary.first.empty() ? "0.0.0.0" : primary.first.c_str();

        if (!g_httpServer->listen(host, primary.second)) {
            LogPrintf("HTTP: failed to bind on %s:%d\n", host, primary.second);
        }
    });
}

void InterruptHTTPServer() {
    LogPrint(BCLog::HTTP, "Interrupting HTTP server\n");
}

void StopHTTPServer() {
    LogPrint(BCLog::HTTP, "Stopping HTTP server\n");
    if (g_httpServer) {
        g_httpServer->stop();
    }
    if (g_thread_http.joinable()) {
        g_thread_http.join();
    }
    pathHandlers.clear();
    g_httpServer.reset();
    g_config = nullptr;
    LogPrint(BCLog::HTTP, "Stopped HTTP server\n");
}

// ─── HTTPRequest implementation ─────────────────────────────────────────────────

HTTPRequest::HTTPRequest(std::unique_ptr<Impl> impl)
    : m_impl(std::move(impl)) {}

HTTPRequest::HTTPRequest() : m_impl(nullptr), replySent(true) {}

HTTPRequest::~HTTPRequest() {
    // In-memory requests are explicitly OK to drop without a reply — the
    // caller reads the captured buffer via CapturedBody(). Only the live,
    // socket-backed path needs the "unhandled request" guard.
    if (m_impl && !m_impl->inMemory && !replySent) {
        LogPrintf("%s: Unhandled request\n", __func__);
        WriteReply(HTTP_INTERNAL_SERVER_ERROR, "Unhandled request");
    }
}

// ─── In-memory factory and accessors ────────────────────────────────────

std::unique_ptr<HTTPRequest>
HTTPRequest::MakeInMemory(RequestMethod method, const std::string &uri,
                          const std::string &body,
                          const std::string &contentType) {
    auto impl = std::make_unique<Impl>();
    impl->inMemory = true;
    impl->imMethod = method;
    impl->imUri = uri;
    impl->imBody = body;
    impl->imBodyContentType = contentType;
    return std::make_unique<HTTPRequest>(std::move(impl));
}

bool HTTPRequest::IsInMemory() const {
    return m_impl && m_impl->inMemory;
}

int HTTPRequest::CapturedStatus() const {
    return m_impl ? m_impl->imStatus : 0;
}

const std::string &HTTPRequest::CapturedBody() const {
    static const std::string empty;
    return m_impl ? m_impl->imResponseBody : empty;
}

std::string HTTPRequest::CapturedContentType() const {
    if (!m_impl) return "";
    auto it = m_impl->extraHeaders.find("Content-Type");
    if (it != m_impl->extraHeaders.end()) {
        return it->second;
    }
    return "text/plain";
}

bool HTTPRequest::ReplySent() const {
    return m_impl ? (m_impl->inMemory ? m_impl->imReplySent : replySent)
                  : replySent;
}

// ─── Standard HTTPRequest accessors ─────────────────────────────────────

std::pair<bool, std::string>
HTTPRequest::GetHeader(const std::string &hdr) const {
    if (m_impl && m_impl->inMemory) {
        if (hdr == "Content-Type" && !m_impl->imBodyContentType.empty()) {
            return {true, m_impl->imBodyContentType};
        }
        return {false, ""};
    }
    if (!m_impl || !m_impl->req) return {false, ""};
    auto it = m_impl->req->headers.find(hdr);
    if (it != m_impl->req->headers.end()) {
        return {true, it->second};
    }
    return {false, ""};
}

std::string HTTPRequest::ReadBody() {
    if (!m_impl || m_impl->bodyConsumed) {
        return "";
    }
    m_impl->bodyConsumed = true;
    if (m_impl->inMemory) {
        return m_impl->imBody;
    }
    if (!m_impl->req) return "";
    return m_impl->req->body;
}

void HTTPRequest::WriteHeader(const std::string &hdr,
                              const std::string &value) {
    if (!m_impl) return;
    m_impl->extraHeaders[hdr] = value;
}

void HTTPRequest::WriteReply(int nStatus, const std::string &strReply) {
    if (!m_impl) { replySent = true; return; }
    if (m_impl->inMemory) {
        // Multiple WriteReply calls on the same in-memory request are
        // a handler bug — quietly keep the first reply, like the live
        // path's assertion would in debug builds, but don't crash a
        // long-lived NNG worker.
        if (m_impl->imReplySent) return;
        m_impl->imStatus = nStatus;
        m_impl->imResponseBody = strReply;
        m_impl->imReplySent = true;
        replySent = true;
        return;
    }
    if (!m_impl->res) { replySent = true; return; }
    assert(!replySent);
    if (ShutdownRequested()) {
        m_impl->extraHeaders["Connection"] = "close";
    }
    for (const auto &kv : m_impl->extraHeaders) {
        m_impl->res->set_header(kv.first, kv.second);
    }
    m_impl->res->status = nStatus;
    if (!strReply.empty()) {
        auto contentType = m_impl->extraHeaders.find("Content-Type");
        std::string ct = (contentType != m_impl->extraHeaders.end())
                             ? contentType->second
                             : "text/plain";
        m_impl->res->set_content(strReply, ct);
    }
    replySent = true;
}

CService HTTPRequest::GetPeer() const {
    if (!m_impl || m_impl->inMemory || !m_impl->req) {
        // In-memory requests have no peer — return a zero CService so
        // ACL checks treat them as "not from anywhere". The NNG layer
        // bypasses the HTTP allowlist entirely so this is harmless.
        return CService();
    }
    const auto &remote = m_impl->req->remote_addr;
    int port = m_impl->req->remote_port;
    CService peer;
    if (!remote.empty()) {
        peer = LookupNumeric(remote, port);
    }
    return peer;
}

std::string HTTPRequest::GetURI() const {
    if (m_impl && m_impl->inMemory) {
        return m_impl->imUri;
    }
    if (!m_impl || !m_impl->req) return "";
    const auto &path = m_impl->req->path;
    const auto &params = m_impl->req->params;
    if (params.empty()) {
        return path;
    }
    std::string uri = path + "?";
    bool first = true;
    for (const auto &p : params) {
        if (!first) uri += "&";
        uri += p.first + "=" + p.second;
        first = false;
    }
    return uri;
}

HTTPRequest::RequestMethod HTTPRequest::GetRequestMethod() const {
    if (m_impl && m_impl->inMemory) {
        return m_impl->imMethod;
    }
    if (!m_impl || !m_impl->req) return UNKNOWN;
    return MethodFromString(m_impl->req->method);
}

// ─── Handler registration ───────────────────────────────────────────────────────

void RegisterHTTPHandler(const std::string &prefix, bool exactMatch,
                         const HTTPRequestHandler &handler) {
    LogPrint(BCLog::HTTP, "Registering HTTP handler for %s (exactmatch %d)\n",
             prefix, exactMatch);
    pathHandlers.emplace_back(prefix, exactMatch, handler);
}

void UnregisterHTTPHandler(const std::string &prefix, bool exactMatch) {
    auto it = pathHandlers.begin();
    for (; it != pathHandlers.end(); ++it) {
        if (it->prefix == prefix && it->exactMatch == exactMatch) {
            break;
        }
    }
    if (it != pathHandlers.end()) {
        LogPrint(BCLog::HTTP,
                 "Unregistering HTTP handler for %s (exactmatch %d)\n", prefix,
                 exactMatch);
        pathHandlers.erase(it);
    }
}
