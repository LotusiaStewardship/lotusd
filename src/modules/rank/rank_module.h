// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MODULES_RANK_MODULE_H
#define BITCOIN_MODULES_RANK_MODULE_H

#include <modules/chain_module.h>
#include <sqlite/sqlite_wrapper.h>

#include <memory>
#include <mutex>

/**
 * RANK social module: parses OP_RETURN outputs with LOKAD ID "RANK"
 * (0x52414e4b) to index on-chain burn-weighted reputation votes.
 *
 * Maintains its own SQLite database (rank.sqlite) with tables for
 * individual votes, aggregated profile scores, post scores, and
 * daily deltas for trending stats.
 *
 * Exposes /api/v1/social/* HTTP endpoints.
 */
class CRankModule final : public IChainModule {
public:
    CRankModule() = default;

    std::string Name() const override;
    bool Init(const fs::path &datadir, const CChainParams &params) override;
    void ConnectBlock(const CBlock &block, const CBlockIndex *pindex,
                      const CChainParams &params) override;
    void DisconnectBlock(const CBlock &block, const CBlockIndex *pindex,
                         const CChainParams &params) override;
    void RegisterRoutes(RouteAdder addRoute) override;
    void Shutdown() override;

    /**
     * Scan a range of historical blocks for RANK transactions.
     * Called from the rescan API endpoint or during startup catch-up.
     * Reads blocks from disk via ChainActive() and feeds them through
     * the normal ConnectBlock indexing pipeline.
     * @return number of RANK votes found and indexed.
     */
    int RescanRange(int startHeight, int endHeight,
                    const CChainParams &params);

    /** Stored last-indexed height from the meta table (-1 if empty). */
    int GetLastIndexedHeight();

private:
    std::unique_ptr<CSqliteWrapper> m_db;
    std::recursive_mutex m_db_mutex;

    // IBD log accumulator -- batches vote summaries to avoid per-block spam
    int64_t m_ibd_log_last{0};
    int m_ibd_votes_accum{0};
    int m_ibd_pos_accum{0};
    int m_ibd_neg_accum{0};
    int m_ibd_blocks_with_votes{0};
    int m_ibd_first_height{0};
    int m_ibd_last_height{0};

    void CreateSchema();
    void SetMeta(const std::string &key, const std::string &value);
    std::string GetMeta(const std::string &key,
                        const std::string &fallback = "");
    void InsertExtractedVotes(const std::vector<struct ExtractedVote> &votes);

    // API handlers
    bool HandleActivity(const util::Ref &ctx, HTTPRequest *req,
                        const std::vector<std::string> &parts,
                        const api::QueryParams &qp);
    bool HandleProfiles(const util::Ref &ctx, HTTPRequest *req,
                        const std::vector<std::string> &parts,
                        const api::QueryParams &qp);
    bool HandleProfileDetail(const util::Ref &ctx, HTTPRequest *req,
                             const std::vector<std::string> &parts,
                             const api::QueryParams &qp);
    bool HandleStats(const util::Ref &ctx, HTTPRequest *req,
                     const std::vector<std::string> &parts,
                     const api::QueryParams &qp);
    bool HandleRescan(const util::Ref &ctx, HTTPRequest *req,
                      const std::vector<std::string> &parts,
                      const api::QueryParams &qp);
};

#endif // BITCOIN_MODULES_RANK_MODULE_H
