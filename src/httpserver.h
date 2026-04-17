// Copyright (c) 2015-2016 The Bitcoin Core developers
// Copyright (c) 2018-2019 The Bitcoin developers
// Copyright (c) 2026 Lotusia — cpp-httplib rewrite
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_HTTPSERVER_H
#define BITCOIN_HTTPSERVER_H

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

static const int DEFAULT_HTTP_THREADS = 16;
static const int DEFAULT_HTTP_SERVER_TIMEOUT = 30;

class Config;
class CService;
class HTTPRequest;

/**
 * Initialize HTTP server.
 * Call this before RegisterHTTPHandler.
 */
bool InitHTTPServer(Config &config);

/**
 * Start HTTP server.
 * This is separate from InitHTTPServer to give users race-condition-free time
 * to register their handlers between InitHTTPServer and StartHTTPServer.
 */
void StartHTTPServer();

/** Interrupt HTTP server threads */
void InterruptHTTPServer();

/** Stop HTTP server */
void StopHTTPServer();

/**
 * Change logging level for HTTP server.
 * Returns true (kept for API compat).
 */
bool UpdateHTTPServerLogging(bool enable);

/** Handler for requests to a certain HTTP path */
typedef std::function<bool(Config &config, HTTPRequest *req,
                           const std::string &)>
    HTTPRequestHandler;

/**
 * Register handler for prefix.
 * If multiple handlers match a prefix, the first-registered one will
 * be invoked.
 */
void RegisterHTTPHandler(const std::string &prefix, bool exactMatch,
                         const HTTPRequestHandler &handler);

/** Unregister handler for prefix */
void UnregisterHTTPHandler(const std::string &prefix, bool exactMatch);

/**
 * In-flight HTTP request.
 * Wraps cpp-httplib request/response pair.
 */
class HTTPRequest {
public:
    struct Impl;

    explicit HTTPRequest(std::unique_ptr<Impl> impl);
    ~HTTPRequest();

    enum RequestMethod { UNKNOWN, GET, POST, HEAD, PUT, OPTIONS };

    /** Get requested URI */
    std::string GetURI() const;

    /** Get CService (address:ip) for the origin of the http request */
    CService GetPeer() const;

    /** Get request method */
    RequestMethod GetRequestMethod() const;

    /**
     * Get the request header specified by hdr, or an empty string.
     * Return a pair (isPresent,string).
     */
    std::pair<bool, std::string> GetHeader(const std::string &hdr) const;

    /**
     * Read request body.
     *
     * @note As this consumes the underlying buffer, call this only once.
     * Repeated calls will return an empty string.
     */
    std::string ReadBody();

    /**
     * Write output header.
     *
     * @note call this before calling WriteErrorReply or Reply.
     */
    void WriteHeader(const std::string &hdr, const std::string &value);

    /**
     * Write HTTP reply.
     * nStatus is the HTTP status code to send.
     * strReply is the body of the reply. Keep it empty to send a standard
     * message.
     *
     * @note Can be called only once.
     */
    void WriteReply(int nStatus, const std::string &strReply = "");

private:
    std::unique_ptr<Impl> m_impl;
    bool replySent{false};
};

/** Event handler closure (kept for API compat, no longer used internally) */
class HTTPClosure {
public:
    virtual void operator()() = 0;
    virtual ~HTTPClosure() {}
};

#endif // BITCOIN_HTTPSERVER_H
