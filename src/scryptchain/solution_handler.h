// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRYPTCHAIN_SOLUTION_HANDLER_H
#define BITCOIN_SCRYPTCHAIN_SOLUTION_HANDLER_H

#include <scryptchain/template_builder.h>

#include <cstdint>
#include <memory>
#include <string>

class ChainstateManager;
class Config;

namespace scryptchain {

class ScryptNetworkManager;

struct ScryptSolution {
    std::string extranonce1;
    std::string extranonce2;
    uint32_t nTime;
    uint32_t nNonce;
};

struct SolutionResult {
    bool ltcFound{false};
    bool dogeFound{false};
    bool lotusFound{false};
    int ltcHeight{0};
    int dogeHeight{0};
    int lotusHeight{0};
};

class ScryptSolutionHandler {
public:
    ScryptSolutionHandler(const Config &nodeConfig,
                          ChainstateManager &chainman);
    ~ScryptSolutionHandler();

    void SetLtcNetwork(ScryptNetworkManager *ltcNet) { m_ltcNet = ltcNet; }
    void SetDogeNetwork(ScryptNetworkManager *dogeNet) { m_dogeNet = dogeNet; }

    SolutionResult ProcessScryptSolution(
        std::shared_ptr<ScryptBlockTemplate> ltcTemplate,
        std::shared_ptr<ScryptBlockTemplate> dogeTemplate,
        const uint256 &lotusAuxHash, const ScryptSolution &solution);

private:
    bool TrySubmitLtc(ScryptBlockTemplate &tmpl,
                      const CParentBlockHeader &solvedHeader);
    bool TrySubmitDoge(ScryptBlockTemplate &dogeTemplate,
                       ScryptBlockTemplate &ltcTemplate,
                       const CParentBlockHeader &solvedHeader,
                       const CMutableTransaction &ltcCoinbase);
    bool TrySubmitLotus(ScryptBlockTemplate &ltcTemplate,
                        const CParentBlockHeader &solvedHeader,
                        const CMutableTransaction &ltcCoinbase,
                        const uint256 &lotusAuxHash);

    const Config &m_nodeConfig;
    ChainstateManager &m_chainman;

    ScryptNetworkManager *m_ltcNet{nullptr};
    ScryptNetworkManager *m_dogeNet{nullptr};
};

} // namespace scryptchain

#endif // BITCOIN_SCRYPTCHAIN_SOLUTION_HANDLER_H
