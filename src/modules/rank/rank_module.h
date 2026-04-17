// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MODULES_RANK_MODULE_H
#define BITCOIN_MODULES_RANK_MODULE_H

#include <modules/chain_module.h>
#include <sqlite/sqlite_wrapper.h>

#include <memory>

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

private:
    std::unique_ptr<CSqliteWrapper> m_db;

    void CreateSchema();

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
};

#endif // BITCOIN_MODULES_RANK_MODULE_H
