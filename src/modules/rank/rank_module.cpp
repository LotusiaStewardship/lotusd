// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <modules/rank/rank_module.h>

#include <api/api_util.h>
#include <amount.h>
#include <chain.h>
#include <logging.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <rpc/protocol.h>
#include <script/script.h>
#include <sqlite3.h>
#include <util/time.h>

#include <ctime>

static const uint8_t RANK_LOKAD[4] = {0x52, 0x41, 0x4e, 0x4b}; // "RANK"

static const char *PLATFORM_NAMES[] = {
    "unknown",  // 0x00
    "twitter",  // 0x01
    "nostr",    // 0x02
    "telegram", // 0x03
};
static const int NUM_PLATFORMS = sizeof(PLATFORM_NAMES) / sizeof(PLATFORM_NAMES[0]);

static std::string PlatformName(uint8_t id) {
    if (id < NUM_PLATFORMS) return PLATFORM_NAMES[id];
    return "platform_" + std::to_string(id);
}

static int PlatformId(const std::string &name) {
    for (int i = 0; i < NUM_PLATFORMS; i++) {
        if (name == PLATFORM_NAMES[i]) return i;
    }
    return -1;
}

struct RankVote {
    uint8_t version;
    uint8_t platform;
    std::string profileId;
    std::string postId;
    uint8_t sentiment; // 1=positive, 0=negative
};

/**
 * Try to decode a RANK vote from an OP_RETURN scriptPubKey.
 * Format: OP_RETURN <push LOKAD "RANK"> <push VERSION> <push PLATFORM>
 *         <push PROFILE_ID> [<push POST_ID>] <push SENTIMENT>
 */
static bool DecodeRankOutput(const CScript &script, RankVote &vote) {
    if (script.size() < 2 || script[0] != OP_RETURN) {
        return false;
    }

    CScript::const_iterator it = script.begin() + 1;
    std::vector<std::vector<uint8_t>> pushes;

    while (it < script.end()) {
        opcodetype opcode;
        std::vector<uint8_t> data;
        if (!script.GetOp(it, opcode, data)) {
            break;
        }
        pushes.push_back(std::move(data));
    }

    // Need at least: LOKAD(4) + VERSION(1) + PLATFORM(1) + PROFILE + SENTIMENT
    if (pushes.size() < 4) {
        return false;
    }

    // Check LOKAD ID
    if (pushes[0].size() != 4 ||
        memcmp(pushes[0].data(), RANK_LOKAD, 4) != 0) {
        return false;
    }

    // Version
    if (pushes[1].size() != 1) return false;
    vote.version = pushes[1][0];
    if (vote.version != 0x01) return false;

    // Platform
    if (pushes[2].size() != 1) return false;
    vote.platform = pushes[2][0];

    if (pushes.size() == 4) {
        // LOKAD + VERSION + PLATFORM + PROFILE_AND_SENTIMENT: not enough
        // Need at least LOKAD + VERSION + PLATFORM + PROFILE + SENTIMENT = 5
        return false;
    }

    // Profile ID (UTF-8)
    vote.profileId = std::string(pushes[3].begin(), pushes[3].end());
    if (vote.profileId.empty()) return false;

    if (pushes.size() == 5) {
        // No post ID: LOKAD + VERSION + PLATFORM + PROFILE + SENTIMENT
        vote.postId.clear();
        if (pushes[4].size() != 1) return false;
        vote.sentiment = pushes[4][0];
    } else if (pushes.size() >= 6) {
        // With post ID: LOKAD + VERSION + PLATFORM + PROFILE + POST + SENTIMENT
        vote.postId = std::string(pushes[4].begin(), pushes[4].end());
        if (pushes[5].size() != 1) return false;
        vote.sentiment = pushes[5][0];
    } else {
        return false;
    }

    if (vote.sentiment != 0x00 && vote.sentiment != 0x01) {
        return false;
    }

    return true;
}

static std::string DateFromTimestamp(int64_t ts) {
    time_t t = static_cast<time_t>(ts);
    struct tm utc;
    gmtime_r(&t, &utc);
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &utc);
    return std::string(buf);
}

// ─── IChainModule implementation ───────────────────────────────────────────────

std::string CRankModule::Name() const {
    return "rank";
}

bool CRankModule::Init(const fs::path &datadir, const CChainParams &) {
    fs::path dbpath = datadir / "rank.sqlite";
    LogPrintf("RANK module: opening database %s\n",
              fs::PathToString(dbpath));
    m_db = std::make_unique<CSqliteWrapper>(dbpath, false, false);
    CreateSchema();
    return true;
}

void CRankModule::CreateSchema() {
    m_db->ExecSQL(
        "CREATE TABLE IF NOT EXISTS rank_votes ("
        "  txid        TEXT NOT NULL,"
        "  vout        INTEGER NOT NULL,"
        "  block_height INTEGER NOT NULL,"
        "  block_time  INTEGER NOT NULL,"
        "  platform    INTEGER NOT NULL,"
        "  profile_id  TEXT NOT NULL,"
        "  post_id     TEXT NOT NULL DEFAULT '',"
        "  sentiment   INTEGER NOT NULL,"
        "  sats        INTEGER NOT NULL,"
        "  PRIMARY KEY (txid, vout)"
        ");"
    );
    m_db->ExecSQL(
        "CREATE INDEX IF NOT EXISTS idx_rv_profile "
        "ON rank_votes(platform, profile_id, block_height DESC);"
    );
    m_db->ExecSQL(
        "CREATE INDEX IF NOT EXISTS idx_rv_post "
        "ON rank_votes(platform, profile_id, post_id) "
        "WHERE post_id != '';"
    );
    m_db->ExecSQL(
        "CREATE INDEX IF NOT EXISTS idx_rv_time "
        "ON rank_votes(block_height DESC);"
    );

    m_db->ExecSQL(
        "CREATE TABLE IF NOT EXISTS rank_profiles ("
        "  platform    INTEGER NOT NULL,"
        "  profile_id  TEXT NOT NULL,"
        "  ranking     INTEGER NOT NULL DEFAULT 0,"
        "  votes_positive INTEGER NOT NULL DEFAULT 0,"
        "  votes_negative INTEGER NOT NULL DEFAULT 0,"
        "  last_vote_height INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (platform, profile_id)"
        ");"
    );
    m_db->ExecSQL(
        "CREATE INDEX IF NOT EXISTS idx_rp_ranking "
        "ON rank_profiles(ranking DESC);"
    );

    m_db->ExecSQL(
        "CREATE TABLE IF NOT EXISTS rank_posts ("
        "  platform    INTEGER NOT NULL,"
        "  profile_id  TEXT NOT NULL,"
        "  post_id     TEXT NOT NULL,"
        "  ranking     INTEGER NOT NULL DEFAULT 0,"
        "  votes_positive INTEGER NOT NULL DEFAULT 0,"
        "  votes_negative INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (platform, profile_id, post_id)"
        ");"
    );
    m_db->ExecSQL(
        "CREATE INDEX IF NOT EXISTS idx_rpost_ranking "
        "ON rank_posts(ranking DESC);"
    );

    m_db->ExecSQL(
        "CREATE TABLE IF NOT EXISTS rank_daily_stats ("
        "  date        TEXT NOT NULL,"
        "  platform    INTEGER NOT NULL,"
        "  profile_id  TEXT NOT NULL,"
        "  delta       INTEGER NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (date, platform, profile_id)"
        ");"
    );
}

void CRankModule::ConnectBlock(const CBlock &block,
                               const CBlockIndex *pindex,
                               const CChainParams &) {
    const int height = pindex->nHeight;
    const int64_t blockTime = pindex->GetBlockTime();
    const std::string date = DateFromTimestamp(blockTime);

    bool hasVotes = false;

    // Quick scan: check if any outputs contain RANK OP_RETURN
    for (const auto &tx : block.vtx) {
        for (size_t o = 0; o < tx->vout.size(); o++) {
            const auto &script = tx->vout[o].scriptPubKey;
            if (script.size() >= 6 && script[0] == OP_RETURN) {
                // Could be RANK -- full parse below
                hasVotes = true;
                break;
            }
        }
        if (hasVotes) break;
    }

    if (!hasVotes) return;

    if (!m_db->BeginTransaction()) {
        LogPrintf("ERROR: RANK module ConnectBlock: failed to begin tx at %d\n",
                  height);
        return;
    }

    sqlite3_stmt *ins_vote = m_db->Prepare(
        "INSERT OR IGNORE INTO rank_votes"
        "(txid, vout, block_height, block_time, platform, profile_id, "
        "post_id, sentiment, sats) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)");

    sqlite3_stmt *ups_profile = m_db->Prepare(
        "INSERT INTO rank_profiles(platform, profile_id, ranking, "
        "votes_positive, votes_negative, last_vote_height) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6) "
        "ON CONFLICT(platform, profile_id) DO UPDATE SET "
        "ranking = ranking + ?3, "
        "votes_positive = votes_positive + ?4, "
        "votes_negative = votes_negative + ?5, "
        "last_vote_height = MAX(last_vote_height, ?6)");

    sqlite3_stmt *ups_post = m_db->Prepare(
        "INSERT INTO rank_posts(platform, profile_id, post_id, ranking, "
        "votes_positive, votes_negative) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6) "
        "ON CONFLICT(platform, profile_id, post_id) DO UPDATE SET "
        "ranking = ranking + ?4, "
        "votes_positive = votes_positive + ?5, "
        "votes_negative = votes_negative + ?6");

    sqlite3_stmt *ups_daily = m_db->Prepare(
        "INSERT INTO rank_daily_stats(date, platform, profile_id, delta) "
        "VALUES(?1, ?2, ?3, ?4) "
        "ON CONFLICT(date, platform, profile_id) DO UPDATE SET "
        "delta = delta + ?4");

    for (const auto &tx : block.vtx) {
        const std::string txidHex = tx->GetId().GetHex();

        for (size_t o = 0; o < tx->vout.size(); o++) {
            const CTxOut &out = tx->vout[o];
            RankVote vote;
            if (!DecodeRankOutput(out.scriptPubKey, vote)) {
                continue;
            }

            int64_t sats = out.nValue / SATOSHI;
            int64_t signedSats =
                (vote.sentiment == 0x01) ? sats : -sats;

            // Insert vote
            sqlite3_bind_text(ins_vote, 1, txidHex.c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_int(ins_vote, 2, static_cast<int>(o));
            sqlite3_bind_int(ins_vote, 3, height);
            sqlite3_bind_int64(ins_vote, 4, blockTime);
            sqlite3_bind_int(ins_vote, 5, vote.platform);
            sqlite3_bind_text(ins_vote, 6, vote.profileId.c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_text(ins_vote, 7, vote.postId.c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_int(ins_vote, 8, vote.sentiment);
            sqlite3_bind_int64(ins_vote, 9, sats);
            sqlite3_step(ins_vote);
            sqlite3_reset(ins_vote);

            // Upsert profile
            int posVotes = (vote.sentiment == 0x01) ? 1 : 0;
            int negVotes = (vote.sentiment == 0x00) ? 1 : 0;

            sqlite3_bind_int(ups_profile, 1, vote.platform);
            sqlite3_bind_text(ups_profile, 2, vote.profileId.c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_int64(ups_profile, 3, signedSats);
            sqlite3_bind_int(ups_profile, 4, posVotes);
            sqlite3_bind_int(ups_profile, 5, negVotes);
            sqlite3_bind_int(ups_profile, 6, height);
            sqlite3_step(ups_profile);
            sqlite3_reset(ups_profile);

            // Upsert post (if post_id present)
            if (!vote.postId.empty()) {
                sqlite3_bind_int(ups_post, 1, vote.platform);
                sqlite3_bind_text(ups_post, 2, vote.profileId.c_str(), -1,
                                  SQLITE_TRANSIENT);
                sqlite3_bind_text(ups_post, 3, vote.postId.c_str(), -1,
                                  SQLITE_TRANSIENT);
                sqlite3_bind_int64(ups_post, 4, signedSats);
                sqlite3_bind_int(ups_post, 5, posVotes);
                sqlite3_bind_int(ups_post, 6, negVotes);
                sqlite3_step(ups_post);
                sqlite3_reset(ups_post);
            }

            // Upsert daily stats
            sqlite3_bind_text(ups_daily, 1, date.c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_int(ups_daily, 2, vote.platform);
            sqlite3_bind_text(ups_daily, 3, vote.profileId.c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_int64(ups_daily, 4, signedSats);
            sqlite3_step(ups_daily);
            sqlite3_reset(ups_daily);
        }
    }

    m_db->CommitTransaction();
}

void CRankModule::DisconnectBlock(const CBlock &block,
                                  const CBlockIndex *pindex,
                                  const CChainParams &) {
    const int height = pindex->nHeight;

    if (!m_db->BeginTransaction()) {
        LogPrintf("ERROR: RANK module DisconnectBlock: failed to begin tx "
                  "at %d\n", height);
        return;
    }

    // Query votes for this block to reverse aggregates
    sqlite3_stmt *q_votes = m_db->Prepare(
        "SELECT platform, profile_id, post_id, sentiment, sats "
        "FROM rank_votes WHERE block_height = ?1");
    sqlite3_bind_int(q_votes, 1, height);

    const std::string date = DateFromTimestamp(pindex->GetBlockTime());

    sqlite3_stmt *upd_profile = m_db->Prepare(
        "UPDATE rank_profiles SET "
        "ranking = ranking - ?3, "
        "votes_positive = votes_positive - ?4, "
        "votes_negative = votes_negative - ?5 "
        "WHERE platform = ?1 AND profile_id = ?2");

    sqlite3_stmt *upd_post = m_db->Prepare(
        "UPDATE rank_posts SET "
        "ranking = ranking - ?4, "
        "votes_positive = votes_positive - ?5, "
        "votes_negative = votes_negative - ?6 "
        "WHERE platform = ?1 AND profile_id = ?2 AND post_id = ?3");

    sqlite3_stmt *upd_daily = m_db->Prepare(
        "UPDATE rank_daily_stats SET delta = delta - ?4 "
        "WHERE date = ?1 AND platform = ?2 AND profile_id = ?3");

    while (sqlite3_step(q_votes) == SQLITE_ROW) {
        int platform = sqlite3_column_int(q_votes, 0);
        const char *pid = reinterpret_cast<const char *>(
            sqlite3_column_text(q_votes, 1));
        const char *postId = reinterpret_cast<const char *>(
            sqlite3_column_text(q_votes, 2));
        int sentiment = sqlite3_column_int(q_votes, 3);
        int64_t sats = sqlite3_column_int64(q_votes, 4);

        std::string profileId(pid ? pid : "");
        std::string postIdStr(postId ? postId : "");

        int64_t signedSats = (sentiment == 1) ? sats : -sats;
        int posVotes = (sentiment == 1) ? 1 : 0;
        int negVotes = (sentiment == 0) ? 1 : 0;

        // Reverse profile
        sqlite3_bind_int(upd_profile, 1, platform);
        sqlite3_bind_text(upd_profile, 2, profileId.c_str(), -1,
                          SQLITE_TRANSIENT);
        sqlite3_bind_int64(upd_profile, 3, signedSats);
        sqlite3_bind_int(upd_profile, 4, posVotes);
        sqlite3_bind_int(upd_profile, 5, negVotes);
        sqlite3_step(upd_profile);
        sqlite3_reset(upd_profile);

        // Reverse post
        if (!postIdStr.empty()) {
            sqlite3_bind_int(upd_post, 1, platform);
            sqlite3_bind_text(upd_post, 2, profileId.c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_text(upd_post, 3, postIdStr.c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_int64(upd_post, 4, signedSats);
            sqlite3_bind_int(upd_post, 5, posVotes);
            sqlite3_bind_int(upd_post, 6, negVotes);
            sqlite3_step(upd_post);
            sqlite3_reset(upd_post);
        }

        // Reverse daily
        sqlite3_bind_text(upd_daily, 1, date.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(upd_daily, 2, platform);
        sqlite3_bind_text(upd_daily, 3, profileId.c_str(), -1,
                          SQLITE_TRANSIENT);
        sqlite3_bind_int64(upd_daily, 4, signedSats);
        sqlite3_step(upd_daily);
        sqlite3_reset(upd_daily);
    }
    sqlite3_reset(q_votes);

    // Delete votes for this block
    sqlite3_stmt *del = m_db->Prepare(
        "DELETE FROM rank_votes WHERE block_height = ?1");
    sqlite3_bind_int(del, 1, height);
    sqlite3_step(del);
    sqlite3_reset(del);

    // Clean up zero-count profiles and posts
    m_db->ExecSQL("DELETE FROM rank_profiles WHERE "
                  "votes_positive = 0 AND votes_negative = 0");
    m_db->ExecSQL("DELETE FROM rank_posts WHERE "
                  "votes_positive = 0 AND votes_negative = 0");
    m_db->ExecSQL("DELETE FROM rank_daily_stats WHERE delta = 0");

    m_db->CommitTransaction();
}

void CRankModule::Shutdown() {
    LogPrintf("RANK module: closing database\n");
    m_db.reset();
}

// ─── API Handlers ──────────────────────────────────────────────────────────────

void CRankModule::RegisterRoutes(RouteAdder addRoute) {
    using namespace std::placeholders;
    addRoute(HTTPRequest::GET, "social/activity",
             std::bind(&CRankModule::HandleActivity, this, _1, _2, _3, _4));
    addRoute(HTTPRequest::GET, "social/profiles",
             std::bind(&CRankModule::HandleProfiles, this, _1, _2, _3, _4));
    addRoute(HTTPRequest::GET, "social/stats",
             std::bind(&CRankModule::HandleStats, this, _1, _2, _3, _4));
    // Profile detail & sub-routes (profiles/{platform}/{id}[/posts|/votes])
    addRoute(HTTPRequest::GET, "social",
             std::bind(&CRankModule::HandleProfileDetail, this, _1, _2, _3, _4));
}

bool CRankModule::HandleActivity(const util::Ref &, HTTPRequest *req,
                                 const std::vector<std::string> &,
                                 const api::QueryParams &qp) {
    int page = qp.GetInt("page", 1);
    int pageSize = qp.GetInt("pageSize", 25);
    if (page < 1) page = 1;
    if (pageSize < 1) pageSize = 1;
    if (pageSize > 100) pageSize = 100;
    int offset = (page - 1) * pageSize;

    // Count total
    sqlite3_stmt *cnt = m_db->Prepare(
        "SELECT COUNT(*) FROM rank_votes");
    int total = 0;
    if (sqlite3_step(cnt) == SQLITE_ROW) {
        total = sqlite3_column_int(cnt, 0);
    }
    sqlite3_reset(cnt);

    int numPages = (total + pageSize - 1) / pageSize;
    if (numPages < 1) numPages = 1;

    sqlite3_stmt *q = m_db->Prepare(
        "SELECT txid, block_time, platform, profile_id, sentiment, sats, "
        "post_id FROM rank_votes ORDER BY block_height DESC, vout ASC "
        "LIMIT ?1 OFFSET ?2");
    sqlite3_bind_int(q, 1, pageSize);
    sqlite3_bind_int(q, 2, offset);

    UniValue votes(UniValue::VARR);
    while (sqlite3_step(q) == SQLITE_ROW) {
        UniValue v(UniValue::VOBJ);
        v.pushKV("txid", std::string(reinterpret_cast<const char *>(
                     sqlite3_column_text(q, 0))));
        v.pushKV("firstSeen", (int64_t)sqlite3_column_int64(q, 1));
        int plat = sqlite3_column_int(q, 2);
        v.pushKV("platform", PlatformName(plat));
        v.pushKV("profileId", std::string(reinterpret_cast<const char *>(
                     sqlite3_column_text(q, 3))));
        v.pushKV("sentiment",
                 sqlite3_column_int(q, 4) == 1 ? "positive" : "negative");
        v.pushKV("sats", (int64_t)sqlite3_column_int64(q, 5));
        const char *postId = reinterpret_cast<const char *>(
            sqlite3_column_text(q, 6));
        v.pushKV("postId", std::string(postId ? postId : ""));
        votes.push_back(std::move(v));
    }
    sqlite3_reset(q);

    UniValue result(UniValue::VOBJ);
    result.pushKV("votes", votes);
    result.pushKV("numPages", numPages);
    api::WriteJSON(req, HTTP_OK, result);
    return true;
}

bool CRankModule::HandleProfiles(const util::Ref &, HTTPRequest *req,
                                 const std::vector<std::string> &,
                                 const api::QueryParams &qp) {
    int page = qp.GetInt("page", 1);
    int pageSize = qp.GetInt("pageSize", 25);
    if (page < 1) page = 1;
    if (pageSize < 1) pageSize = 1;
    if (pageSize > 200) pageSize = 200;
    int offset = (page - 1) * pageSize;

    sqlite3_stmt *cnt = m_db->Prepare(
        "SELECT COUNT(*) FROM rank_profiles");
    int total = 0;
    if (sqlite3_step(cnt) == SQLITE_ROW) {
        total = sqlite3_column_int(cnt, 0);
    }
    sqlite3_reset(cnt);

    int numPages = (total + pageSize - 1) / pageSize;
    if (numPages < 1) numPages = 1;

    sqlite3_stmt *q = m_db->Prepare(
        "SELECT platform, profile_id, ranking, votes_positive, "
        "votes_negative FROM rank_profiles ORDER BY ranking DESC "
        "LIMIT ?1 OFFSET ?2");
    sqlite3_bind_int(q, 1, pageSize);
    sqlite3_bind_int(q, 2, offset);

    UniValue profiles(UniValue::VARR);
    while (sqlite3_step(q) == SQLITE_ROW) {
        UniValue p(UniValue::VOBJ);
        int plat = sqlite3_column_int(q, 0);
        p.pushKV("platform", PlatformName(plat));
        p.pushKV("id", std::string(reinterpret_cast<const char *>(
                     sqlite3_column_text(q, 1))));
        p.pushKV("ranking", (int64_t)sqlite3_column_int64(q, 2));
        p.pushKV("votesPositive", sqlite3_column_int(q, 3));
        p.pushKV("votesNegative", sqlite3_column_int(q, 4));
        profiles.push_back(std::move(p));
    }
    sqlite3_reset(q);

    UniValue result(UniValue::VOBJ);
    result.pushKV("profiles", profiles);
    result.pushKV("numPages", numPages);
    api::WriteJSON(req, HTTP_OK, result);
    return true;
}

bool CRankModule::HandleProfileDetail(const util::Ref &, HTTPRequest *req,
                                      const std::vector<std::string> &parts,
                                      const api::QueryParams &qp) {
    // Routes: social/{platform}/{profileId}
    //         social/{platform}/{profileId}/posts
    //         social/{platform}/{profileId}/votes
    // parts[0] = "social", parts[1] = platform, parts[2] = profileId,
    //            parts[3] = optional "posts"/"votes"
    if (parts.size() < 3) {
        api::WriteError(req, HTTP_NOT_FOUND, "not_found",
                        "Missing platform or profile ID");
        return true;
    }

    const std::string &platform = parts[1];
    const std::string &profileId = parts[2];
    int platId = PlatformId(platform);

    if (parts.size() == 3 || (parts.size() == 4 && parts[3].empty())) {
        // Profile detail: GET /social/{platform}/{profileId}
        sqlite3_stmt *q = m_db->Prepare(
            "SELECT ranking, votes_positive, votes_negative "
            "FROM rank_profiles WHERE platform = ?1 AND profile_id = ?2");
        sqlite3_bind_int(q, 1, platId);
        sqlite3_bind_text(q, 2, profileId.c_str(), -1, SQLITE_TRANSIENT);

        UniValue result(UniValue::VOBJ);
        if (sqlite3_step(q) == SQLITE_ROW) {
            result.pushKV("platform", platform);
            result.pushKV("id", profileId);
            result.pushKV("ranking", (int64_t)sqlite3_column_int64(q, 0));
            result.pushKV("votesPositive", sqlite3_column_int(q, 1));
            result.pushKV("votesNegative", sqlite3_column_int(q, 2));
        } else {
            result.pushKV("platform", platform);
            result.pushKV("id", profileId);
            result.pushKV("ranking", (int64_t)0);
            result.pushKV("votesPositive", 0);
            result.pushKV("votesNegative", 0);
        }
        sqlite3_reset(q);
        api::WriteJSON(req, HTTP_OK, result);
        return true;
    }

    const std::string &subRoute = parts[3];

    if (subRoute == "posts") {
        int page = qp.GetInt("page", 1);
        int pageSize = qp.GetInt("pageSize", 10);
        if (page < 1) page = 1;
        if (pageSize < 1) pageSize = 1;
        if (pageSize > 100) pageSize = 100;
        int offset = (page - 1) * pageSize;

        sqlite3_stmt *cnt = m_db->Prepare(
            "SELECT COUNT(*) FROM rank_posts "
            "WHERE platform = ?1 AND profile_id = ?2");
        sqlite3_bind_int(cnt, 1, platId);
        sqlite3_bind_text(cnt, 2, profileId.c_str(), -1, SQLITE_TRANSIENT);
        int total = 0;
        if (sqlite3_step(cnt) == SQLITE_ROW) {
            total = sqlite3_column_int(cnt, 0);
        }
        sqlite3_reset(cnt);
        int numPages = (total + pageSize - 1) / pageSize;
        if (numPages < 1) numPages = 1;

        sqlite3_stmt *q = m_db->Prepare(
            "SELECT post_id, ranking, votes_positive, votes_negative "
            "FROM rank_posts WHERE platform = ?1 AND profile_id = ?2 "
            "ORDER BY ranking DESC LIMIT ?3 OFFSET ?4");
        sqlite3_bind_int(q, 1, platId);
        sqlite3_bind_text(q, 2, profileId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(q, 3, pageSize);
        sqlite3_bind_int(q, 4, offset);

        UniValue posts(UniValue::VARR);
        while (sqlite3_step(q) == SQLITE_ROW) {
            UniValue p(UniValue::VOBJ);
            p.pushKV("id", std::string(reinterpret_cast<const char *>(
                         sqlite3_column_text(q, 0))));
            p.pushKV("ranking", (int64_t)sqlite3_column_int64(q, 1));
            p.pushKV("votesPositive", sqlite3_column_int(q, 2));
            p.pushKV("votesNegative", sqlite3_column_int(q, 3));
            posts.push_back(std::move(p));
        }
        sqlite3_reset(q);

        UniValue result(UniValue::VOBJ);
        result.pushKV("posts", posts);
        result.pushKV("numPages", numPages);
        api::WriteJSON(req, HTTP_OK, result);
        return true;
    }

    if (subRoute == "votes") {
        int page = qp.GetInt("page", 1);
        int pageSize = qp.GetInt("pageSize", 10);
        if (page < 1) page = 1;
        if (pageSize < 1) pageSize = 1;
        if (pageSize > 100) pageSize = 100;
        int offset = (page - 1) * pageSize;

        sqlite3_stmt *cnt = m_db->Prepare(
            "SELECT COUNT(*) FROM rank_votes "
            "WHERE platform = ?1 AND profile_id = ?2");
        sqlite3_bind_int(cnt, 1, platId);
        sqlite3_bind_text(cnt, 2, profileId.c_str(), -1, SQLITE_TRANSIENT);
        int total = 0;
        if (sqlite3_step(cnt) == SQLITE_ROW) {
            total = sqlite3_column_int(cnt, 0);
        }
        sqlite3_reset(cnt);
        int numPages = (total + pageSize - 1) / pageSize;
        if (numPages < 1) numPages = 1;

        sqlite3_stmt *q = m_db->Prepare(
            "SELECT txid, block_time, sentiment, sats, post_id "
            "FROM rank_votes WHERE platform = ?1 AND profile_id = ?2 "
            "ORDER BY block_height DESC LIMIT ?3 OFFSET ?4");
        sqlite3_bind_int(q, 1, platId);
        sqlite3_bind_text(q, 2, profileId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(q, 3, pageSize);
        sqlite3_bind_int(q, 4, offset);

        UniValue votes(UniValue::VARR);
        while (sqlite3_step(q) == SQLITE_ROW) {
            UniValue v(UniValue::VOBJ);
            v.pushKV("txid", std::string(reinterpret_cast<const char *>(
                         sqlite3_column_text(q, 0))));
            v.pushKV("timestamp", (int64_t)sqlite3_column_int64(q, 1));
            v.pushKV("sentiment",
                     sqlite3_column_int(q, 2) == 1 ? "positive" : "negative");
            v.pushKV("sats", (int64_t)sqlite3_column_int64(q, 3));
            const char *postId = reinterpret_cast<const char *>(
                sqlite3_column_text(q, 4));
            UniValue post(UniValue::VOBJ);
            post.pushKV("id", std::string(postId ? postId : ""));
            v.pushKV("post", post);
            votes.push_back(std::move(v));
        }
        sqlite3_reset(q);

        UniValue result(UniValue::VOBJ);
        result.pushKV("votes", votes);
        result.pushKV("numPages", numPages);
        api::WriteJSON(req, HTTP_OK, result);
        return true;
    }

    api::WriteError(req, HTTP_NOT_FOUND, "not_found",
                    "Unknown sub-route: " + subRoute);
    return true;
}

bool CRankModule::HandleStats(const util::Ref &, HTTPRequest *req,
                              const std::vector<std::string> &parts,
                              const api::QueryParams &) {
    // Routes:
    //   social/stats/profiles/top    -> top ranked profiles (by daily delta)
    //   social/stats/profiles/bottom -> lowest ranked profiles
    //   social/stats/posts/top       -> top ranked posts
    //   social/stats/posts/bottom    -> lowest ranked posts
    // parts: [social, stats, profiles|posts, top|bottom]

    if (parts.size() < 4) {
        api::WriteError(req, HTTP_NOT_FOUND, "not_found",
                        "Stats endpoint requires: stats/{type}/{direction}");
        return true;
    }

    const std::string &type = parts[2];     // "profiles" or "posts"
    const std::string &direction = parts[3]; // "top" or "bottom"

    bool isTop = (direction == "top" || direction == "top-ranked");
    bool isBottom = (direction == "bottom" || direction == "lowest-ranked");
    if (!isTop && !isBottom) {
        api::WriteError(req, HTTP_NOT_FOUND, "not_found",
                        "Direction must be 'top' or 'bottom'");
        return true;
    }

    const std::string order = isTop ? "DESC" : "ASC";

    if (type == "profiles") {
        // Use rank_profiles table, sorted by ranking
        std::string sql =
            "SELECT platform, profile_id, ranking FROM rank_profiles "
            "ORDER BY ranking " + order + " LIMIT 10";
        sqlite3_stmt *q = m_db->Prepare(sql);

        UniValue arr(UniValue::VARR);
        while (sqlite3_step(q) == SQLITE_ROW) {
            UniValue p(UniValue::VOBJ);
            p.pushKV("platform",
                     PlatformName(sqlite3_column_int(q, 0)));
            p.pushKV("profileId", std::string(reinterpret_cast<const char *>(
                         sqlite3_column_text(q, 1))));
            p.pushKV("ranking", (int64_t)sqlite3_column_int64(q, 2));
            arr.push_back(std::move(p));
        }
        sqlite3_reset(q);

        api::WriteJSON(req, HTTP_OK, arr);
        return true;
    }

    if (type == "posts") {
        std::string sql =
            "SELECT p.platform, p.profile_id, p.post_id, p.ranking "
            "FROM rank_posts p ORDER BY p.ranking " + order + " LIMIT 10";
        sqlite3_stmt *q = m_db->Prepare(sql);

        UniValue arr(UniValue::VARR);
        while (sqlite3_step(q) == SQLITE_ROW) {
            UniValue p(UniValue::VOBJ);
            p.pushKV("platform",
                     PlatformName(sqlite3_column_int(q, 0)));
            p.pushKV("profileId", std::string(reinterpret_cast<const char *>(
                         sqlite3_column_text(q, 1))));
            p.pushKV("postId", std::string(reinterpret_cast<const char *>(
                         sqlite3_column_text(q, 2))));
            p.pushKV("ranking", (int64_t)sqlite3_column_int64(q, 3));
            arr.push_back(std::move(p));
        }
        sqlite3_reset(q);

        api::WriteJSON(req, HTTP_OK, arr);
        return true;
    }

    api::WriteError(req, HTTP_NOT_FOUND, "not_found",
                    "Stats type must be 'profiles' or 'posts'");
    return true;
}
