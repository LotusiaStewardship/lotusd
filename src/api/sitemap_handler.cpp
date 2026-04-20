// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/sitemap_handler.h>

#include <chain.h>
#include <httpserver.h>
#include <rpc/protocol.h>
#include <sqlite/block_tree_sqlite.h>
#include <sqlite/sqlite_wrapper.h>
#include <sqlite3.h>
#include <sync.h>
#include <txdb.h>
#include <util/strencodings.h>
#include <util/system.h>
#include <validation.h>

#include <fs.h>
#include <util/threadnames.h>
#include <util/time.h>
#include <shutdown.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <set>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace api {

// Per the Sitemaps protocol, each sitemap file may contain at most
// 50,000 URLs and must not exceed 50 MiB uncompressed. We use 50,000.
static constexpr int SHARD_SIZE = 50000;

// Static dashboard pages always present in the sitemap.
static const char *STATIC_PATHS[] = {
    "/dashboard",
    "/dashboard/explorer",
    "/dashboard/network",
    "/dashboard/top100",
    "/dashboard/stats",
    "/dashboard/apidocs",
    "/dashboard/social",
    "/dashboard/social/activity",
    "/dashboard/social/trending",
    "/dashboard/social/profiles",
};
static constexpr int STATIC_PATHS_COUNT =
    sizeof(STATIC_PATHS) / sizeof(STATIC_PATHS[0]);

// Absolute base URL used in <loc> entries and robots.txt. Sitemap entries
// MUST be absolute URLs per the sitemaps.org spec, so we always return one.
// Operators can override via LOTUS_SITE_URL (no trailing slash) when running
// the explorer behind a different domain.
static std::string SiteUrl() {
    const char *env = std::getenv("LOTUS_SITE_URL");
    if (env && *env) {
        std::string s(env);
        while (!s.empty() && s.back() == '/') s.pop_back();
        return s;
    }
    return std::string("https://explorer.burnlotus.fr");
}

// Percent-encode a path segment for safe use inside <loc>.
// Keeps unreserved chars; encodes everything else.
static std::string UrlEncodeSegment(const std::string &s) {
    static const char *kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                          c == '.' || c == '~';
        if (unreserved) {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[(c >> 4) & 0x0F];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

static void WriteXml(HTTPRequest *req, const std::string &xml) {
    req->WriteHeader("Content-Type", "application/xml; charset=utf-8");
    req->WriteHeader("Cache-Control", "public, max-age=3600");
    req->WriteHeader("X-Robots-Tag", "noindex");
    req->WriteReply(HTTP_OK, xml);
}

static void WriteText(HTTPRequest *req, const std::string &text,
                      const std::string &cacheControl) {
    req->WriteHeader("Content-Type", "text/plain; charset=utf-8");
    req->WriteHeader("Cache-Control", cacheControl);
    req->WriteReply(HTTP_OK, text);
}

static int ParseShardSuffix(const std::string &path,
                            const std::string &prefix) {
    // path like "/sitemap-blocks-3.xml", prefix "/sitemap-blocks-"
    if (path.size() <= prefix.size() + 4) return -1;
    if (path.substr(0, prefix.size()) != prefix) return -1;
    std::string mid = path.substr(prefix.size(),
                                  path.size() - prefix.size() - 4);
    if (path.substr(path.size() - 4) != ".xml") return -1;
    if (mid.empty()) return -1;
    for (char c : mid) {
        if (c < '0' || c > '9') return -1;
    }
    int n = std::atoi(mid.c_str());
    return (n >= 1) ? n : -1;
}

// ──────────────────────────────────────────────────────────────────────────
// Counts (used by sitemap index)

static int CountBlocks() {
    LOCK(cs_main);
    const CBlockIndex *tip = ::ChainActive().Tip();
    return tip ? (tip->nHeight + 1) : 0;
}

// Open rank.sqlite read-only. Returns nullptr on any error (database may
// not exist yet, table missing, etc.) — sitemap still produces a valid
// (empty) shard set in that case rather than 500-ing.
static sqlite3 *OpenRankDbReadOnly() {
    fs::path dbpath = gArgs.GetDataDirPath() / "modules" / "rank.sqlite";
    sqlite3 *db = nullptr;
    int rc = sqlite3_open_v2(fs::PathToString(dbpath).c_str(), &db,
                             SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX,
                             nullptr);
    if (rc != SQLITE_OK || !db) {
        if (db) sqlite3_close(db);
        return nullptr;
    }
    return db;
}

static int CountProfiles() {
    sqlite3 *db = OpenRankDbReadOnly();
    if (!db) return 0;
    sqlite3_stmt *q = nullptr;
    int n = 0;
    if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM rank_profiles", -1, &q,
                           nullptr) == SQLITE_OK) {
        if (sqlite3_step(q) == SQLITE_ROW) n = sqlite3_column_int(q, 0);
    }
    if (q) sqlite3_finalize(q);
    sqlite3_close(db);
    return n;
}

static int CountAddresses() {
    auto *btree = dynamic_cast<CBlockTreeSqlite *>(pblocktree.get());
    if (!btree) return 0;
    CSqliteWrapper &db = btree->GetDb();
    // Cap at 250k addresses for crawling purposes (top by balance). Wrap
    // in try-style: if address_balances doesn't exist yet (fresh node /
    // early IBD) just return 0 instead of 500-ing the whole sitemap.
    sqlite3_stmt *q = nullptr;
    try {
        q = db.Prepare(
            "SELECT min(250000, COUNT(*)) FROM address_balances "
            "WHERE balance_sats > 0");
    } catch (const std::exception &) {
        return 0;
    }
    int n = 0;
    if (sqlite3_step(q) == SQLITE_ROW) n = sqlite3_column_int(q, 0);
    sqlite3_reset(q);
    return n;
}

static int Shards(int total) {
    if (total <= 0) return 0;
    return (total + SHARD_SIZE - 1) / SHARD_SIZE;
}

// ──────────────────────────────────────────────────────────────────────────

static std::string BuildIndex() {
    std::string base = SiteUrl();
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<sitemapindex xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";

    auto add = [&](const std::string &name) {
        xml << "  <sitemap><loc>" << base << "/" << name
            << "</loc></sitemap>\n";
    };

    add("sitemap-static.xml");
    int blockShards = Shards(CountBlocks());
    for (int i = 1; i <= blockShards; i++) {
        add("sitemap-blocks-" + std::to_string(i) + ".xml");
    }
    int profileShards = Shards(CountProfiles());
    for (int i = 1; i <= profileShards; i++) {
        add("sitemap-profiles-" + std::to_string(i) + ".xml");
    }
    int addrShards = Shards(CountAddresses());
    for (int i = 1; i <= addrShards; i++) {
        add("sitemap-addresses-" + std::to_string(i) + ".xml");
    }

    xml << "</sitemapindex>\n";
    return xml.str();
}

static std::string BuildStatic() {
    std::string base = SiteUrl();
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";
    for (int i = 0; i < STATIC_PATHS_COUNT; i++) {
        xml << "  <url><loc>" << base << STATIC_PATHS[i]
            << "</loc><changefreq>hourly</changefreq>"
            << "<priority>0.8</priority></url>\n";
    }
    xml << "</urlset>\n";
    return xml.str();
}

// Block sitemap shard `n` (1-based): blocks (n-1)*SHARD_SIZE .. n*SHARD_SIZE-1
// indexed by height ascending.
static std::string BuildBlocksShard(int n) {
    std::string base = SiteUrl();
    int total = CountBlocks();
    int start = (n - 1) * SHARD_SIZE;
    int end = std::min(start + SHARD_SIZE, total);
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";
    for (int h = start; h < end; h++) {
        xml << "  <url><loc>" << base << "/dashboard/block/" << h
            << "</loc><changefreq>never</changefreq>"
            << "<priority>0.4</priority></url>\n";
    }
    xml << "</urlset>\n";
    return xml.str();
}

// Profile sitemap shard `n` (1-based) ordered by ranking DESC.
// rank_profiles lives in the rank module's own database
// (`<datadir>/modules/rank.sqlite`), NOT in the chain analytics DB.
static std::string BuildProfilesShard(int n) {
    std::string base = SiteUrl();
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";

    sqlite3 *db = OpenRankDbReadOnly();
    if (!db) {
        xml << "</urlset>\n";
        return xml.str();
    }

    static const char *PLATFORM_NAMES[] = {"unknown", "twitter", "nostr",
                                            "telegram"};
    static constexpr int NUM_PLATFORMS = 4;

    int offset = (n - 1) * SHARD_SIZE;
    sqlite3_stmt *q = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT platform, profile_id FROM rank_profiles "
            "ORDER BY ranking DESC LIMIT ?1 OFFSET ?2",
            -1, &q, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(q, 1, SHARD_SIZE);
        sqlite3_bind_int(q, 2, offset);
        while (sqlite3_step(q) == SQLITE_ROW) {
            int plat = sqlite3_column_int(q, 0);
            std::string platName = (plat >= 0 && plat < NUM_PLATFORMS)
                                       ? PLATFORM_NAMES[plat]
                                       : "platform_" + std::to_string(plat);
            const char *pid = reinterpret_cast<const char *>(
                sqlite3_column_text(q, 1));
            if (!pid) continue;
            xml << "  <url><loc>" << base << "/dashboard/social/"
                << UrlEncodeSegment(platName) << "/"
                << UrlEncodeSegment(pid)
                << "</loc><changefreq>daily</changefreq>"
                << "<priority>0.6</priority></url>\n";
        }
    }
    if (q) sqlite3_finalize(q);
    sqlite3_close(db);

    xml << "</urlset>\n";
    return xml.str();
}

// Top-N addresses sitemap shard `n` (1-based) ordered by balance DESC.
static std::string BuildAddressesShard(int n) {
    std::string base = SiteUrl();
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";

    auto *btree = dynamic_cast<CBlockTreeSqlite *>(pblocktree.get());
    if (!btree) {
        xml << "</urlset>\n";
        return xml.str();
    }
    CSqliteWrapper &db = btree->GetDb();

    int offset = (n - 1) * SHARD_SIZE;
    sqlite3_stmt *q = nullptr;
    try {
        q = db.Prepare(
            "SELECT address FROM address_balances "
            "WHERE balance_sats > 0 "
            "ORDER BY balance_sats DESC LIMIT ?1 OFFSET ?2");
    } catch (const std::exception &) {
        xml << "</urlset>\n";
        return xml.str();
    }
    sqlite3_bind_int(q, 1, SHARD_SIZE);
    sqlite3_bind_int(q, 2, offset);

    while (sqlite3_step(q) == SQLITE_ROW) {
        const char *addr = reinterpret_cast<const char *>(
            sqlite3_column_text(q, 0));
        if (!addr) continue;
        xml << "  <url><loc>" << base << "/dashboard/address/"
            << UrlEncodeSegment(addr)
            << "</loc><changefreq>weekly</changefreq>"
            << "<priority>0.3</priority></url>\n";
    }
    sqlite3_reset(q);

    xml << "</urlset>\n";
    return xml.str();
}

static std::string BuildRobots() {
    std::string base = SiteUrl();
    std::ostringstream out;
    out << "User-agent: *\n"
        << "Allow: /\n"
        << "Disallow: /api/\n"
        << "\n";
    if (!base.empty()) {
        out << "Sitemap: " << base << "/sitemap.xml\n";
    } else {
        out << "Sitemap: /sitemap.xml\n";
    }
    return out.str();
}

// ──────────────────────────────────────────────────────────────────────────
// Build dispatcher

// Cheap chain-readiness probe — see WaitForChainReady() below for the
// rationale. We can't safely call ChainActive() before pblocktree is set
// up because ChainstateActive() asserts on a null m_active_chainstate.
static bool ChainReadyForBuild(const std::string &path) {
    // robots.txt and the static sitemap don't touch chain/sqlite, so
    // they're always safe to build.
    if (path == "/robots.txt" || path == "/sitemap-static.xml") return true;
    return pblocktree && pblocktree.get() != nullptr;
}

// Empty-but-valid urlset returned when a builder is invoked before the
// chain is ready. Crawlers receive a parseable XML document instead of
// a 500 (which would get the whole sitemap submission rejected).
static std::string EmptyUrlset(const char *reason) {
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">"
        << "<!-- " << reason << " --></urlset>\n";
    return xml.str();
}

// Returns the body for a known sitemap path, or empty string if the path
// is not a sitemap route. Throws on internal errors so the caller can
// decide whether to surface the failure or quietly fall back.
static std::string BuildForPath(const std::string &path) {
    if (path == "/robots.txt") return BuildRobots();
    if (path == "/sitemap-static.xml") return BuildStatic();

    if (!ChainReadyForBuild(path)) {
        if (path == "/sitemap.xml") {
            // Index without dynamic shards — at least announces /static.
            std::string base = SiteUrl();
            std::ostringstream xml;
            xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                << "<sitemapindex xmlns=\"http://www.sitemaps.org/schemas/"
                   "sitemap/0.9\">\n"
                << "  <sitemap><loc>" << base
                << "/sitemap-static.xml</loc></sitemap>\n"
                << "</sitemapindex>\n";
            return xml.str();
        }
        return EmptyUrlset("chain not ready");
    }

    if (path == "/sitemap.xml") return BuildIndex();
    int s;
    if ((s = ParseShardSuffix(path, "/sitemap-blocks-")) > 0)
        return BuildBlocksShard(s);
    if ((s = ParseShardSuffix(path, "/sitemap-profiles-")) > 0)
        return BuildProfilesShard(s);
    if ((s = ParseShardSuffix(path, "/sitemap-addresses-")) > 0)
        return BuildAddressesShard(s);
    return std::string();
}

// TTL by shard kind. Stale entries are still served instantly; we just
// kick off an async rebuild when a stale hit comes in. The preheater
// also re-warms everything every g_rewarm_sec seconds in the background.
static int CacheTtlSeconds(const std::string &path) {
    if (path == "/robots.txt") return 86400;
    if (path == "/sitemap.xml") return 300;          // index follows tip
    if (path == "/sitemap-static.xml") return 86400; // static list
    if (ParseShardSuffix(path, "/sitemap-blocks-") > 0) return 600;
    if (ParseShardSuffix(path, "/sitemap-profiles-") > 0) return 1800;
    if (ParseShardSuffix(path, "/sitemap-addresses-") > 0) return 3600;
    return 600;
}

// ──────────────────────────────────────────────────────────────────────────
// Cache

struct CachedEntry {
    std::string body;
    int64_t built_at_sec = 0;
    int64_t build_ms = 0;
    size_t bytes = 0;
};

static std::shared_mutex g_cache_mtx;
static std::unordered_map<std::string, CachedEntry> g_cache;

// Single mutex serializing build operations so that two threads never run
// the same SQL prepare on a shared sqlite wrapper concurrently.
static std::mutex g_build_mtx;

// In-flight set so a stale-hit doesn't kick off duplicate async rebuilds
// for the same path.
static std::mutex g_inflight_mtx;
static std::set<std::string> g_inflight;

static bool TryGetCached(const std::string &path, std::string &body,
                         int64_t &built_at_sec) {
    std::shared_lock<std::shared_mutex> lk(g_cache_mtx);
    auto it = g_cache.find(path);
    if (it == g_cache.end()) return false;
    body = it->second.body;
    built_at_sec = it->second.built_at_sec;
    return true;
}

static void StoreCached(const std::string &path, std::string body,
                        int64_t build_ms) {
    CachedEntry e;
    e.bytes = body.size();
    e.body = std::move(body);
    e.built_at_sec = GetTime();
    e.build_ms = build_ms;
    std::unique_lock<std::shared_mutex> lk(g_cache_mtx);
    g_cache[path] = std::move(e);
}

// Build `path` synchronously under the build mutex and store in cache.
// Returns the body (empty on unknown path; throws on builder failure).
static std::string BuildAndStore(const std::string &path) {
    std::lock_guard<std::mutex> lk(g_build_mtx);
    int64_t t0 = GetTimeMillis();
    std::string body = BuildForPath(path);
    int64_t dt = GetTimeMillis() - t0;
    if (!body.empty()) {
        StoreCached(path, body, dt);
    }
    return body;
}

// Spawn a detached thread to rebuild `path` if not already in flight.
static void TriggerAsyncRebuild(const std::string &path) {
    {
        std::lock_guard<std::mutex> lk(g_inflight_mtx);
        if (g_inflight.count(path)) return;
        g_inflight.insert(path);
    }
    std::thread([path]() {
        util::ThreadRename("sitemap-rebuild");
        try {
            BuildAndStore(path);
        } catch (const std::exception &e) {
            LogPrintf("🗺️  Sitemap rebuild failed (%s): %s\n", path, e.what());
        } catch (...) {
            LogPrintf("🗺️  Sitemap rebuild failed (%s): unknown error\n", path);
        }
        std::lock_guard<std::mutex> lk(g_inflight_mtx);
        g_inflight.erase(path);
    }).detach();
}

// ──────────────────────────────────────────────────────────────────────────
// Preheater

static std::atomic<bool> g_preheat_stop{false};
static std::thread g_preheat_thread;
static std::condition_variable g_preheat_cv;
static std::mutex g_preheat_cv_mtx;
static int g_rewarm_sec = 600;

static void PreheatPath(const std::string &path, bool log) {
    if (g_preheat_stop) return;
    try {
        int64_t t0 = GetTimeMillis();
        BuildAndStore(path);
        int64_t dt = GetTimeMillis() - t0;
        size_t sz = 0;
        {
            std::shared_lock<std::shared_mutex> lk(g_cache_mtx);
            auto it = g_cache.find(path);
            if (it != g_cache.end()) sz = it->second.bytes;
        }
        if (log) {
            LogPrintf("🗺️    %-34s  %5dms  %8u B\n", path, (int)dt,
                      (unsigned)sz);
        }
    } catch (const std::exception &e) {
        if (log) LogPrintf("🗺️    %-34s  ERROR: %s\n", path, e.what());
    } catch (...) {
        if (log) LogPrintf("🗺️    %-34s  ERROR: unknown\n", path);
    }
}

static void DoPreheat(bool initial) {
    int64_t t0 = GetTimeMillis();
    bool log = initial; // verbose only on first run

    if (log) LogPrintf("🗺️  Sitemap preheat: scanning shards...\n");

    int blockShards = 0, profileShards = 0, addrShards = 0;
    try {
        blockShards = Shards(CountBlocks());
        profileShards = Shards(CountProfiles());
        addrShards = Shards(CountAddresses());
    } catch (...) {
    }

    if (log) {
        LogPrintf("🗺️  Sitemap preheat: 3 root + %d blocks + %d profiles "
                  "+ %d addresses = %d shards to build\n",
                  blockShards, profileShards, addrShards,
                  3 + blockShards + profileShards + addrShards);
    }

    PreheatPath("/robots.txt", log);
    PreheatPath("/sitemap.xml", log);
    PreheatPath("/sitemap-static.xml", log);

    for (int i = 1; i <= blockShards && !g_preheat_stop; i++) {
        PreheatPath("/sitemap-blocks-" + std::to_string(i) + ".xml", log);
    }
    for (int i = 1; i <= profileShards && !g_preheat_stop; i++) {
        PreheatPath("/sitemap-profiles-" + std::to_string(i) + ".xml", log);
    }
    for (int i = 1; i <= addrShards && !g_preheat_stop; i++) {
        PreheatPath("/sitemap-addresses-" + std::to_string(i) + ".xml", log);
    }

    // After block tip moves, the index needs a refresh so new shard URLs
    // appear. Always rebuild index last to reflect current counts.
    PreheatPath("/sitemap.xml", false);

    int64_t dt = GetTimeMillis() - t0;
    size_t totalBytes = 0;
    int totalEntries = 0;
    {
        std::shared_lock<std::shared_mutex> lk(g_cache_mtx);
        totalEntries = (int)g_cache.size();
        for (const auto &kv : g_cache) totalBytes += kv.second.bytes;
    }
    LogPrintf("🗺️  Sitemap %s done in %dms — %d shards, %.2f MiB cached\n",
              initial ? "preheat" : "rewarm", (int)dt, totalEntries,
              (double)totalBytes / (1024.0 * 1024.0));
}

// Block until the chain has been loaded enough that ChainActive() is safe
// to call. StartAPI() runs early in init.cpp — well before LoadBlockIndex
// finishes — so the very first preheat would otherwise trip the
// `m_active_chainstate` assertion in ChainstateActive(). pblocktree is
// reset to a non-null CBlockTreeSqlite *after* InitializeChainstate(), so
// it's a safe trailing marker for chain readiness without needing to
// touch ChainActive() directly.
static bool WaitForChainReady() {
    LogPrintf("🗺️  Sitemap preheater: waiting for chain to load...\n");
    int waited = 0;
    while (!g_preheat_stop) {
        if (pblocktree && pblocktree.get() != nullptr) {
            // One more brief settle so cs_main is taken cleanly and tip
            // is populated before our first CountBlocks() call.
            std::unique_lock<std::mutex> lk(g_preheat_cv_mtx);
            g_preheat_cv.wait_for(lk, std::chrono::seconds(2),
                                  [] { return g_preheat_stop.load(); });
            if (g_preheat_stop) return false;
            LogPrintf("🗺️  Sitemap preheater: chain ready after %ds\n",
                      waited);
            return true;
        }
        std::unique_lock<std::mutex> lk(g_preheat_cv_mtx);
        g_preheat_cv.wait_for(lk, std::chrono::seconds(2),
                              [] { return g_preheat_stop.load(); });
        waited += 2;
        if (waited > 0 && (waited % 30 == 0)) {
            LogPrintf("🗺️  Sitemap preheater: still waiting (%ds)...\n",
                      waited);
        }
    }
    return false;
}

static void PreheatLoop() {
    util::ThreadRename("sitemap-preheat");
    LogPrintf("🗺️  Sitemap preheater starting (rewarm every %ds)\n",
              g_rewarm_sec);

    if (!WaitForChainReady()) {
        LogPrintf("🗺️  Sitemap preheater: stopped before chain ready\n");
        return;
    }

    DoPreheat(/*initial=*/true);

    while (!g_preheat_stop) {
        std::unique_lock<std::mutex> lk(g_preheat_cv_mtx);
        g_preheat_cv.wait_for(lk, std::chrono::seconds(g_rewarm_sec),
                              [] { return g_preheat_stop.load(); });
        if (g_preheat_stop) break;
        DoPreheat(/*initial=*/false);
    }
    LogPrintf("🗺️  Sitemap preheater stopped\n");
}

void StartSitemapPreheater() {
    g_preheat_stop = false;
    g_rewarm_sec = (int)gArgs.GetArg("-sitemapwarm", 600);
    if (g_rewarm_sec < 30) g_rewarm_sec = 30;
    g_preheat_thread = std::thread(PreheatLoop);
}

void StopSitemapPreheater() {
    g_preheat_stop = true;
    g_preheat_cv.notify_all();
    if (g_preheat_thread.joinable()) g_preheat_thread.join();
    std::unique_lock<std::shared_mutex> lk(g_cache_mtx);
    g_cache.clear();
}

// ──────────────────────────────────────────────────────────────────────────

bool IsSitemapPath(const std::string &path) {
    if (path == "/sitemap.xml") return true;
    if (path == "/sitemap-static.xml") return true;
    if (path == "/robots.txt") return true;
    if (ParseShardSuffix(path, "/sitemap-blocks-") > 0) return true;
    if (ParseShardSuffix(path, "/sitemap-profiles-") > 0) return true;
    if (ParseShardSuffix(path, "/sitemap-addresses-") > 0) return true;
    return false;
}

static void ServeBody(HTTPRequest *req, const std::string &path,
                      const std::string &body) {
    if (path == "/robots.txt") {
        WriteText(req, body, "public, max-age=86400");
    } else {
        WriteXml(req, body);
    }
}

bool HandleSitemap(HTTPRequest *req, const std::string &path) {
    // Fast path: serve from cache. If stale, kick off an async rebuild
    // but still return the cached body immediately — Google times out
    // aggressively on slow sitemap responses and silently drops them.
    {
        std::string body;
        int64_t built_at = 0;
        if (TryGetCached(path, body, built_at)) {
            int64_t age = GetTime() - built_at;
            if (age > CacheTtlSeconds(path)) {
                TriggerAsyncRebuild(path);
            }
            ServeBody(req, path, body);
            return true;
        }
    }

    // Cache miss — preheater hasn't reached this shard yet (or it's an
    // unknown path). Build synchronously, store in cache, return.
    try {
        std::string body = BuildAndStore(path);
        if (body.empty()) {
            req->WriteHeader("Content-Type", "text/plain; charset=utf-8");
            req->WriteReply(HTTP_NOT_FOUND, "Not Found\n");
            return true;
        }
        ServeBody(req, path, body);
    } catch (const std::exception &e) {
        // Last-resort fallback: never 500 a crawler. Return a syntactically
        // valid empty urlset with the error as an XML comment.
        std::ostringstream empty;
        empty << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              << "<urlset xmlns=\"http://www.sitemaps.org/schemas/"
                 "sitemap/0.9\"><!-- "
              << e.what() << " --></urlset>\n";
        WriteXml(req, empty.str());
    }
    return true;
}

} // namespace api
