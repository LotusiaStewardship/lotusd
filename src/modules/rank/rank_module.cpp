// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <modules/rank/rank_module.h>

#include <api/api_util.h>
#include <amount.h>
#include <blockdb.h>
#include <chain.h>
#include <chainparams.h>
#include <logging.h>
#include <node/context.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <rpc/protocol.h>
#include <script/script.h>
#include <sqlite3.h>
#include <sync.h>
#include <util/time.h>
#include <validation.h>

#include <algorithm>
#include <atomic>
#include <ctime>
#include <future>
#include <map>
#include <thread>

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
 * Decode Format A (RankService / multi-push):
 *   OP_RETURN
 *   push4  "RANK"           (04 52414e4b)
 *   OP_0 | OP_1             (sentiment: 00=negative, 51=positive)
 *   push1  <platform>       (01 xx)
 *   push16 <profile_id>     (10 + 16 bytes, zero-padded UTF-8)
 *   [push8 <post_id>]       (08 + 8 bytes, big-endian uint64, optional)
 */
struct ScriptElement {
    opcodetype opcode;
    std::vector<uint8_t> data;
};

static bool DecodeRankFormatA(const std::vector<ScriptElement> &elems,
                              RankVote &vote) {
    if (elems.size() < 4) return false;

    // [0] LOKAD "RANK" (exactly 4 bytes)
    if (elems[0].data.size() != 4 ||
        memcmp(elems[0].data.data(), RANK_LOKAD, 4) != 0) {
        return false;
    }

    // [1] Sentiment via opcode: OP_0 = negative, OP_1 = positive
    if (elems[1].opcode == OP_0) {
        vote.sentiment = 0x00;
    } else if (elems[1].opcode == OP_1) {
        vote.sentiment = 0x01;
    } else {
        return false;
    }

    // [2] Platform (1-byte data push)
    if (elems[2].data.size() != 1) return false;
    vote.platform = elems[2].data[0];

    // [3] Profile ID (zero-padded UTF-8, typically 16 bytes)
    if (elems[3].data.empty()) return false;
    std::string rawProfile(elems[3].data.begin(), elems[3].data.end());
    rawProfile.erase(
        std::remove(rawProfile.begin(), rawProfile.end(), '\0'),
        rawProfile.end());
    if (rawProfile.empty()) return false;
    vote.profileId = std::move(rawProfile);

    // [4] Optional post ID (8-byte big-endian uint64)
    if (elems.size() >= 5 && !elems[4].data.empty()) {
        if (elems[4].data.size() == 8) {
            uint64_t postIdNum = 0;
            for (int i = 0; i < 8; i++) {
                postIdNum = (postIdNum << 8) | elems[4].data[i];
            }
            vote.postId = std::to_string(postIdNum);
        } else {
            vote.postId = std::string(elems[4].data.begin(),
                                      elems[4].data.end());
        }
    } else {
        vote.postId.clear();
    }

    vote.version = 0x01;
    return true;
}

/**
 * Decode Format B (SimplifiedVotingService / single-push):
 *   OP_RETURN
 *   pushN <entire payload as one blob>
 *
 * Payload layout:
 *   bytes 0-3:  "RANK" (52 41 4e 4b)
 *   byte  4:    version (0x01)
 *   byte  5:    sentiment (0x00=negative, 0x01=positive)
 *   byte  6:    platform_length
 *   bytes 7..:  platform string (UTF-8)
 *   next byte:  profile_length
 *   next bytes: profile string (UTF-8)
 *   next byte:  post_length (0x00 = no post)
 *   next bytes: post string (UTF-8, if length > 0)
 */
static bool DecodeRankFormatB(const std::vector<ScriptElement> &elems,
                              RankVote &vote) {
    if (elems.size() < 1) return false;

    const auto &blob = elems[0].data;
    if (blob.size() < 8) return false;  // minimum: RANK(4) + ver + sent + plen + prlen
    if (memcmp(blob.data(), RANK_LOKAD, 4) != 0) return false;

    // Must be a single-push (data > 4 bytes), not the 4-byte LOKAD of Format A
    if (blob.size() == 4) return false;

    size_t off = 4;
    uint8_t version = blob[off++];
    if (version != 0x01) return false;
    vote.version = version;

    uint8_t sentimentByte = blob[off++];
    if (sentimentByte != 0x00 && sentimentByte != 0x01) return false;
    vote.sentiment = sentimentByte;

    // Platform (length-prefixed string)
    if (off >= blob.size()) return false;
    uint8_t platformLen = blob[off++];
    if (off + platformLen > blob.size()) return false;
    std::string platformStr(blob.begin() + off, blob.begin() + off + platformLen);
    off += platformLen;

    vote.platform = 0;
    for (int i = 0; i < NUM_PLATFORMS; i++) {
        if (platformStr == PLATFORM_NAMES[i]) {
            vote.platform = static_cast<uint8_t>(i);
            break;
        }
    }

    // Profile ID (length-prefixed string)
    if (off >= blob.size()) return false;
    uint8_t profileLen = blob[off++];
    if (off + profileLen > blob.size()) return false;
    vote.profileId = std::string(blob.begin() + off, blob.begin() + off + profileLen);
    off += profileLen;
    if (vote.profileId.empty()) return false;

    // Post ID (length-prefixed string, 0x00 = no post)
    vote.postId.clear();
    if (off < blob.size()) {
        uint8_t postLen = blob[off++];
        if (postLen > 0 && off + postLen <= blob.size()) {
            vote.postId = std::string(blob.begin() + off, blob.begin() + off + postLen);
            off += postLen;
        }
    }

    return true;
}

/**
 * Try to decode a RANK vote from an OP_RETURN scriptPubKey.
 * Supports both Format A (multi-push) and Format B (single-push).
 */
static bool DecodeRankOutput(const CScript &script, RankVote &vote) {
    if (script.size() < 2 || script[0] != OP_RETURN) {
        return false;
    }

    CScript::const_iterator it = script.begin() + 1;
    std::vector<ScriptElement> elems;

    while (it < script.end()) {
        opcodetype opcode;
        std::vector<uint8_t> data;
        if (!script.GetOp(it, opcode, data)) {
            break;
        }
        elems.push_back({opcode, std::move(data)});
    }

    if (elems.empty()) return false;

    // Format A: first push is exactly 4 bytes "RANK", followed by opcode-based
    // sentiment and separate pushes for platform / profile / post.
    if (elems[0].data.size() == 4 &&
        memcmp(elems[0].data.data(), RANK_LOKAD, 4) == 0) {
        return DecodeRankFormatA(elems, vote);
    }

    // Format B: entire payload in one push, starts with "RANK" + version.
    if (elems[0].data.size() > 4 &&
        memcmp(elems[0].data.data(), RANK_LOKAD, 4) == 0) {
        return DecodeRankFormatB(elems, vote);
    }

    return false;
}

static std::string DateFromTimestamp(int64_t ts) {
    time_t t = static_cast<time_t>(ts);
    struct tm utc;
    gmtime_r(&t, &utc);
    char buf[11];
    strftime(buf, sizeof(buf), "%Y-%m-%d", &utc);
    return std::string(buf);
}

struct ExtractedVote {
    std::string txid;
    int vout;
    int height;
    int64_t blockTime;
    uint8_t platform;
    std::string profileId;
    std::string postId;
    uint8_t sentiment;
    int64_t sats;
};

static std::vector<ExtractedVote>
ExtractVotesFromBlock(const CBlock &block, const CBlockIndex *pindex) {
    std::vector<ExtractedVote> out;
    const int height = pindex->nHeight;
    const int64_t blockTime = pindex->GetBlockTime();

    for (const auto &tx : block.vtx) {
        for (size_t o = 0; o < tx->vout.size(); o++) {
            const CTxOut &txout = tx->vout[o];
            RankVote vote;
            if (!DecodeRankOutput(txout.scriptPubKey, vote)) {
                continue;
            }
            ExtractedVote ev;
            ev.txid = tx->GetId().GetHex();
            ev.vout = static_cast<int>(o);
            ev.height = height;
            ev.blockTime = blockTime;
            ev.platform = vote.platform;
            ev.profileId = std::move(vote.profileId);
            ev.postId = std::move(vote.postId);
            ev.sentiment = vote.sentiment;
            ev.sats = txout.nValue / SATOSHI;
            out.push_back(std::move(ev));
        }
    }
    return out;
}

// ─── IChainModule implementation ───────────────────────────────────────────────

std::string CRankModule::Name() const {
    return "rank";
}

bool CRankModule::Init(const fs::path &datadir, const CChainParams &) {
    fs::path dbpath = datadir / "rank.sqlite";
    LogPrintf("🗳️  RANK database: %s\n",
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
    m_db->ExecSQL("DROP INDEX IF EXISTS idx_rv_time;");
    m_db->ExecSQL(
        "CREATE INDEX IF NOT EXISTS idx_rv_time "
        "ON rank_votes(block_height DESC, vout ASC);"
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
        "CREATE INDEX IF NOT EXISTS idx_rpost_profile_rank "
        "ON rank_posts(platform, profile_id, ranking DESC);"
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

    m_db->ExecSQL(
        "CREATE TABLE IF NOT EXISTS rank_meta ("
        "  key   TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL"
        ");"
    );
}

void CRankModule::SetMeta(const std::string &key,
                          const std::string &value) {
    sqlite3_stmt *stmt = m_db->Prepare(
        "INSERT INTO rank_meta(key, value) VALUES(?1, ?2) "
        "ON CONFLICT(key) DO UPDATE SET value = ?2");
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_reset(stmt);
}

std::string CRankModule::GetMeta(const std::string &key,
                                 const std::string &fallback) {
    sqlite3_stmt *stmt = m_db->Prepare(
        "SELECT value FROM rank_meta WHERE key = ?1");
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    std::string result = fallback;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *val = reinterpret_cast<const char *>(
            sqlite3_column_text(stmt, 0));
        if (val) result = val;
    }
    sqlite3_reset(stmt);
    return result;
}

int CRankModule::GetLastIndexedHeight() {
    std::string val = GetMeta("last_indexed_height", "-1");
    int h = atoi(val.c_str());
    if (h < 0) {
        sqlite3_stmt *q = m_db->Prepare(
            "SELECT MAX(block_height) FROM rank_votes");
        if (sqlite3_step(q) == SQLITE_ROW &&
            sqlite3_column_type(q, 0) != SQLITE_NULL) {
            h = sqlite3_column_int(q, 0);
        }
        sqlite3_reset(q);
    }
    return h;
}

void CRankModule::InsertExtractedVotes(
        const std::vector<ExtractedVote> &votes) {
    if (votes.empty()) return;

    m_db->BeginTransaction();

    sqlite3_stmt *ins = m_db->Prepare(
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

    for (const auto &ev : votes) {
        int64_t signedSats =
            (ev.sentiment == 0x01) ? ev.sats : -ev.sats;
        int posVotes = (ev.sentiment == 0x01) ? 1 : 0;
        int negVotes = (ev.sentiment == 0x00) ? 1 : 0;
        std::string date = DateFromTimestamp(ev.blockTime);

        sqlite3_bind_text(ins, 1, ev.txid.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ins, 2, ev.vout);
        sqlite3_bind_int(ins, 3, ev.height);
        sqlite3_bind_int64(ins, 4, ev.blockTime);
        sqlite3_bind_int(ins, 5, ev.platform);
        sqlite3_bind_text(ins, 6, ev.profileId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(ins, 7, ev.postId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ins, 8, ev.sentiment);
        sqlite3_bind_int64(ins, 9, ev.sats);
        sqlite3_step(ins);
        sqlite3_reset(ins);

        sqlite3_bind_int(ups_profile, 1, ev.platform);
        sqlite3_bind_text(ups_profile, 2, ev.profileId.c_str(), -1,
                          SQLITE_TRANSIENT);
        sqlite3_bind_int64(ups_profile, 3, signedSats);
        sqlite3_bind_int(ups_profile, 4, posVotes);
        sqlite3_bind_int(ups_profile, 5, negVotes);
        sqlite3_bind_int(ups_profile, 6, ev.height);
        sqlite3_step(ups_profile);
        sqlite3_reset(ups_profile);

        if (!ev.postId.empty()) {
            sqlite3_bind_int(ups_post, 1, ev.platform);
            sqlite3_bind_text(ups_post, 2, ev.profileId.c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_text(ups_post, 3, ev.postId.c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_int64(ups_post, 4, signedSats);
            sqlite3_bind_int(ups_post, 5, posVotes);
            sqlite3_bind_int(ups_post, 6, negVotes);
            sqlite3_step(ups_post);
            sqlite3_reset(ups_post);
        }

        sqlite3_bind_text(ups_daily, 1, date.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ups_daily, 2, ev.platform);
        sqlite3_bind_text(ups_daily, 3, ev.profileId.c_str(), -1,
                          SQLITE_TRANSIENT);
        sqlite3_bind_int64(ups_daily, 4, signedSats);
        sqlite3_step(ups_daily);
        sqlite3_reset(ups_daily);
    }

    m_db->CommitTransaction();
}

int CRankModule::RescanRange(int startHeight, int endHeight,
                             const CChainParams &params) {
    const int totalRange = endHeight - startHeight + 1;
    const int64_t tStart = GetTimeMicros();
    const unsigned int nWorkers = std::max(1u,
        std::min(std::thread::hardware_concurrency(), 16u));
    const int CHUNK_SIZE = 5000;

    LogPrintf("🗳️  RANK rescan: %d blocks (%d → %d) | %u threads\n",
              totalRange, startHeight, endHeight, nWorkers);

    // Collect block index pointers under cs_main once.
    int actualEnd;
    std::vector<const CBlockIndex *> indices;
    {
        LOCK(cs_main);
        actualEnd = std::min(endHeight, ::ChainActive().Height());
        indices.resize(actualEnd - startHeight + 1, nullptr);
        for (int h = startHeight; h <= actualEnd; h++) {
            indices[h - startHeight] = ::ChainActive()[h];
        }
    }

    if (actualEnd < endHeight) {
        LogPrintf("🗳️  RANK rescan: chain tip %d < requested %d, "
                  "capping at %d\n", actualEnd, endHeight, actualEnd);
    }

    std::atomic<int> totalVotes{0};
    std::atomic<int> blocksProcessed{0};
    int64_t tLastLog = tStart;
    const Consensus::Params &consensus = params.GetConsensus();

    // Process in chunks: parallel read+decode, serial DB write
    for (int chunkStart = 0; chunkStart < (int)indices.size();
         chunkStart += CHUNK_SIZE) {

        int chunkEnd = std::min(chunkStart + CHUNK_SIZE,
                                (int)indices.size());
        int chunkLen = chunkEnd - chunkStart;

        // Parallel extraction: each worker processes a slice of the chunk
        int perWorker = (chunkLen + nWorkers - 1) / nWorkers;
        std::vector<std::future<std::vector<ExtractedVote>>> futures;

        for (unsigned int w = 0; w < nWorkers; w++) {
            int wStart = chunkStart + w * perWorker;
            int wEnd = std::min(wStart + perWorker, chunkEnd);
            if (wStart >= chunkEnd) break;

            futures.push_back(std::async(std::launch::async,
                [&indices, &consensus, wStart, wEnd, startHeight]()
                    -> std::vector<ExtractedVote> {
                std::vector<ExtractedVote> workerVotes;
                for (int i = wStart; i < wEnd; i++) {
                    const CBlockIndex *pindex = indices[i];
                    if (!pindex) continue;

                    CBlock block;
                    if (!ReadBlockFromDisk(block, pindex, consensus)) {
                        continue;
                    }
                    auto votes = ExtractVotesFromBlock(block, pindex);
                    if (!votes.empty()) {
                        workerVotes.insert(workerVotes.end(),
                            std::make_move_iterator(votes.begin()),
                            std::make_move_iterator(votes.end()));
                    }
                }
                return workerVotes;
            }));
        }

        // Collect results from all workers
        std::vector<ExtractedVote> chunkVotes;
        for (auto &f : futures) {
            auto workerResult = f.get();
            if (!workerResult.empty()) {
                chunkVotes.insert(chunkVotes.end(),
                    std::make_move_iterator(workerResult.begin()),
                    std::make_move_iterator(workerResult.end()));
            }
        }

        // Serial DB write for this chunk
        if (!chunkVotes.empty()) {
            std::lock_guard<std::recursive_mutex> lock(m_db_mutex);
            InsertExtractedVotes(chunkVotes);
        }

        int newVotes = static_cast<int>(chunkVotes.size());
        totalVotes += newVotes;
        blocksProcessed += chunkLen;

        // Progress log every 5 seconds
        int64_t tNow = GetTimeMicros();
        bool isFinal = blocksProcessed.load() == (int)indices.size();
        if (tNow - tLastLog >= 5'000'000 || isFinal) {
            double elapsed = (tNow - tStart) / 1e6;
            int done = blocksProcessed.load();
            double pct = indices.size() > 0
                ? 100.0 * done / indices.size() : 100.0;
            double bps = elapsed > 0 ? done / elapsed : 0;
            int remaining = (int)indices.size() - done;
            int etaSec = bps > 0 ? static_cast<int>(remaining / bps) : 0;
            LogPrintf("🗳️  RANK rescan: %d/%d (%.1f%%) | "
                      "%.0f blk/s | %d votes | ETA %dm%02ds\n",
                      done, (int)indices.size(), pct,
                      bps, totalVotes.load(),
                      etaSec / 60, etaSec % 60);
            tLastLog = tNow;
        }

        // Checkpoint progress periodically
        if (blocksProcessed.load() % 50000 < CHUNK_SIZE) {
            std::lock_guard<std::recursive_mutex> lock(m_db_mutex);
            SetMeta("last_indexed_height",
                    std::to_string(startHeight + chunkEnd - 1));
        }
    }

    {
        std::lock_guard<std::recursive_mutex> lock(m_db_mutex);
        SetMeta("last_indexed_height", std::to_string(actualEnd));
    }

    double elapsed = (GetTimeMicros() - tStart) / 1e6;
    LogPrintf("🗳️  RANK rescan complete: %d blocks | %d votes | "
              "%.1fs (%.0f blk/s)\n",
              (int)indices.size(), totalVotes.load(),
              elapsed, elapsed > 0 ? indices.size() / elapsed : 0);
    return totalVotes;
}

void CRankModule::ConnectBlock(const CBlock &block,
                               const CBlockIndex *pindex,
                               const CChainParams &) {
    std::lock_guard<std::recursive_mutex> lock(m_db_mutex);
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

    int voteCount = 0;
    int posCount = 0;
    int negCount = 0;
    std::map<std::string, int> profileHits;

    for (const auto &tx : block.vtx) {
        const std::string txidHex = tx->GetId().GetHex();

        for (size_t o = 0; o < tx->vout.size(); o++) {
            const CTxOut &out = tx->vout[o];
            RankVote vote;
            if (!DecodeRankOutput(out.scriptPubKey, vote)) {
                continue;
            }

            voteCount++;
            int64_t sats = out.nValue / SATOSHI;
            int64_t signedSats =
                (vote.sentiment == 0x01) ? sats : -sats;
            int posVotes = (vote.sentiment == 0x01) ? 1 : 0;
            int negVotes = (vote.sentiment == 0x00) ? 1 : 0;
            posCount += posVotes;
            negCount += negVotes;
            profileHits[vote.profileId]++;

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

            sqlite3_bind_int(ups_profile, 1, vote.platform);
            sqlite3_bind_text(ups_profile, 2, vote.profileId.c_str(), -1,
                              SQLITE_TRANSIENT);
            sqlite3_bind_int64(ups_profile, 3, signedSats);
            sqlite3_bind_int(ups_profile, 4, posVotes);
            sqlite3_bind_int(ups_profile, 5, negVotes);
            sqlite3_bind_int(ups_profile, 6, height);
            sqlite3_step(ups_profile);
            sqlite3_reset(ups_profile);

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

    if (voteCount == 0) return;

    bool isIBD = ::ChainstateActive().IsInitialBlockDownload();

    if (!isIBD) {
        // At chain tip: log every block individually
        std::string summary;
        std::vector<std::pair<int, std::string>> sorted;
        for (const auto &p : profileHits) {
            sorted.push_back({p.second, p.first});
        }
        std::sort(sorted.rbegin(), sorted.rend());
        int shown = 0;
        for (const auto &s : sorted) {
            if (shown >= 3) {
                int remaining = static_cast<int>(sorted.size()) - shown;
                if (remaining > 0)
                    summary += " +" + std::to_string(remaining) + " more";
                break;
            }
            if (!summary.empty()) summary += ", ";
            summary += s.second;
            if (s.first > 1) summary += " x" + std::to_string(s.first);
            shown++;
        }
        LogPrintf("🗳️  Block %d | %d votes (+%d/-%d) | %s\n",
                  height, voteCount, posCount, negCount, summary);
    } else {
        // During IBD: accumulate and log a summary every 10 seconds
        if (m_ibd_blocks_with_votes == 0) {
            m_ibd_first_height = height;
        }
        m_ibd_votes_accum += voteCount;
        m_ibd_pos_accum += posCount;
        m_ibd_neg_accum += negCount;
        m_ibd_blocks_with_votes++;
        m_ibd_last_height = height;

        int64_t now = GetTimeMicros();
        if (m_ibd_log_last == 0) m_ibd_log_last = now;

        if (now - m_ibd_log_last >= 10'000'000) {
            LogPrintf("🗳️  RANK IBD: blocks %d→%d | %d blocks with votes | "
                      "%d votes (+%d/-%d)\n",
                      m_ibd_first_height, m_ibd_last_height,
                      m_ibd_blocks_with_votes,
                      m_ibd_votes_accum, m_ibd_pos_accum, m_ibd_neg_accum);
            m_ibd_votes_accum = 0;
            m_ibd_pos_accum = 0;
            m_ibd_neg_accum = 0;
            m_ibd_blocks_with_votes = 0;
            m_ibd_log_last = now;
        }
    }
}

void CRankModule::DisconnectBlock(const CBlock &block,
                                  const CBlockIndex *pindex,
                                  const CChainParams &) {
    std::lock_guard<std::recursive_mutex> lock(m_db_mutex);
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
    // Flush any pending IBD log accumulator
    if (m_ibd_votes_accum > 0) {
        LogPrintf("🗳️  RANK IBD (final): blocks %d→%d | %d blocks with votes | "
                  "%d votes (+%d/-%d)\n",
                  m_ibd_first_height, m_ibd_last_height,
                  m_ibd_blocks_with_votes,
                  m_ibd_votes_accum, m_ibd_pos_accum, m_ibd_neg_accum);
        m_ibd_votes_accum = 0;
    }

    // Log total stats before closing
    if (m_db) {
        int totalVotes = 0, totalProfiles = 0;
        sqlite3_stmt *q = m_db->Prepare(
            "SELECT COUNT(*) FROM rank_votes");
        if (sqlite3_step(q) == SQLITE_ROW) {
            totalVotes = sqlite3_column_int(q, 0);
        }
        sqlite3_reset(q);

        q = m_db->Prepare("SELECT COUNT(*) FROM rank_profiles");
        if (sqlite3_step(q) == SQLITE_ROW) {
            totalProfiles = sqlite3_column_int(q, 0);
        }
        sqlite3_reset(q);

        int lastHeight = GetLastIndexedHeight();
        LogPrintf("🗳️  RANK shutdown: %d votes | %d profiles | "
                  "last height %d | flushing WAL...\n",
                  totalVotes, totalProfiles, lastHeight);
    }

    m_db.reset();
    LogPrintf("🗳️  RANK module: closed\n");
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
    addRoute(HTTPRequest::GET, "social/rescan",
             std::bind(&CRankModule::HandleRescan, this, _1, _2, _3, _4));
    addRoute(HTTPRequest::POST, "social/rescan",
             std::bind(&CRankModule::HandleRescan, this, _1, _2, _3, _4));
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

bool CRankModule::HandleRescan(const util::Ref &ctx, HTTPRequest *req,
                               const std::vector<std::string> &,
                               const api::QueryParams &qp) {
    int start = qp.GetInt("start", -1);
    int end = qp.GetInt("end", -1);

    int chainHeight;
    {
        LOCK(cs_main);
        chainHeight = ::ChainActive().Height();
    }

    if (start < 0) {
        int lastIndexed = GetLastIndexedHeight();
        start = (lastIndexed >= 0) ? lastIndexed + 1 : 0;
    }
    if (end < 0 || end > chainHeight) {
        end = chainHeight;
    }
    if (start > end) {
        UniValue result(UniValue::VOBJ);
        result.pushKV("status", "up_to_date");
        result.pushKV("lastIndexedHeight", GetLastIndexedHeight());
        result.pushKV("chainHeight", chainHeight);
        result.pushKV("votesFound", 0);
        api::WriteJSON(req, HTTP_OK, result);
        return true;
    }

    const CChainParams &params = Params();
    int votes = RescanRange(start, end, params);

    UniValue result(UniValue::VOBJ);
    result.pushKV("status", "complete");
    result.pushKV("startHeight", start);
    result.pushKV("endHeight", end);
    result.pushKV("blocksScanned", end - start + 1);
    result.pushKV("votesFound", votes);
    result.pushKV("lastIndexedHeight", GetLastIndexedHeight());
    result.pushKV("chainHeight", chainHeight);
    api::WriteJSON(req, HTTP_OK, result);
    return true;
}
