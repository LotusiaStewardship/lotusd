// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_STRATUM_STRATUMSUBMIT_H
#define BITCOIN_STRATUM_STRATUMSUBMIT_H

#include <arith_uint256.h>
#include <consensus/params.h>
#include <primitives/block.h>
#include <primitives/blockhash.h>
#include <stratum/stratumjob.h>
#include <univalue.h>

#include <cstdint>
#include <set>
#include <string>

class ChainstateManager;
class CChainParams;
class Config;

namespace stratum {

enum class ShareResult {
    ACCEPTED,
    ACCEPTED_BLOCK,
    REJECTED_LOW_DIFF,
    REJECTED_STALE,
    REJECTED_DUPLICATE,
    REJECTED_INVALID,
};

struct ShareSubmission {
    std::string workerName;
    uint64_t jobId;
    std::string extranonce2;
    std::string ntime; // 12 hex chars (6 bytes) for Lotus
    std::string nonce; // 16 hex chars (8 bytes) for Lotus
};

struct AuxPowSubmission {
    std::string workerName;
    uint64_t jobId;
    std::string auxpowHex;

    // Multi-chain Scrypt submission fields
    std::string extranonce1;
    std::string extranonce2;
    uint32_t nTime{0};
    uint32_t nNonce{0};
};

bool ParseSubmitParams(const UniValue &params, ShareSubmission &sub,
                       std::string &error);

bool ParseAuxPowSubmitParams(const UniValue &params, AuxPowSubmission &sub,
                             std::string &error);

/**
 * Reconstruct the 160-byte Lotus block header from job + extranonce + submit.
 * The coinbase is rebuilt with extranonce1+extranonce2 injected, the merkle
 * root is recomputed, and the submitted nonce/ntime are applied.
 */
CBlockHeader ReconstructLotusHeader(const StratumJob &job,
                                    const std::string &extranonce1,
                                    const ShareSubmission &sub);

/**
 * Compute the triple-SHA-256 PoW hash using the Lotus 3-layer structure.
 * Optimized: uses the precomputed layer3 hash from the job when available.
 */
BlockHash LotusTripleSha256(const CBlockHeader &header);

arith_uint256 DifficultyToTarget(double difficulty);

ShareResult ValidateShare(const StratumJob &job,
                          const std::string &extranonce1,
                          const ShareSubmission &sub,
                          double workerDifficulty,
                          const Consensus::Params &params,
                          std::set<std::string> &submittedNonces);

bool SubmitBlock(const Config &config, const StratumJob &job,
                 const std::string &extranonce1,
                 const ShareSubmission &sub,
                 ChainstateManager &chainman);

namespace StratumError {
static constexpr int JOB_NOT_FOUND = 21;
static constexpr int DUPLICATE_SHARE = 22;
static constexpr int LOW_DIFFICULTY = 23;
static constexpr int UNAUTHORIZED = 24;
static constexpr int NOT_SUBSCRIBED = 25;
} // namespace StratumError

} // namespace stratum

#endif // BITCOIN_STRATUM_STRATUMSUBMIT_H
