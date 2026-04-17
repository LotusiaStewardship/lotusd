// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stratum/stratumsubmit.h>

#include <arith_uint256.h>
#include <chainparams.h>
#include <config.h>
#include <consensus/merkle.h>
#include <hash.h>
#include <pow/pow.h>
#include <streams.h>
#include <util/strencodings.h>
#include <validation.h>

namespace stratum {

bool ParseSubmitParams(const UniValue &params, ShareSubmission &sub,
                       std::string &error) {
    if (!params.isArray() || params.size() < 5) {
        error = "mining.submit requires 5 parameters";
        return false;
    }

    sub.workerName = params[0].get_str();

    std::string jobIdStr = params[1].get_str();
    try {
        sub.jobId = std::stoull(jobIdStr, nullptr, 16);
    } catch (...) {
        error = "Invalid job ID";
        return false;
    }

    sub.extranonce2 = params[2].get_str();
    sub.ntime = params[3].get_str();
    sub.nonce = params[4].get_str();

    // Lotus: extranonce2 = 16 hex (8 bytes), ntime = 12 hex (6 bytes),
    // nonce = 16 hex (8 bytes)
    if (!IsHex(sub.extranonce2) || sub.extranonce2.size() != 16) {
        error = "Invalid extranonce2 (expected 16 hex chars / 8 bytes)";
        return false;
    }
    if (!IsHex(sub.ntime) || sub.ntime.size() != 12) {
        error = "Invalid ntime (expected 12 hex chars / 6 bytes)";
        return false;
    }
    if (!IsHex(sub.nonce) || sub.nonce.size() != 16) {
        error = "Invalid nonce (expected 16 hex chars / 8 bytes)";
        return false;
    }

    return true;
}

bool ParseAuxPowSubmitParams(const UniValue &params, AuxPowSubmission &sub,
                             std::string &error) {
    if (!params.isArray() || params.size() < 3) {
        error = "mining.submit.aux requires 3 parameters";
        return false;
    }

    sub.workerName = params[0].get_str();

    std::string jobIdStr = params[1].get_str();
    try {
        sub.jobId = std::stoull(jobIdStr, nullptr, 16);
    } catch (...) {
        error = "Invalid job ID";
        return false;
    }

    sub.auxpowHex = params[2].get_str();
    if (!IsHex(sub.auxpowHex)) {
        error = "Invalid auxpow hex data";
        return false;
    }

    return true;
}

CBlockHeader ReconstructLotusHeader(const StratumJob &job,
                                    const std::string &extranonce1,
                                    const ShareSubmission &sub) {
    // Reconstruct the full coinbase transaction
    std::string fullCoinbaseHex =
        job.coinbase1 + extranonce1 + sub.extranonce2 + job.coinbase2;
    std::vector<uint8_t> coinbaseBytes = ParseHex(fullCoinbaseHex);

    CDataStream ss(coinbaseBytes, SER_NETWORK, PROTOCOL_VERSION);
    CTransaction coinbaseTx(deserialize, ss);

    // Compute merkle root
    uint256 merkleRoot = coinbaseTx.GetHash();
    for (const auto &branchHex : job.merkleBranches) {
        uint256 branchHash;
        branchHash.SetHex(branchHex);
        merkleRoot = Hash(merkleRoot, branchHash);
    }

    // Build the 160-byte Lotus header
    CBlockHeader header;
    header.hashPrevBlock = job.block->hashPrevBlock;
    header.nBits = job.nBitsRaw;

    // Parse 6-byte ntime from submission (little-endian hex)
    std::vector<uint8_t> ntimeBytes = ParseHex(sub.ntime);
    block_time_t vTime;
    for (size_t i = 0; i < 6 && i < ntimeBytes.size(); i++) {
        vTime[i] = ntimeBytes[i];
    }
    header.vTime = vTime;

    header.nReserved = job.block->nReserved;

    // Parse 8-byte nonce from submission (little-endian hex)
    std::vector<uint8_t> nonceBytes = ParseHex(sub.nonce);
    uint64_t nonce = 0;
    for (size_t i = 0; i < 8 && i < nonceBytes.size(); i++) {
        nonce |= (uint64_t)nonceBytes[i] << (i * 8);
    }
    header.nNonce = nonce;

    header.nHeaderVersion = job.block->nHeaderVersion;
    header.vSize = job.block->vSize;
    header.nHeight = job.block->nHeight;
    header.hashEpochBlock = job.block->hashEpochBlock;
    header.hashMerkleRoot = merkleRoot;
    header.hashExtendedMetadata = job.block->hashExtendedMetadata;

    return header;
}

BlockHash LotusTripleSha256(const CBlockHeader &header) {
    return header.GetHash();
}

arith_uint256 DifficultyToTarget(double difficulty) {
    // Difficulty 1 target for SHA-256:
    // 0x00000000ffff0000000000000000000000000000000000000000000000000000
    arith_uint256 target;
    target.SetCompact(0x1d00ffff);

    if (difficulty <= 0) {
        return target;
    }

    if (difficulty >= 1.0) {
        uint64_t diffInt = static_cast<uint64_t>(difficulty);
        if (diffInt > 0 && static_cast<double>(diffInt) == difficulty) {
            return target / diffInt;
        }
    }

    uint64_t scale = 1000000;
    arith_uint256 scaled = target * scale;
    uint64_t diffScaled = static_cast<uint64_t>(difficulty * scale);
    if (diffScaled == 0) {
        diffScaled = 1;
    }
    return scaled / diffScaled;
}

ShareResult ValidateShare(const StratumJob &job,
                          const std::string &extranonce1,
                          const ShareSubmission &sub,
                          double workerDifficulty,
                          const Consensus::Params &params,
                          std::set<std::string> &submittedNonces) {
    std::string dupeKey = strprintf("%x:%s:%s:%s", sub.jobId, sub.extranonce2,
                                    sub.nonce, sub.ntime);
    if (submittedNonces.count(dupeKey)) {
        return ShareResult::REJECTED_DUPLICATE;
    }

    CBlockHeader header = ReconstructLotusHeader(job, extranonce1, sub);
    BlockHash powHash = LotusTripleSha256(header);

    arith_uint256 hashValue = UintToArith256(powHash);

    arith_uint256 networkTarget;
    networkTarget.SetCompact(job.nBitsRaw);

    submittedNonces.insert(dupeKey);

    if (hashValue <= networkTarget) {
        return ShareResult::ACCEPTED_BLOCK;
    }

    arith_uint256 workerTarget = DifficultyToTarget(workerDifficulty);
    if (hashValue <= workerTarget) {
        return ShareResult::ACCEPTED;
    }

    return ShareResult::REJECTED_LOW_DIFF;
}

bool SubmitBlock(const Config &config, const StratumJob &job,
                 const std::string &extranonce1,
                 const ShareSubmission &sub,
                 ChainstateManager &chainman) {
    std::string fullCoinbaseHex =
        job.coinbase1 + extranonce1 + sub.extranonce2 + job.coinbase2;
    std::vector<uint8_t> coinbaseBytes = ParseHex(fullCoinbaseHex);

    CDataStream ss(coinbaseBytes, SER_NETWORK, PROTOCOL_VERSION);
    CMutableTransaction coinbaseMtx;
    ss >> coinbaseMtx;

    auto block = std::make_shared<CBlock>(*job.block);
    block->vtx[0] = MakeTransactionRef(std::move(coinbaseMtx));
    block->hashMerkleRoot = BlockMerkleRoot(*block);

    // Set ntime (6 bytes)
    std::vector<uint8_t> ntimeBytes = ParseHex(sub.ntime);
    block_time_t vTime;
    for (size_t i = 0; i < 6 && i < ntimeBytes.size(); i++) {
        vTime[i] = ntimeBytes[i];
    }
    block->vTime = vTime;

    // Set nonce (8 bytes)
    std::vector<uint8_t> nonceBytes = ParseHex(sub.nonce);
    uint64_t nonce = 0;
    for (size_t i = 0; i < 8 && i < nonceBytes.size(); i++) {
        nonce |= (uint64_t)nonceBytes[i] << (i * 8);
    }
    block->nNonce = nonce;

    bool fNewBlock = false;
    return chainman.ProcessNewBlock(config, block, true, &fNewBlock);
}

} // namespace stratum
