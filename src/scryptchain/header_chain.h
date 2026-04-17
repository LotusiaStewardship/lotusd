// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRYPTCHAIN_HEADER_CHAIN_H
#define BITCOIN_SCRYPTCHAIN_HEADER_CHAIN_H

#include <arith_uint256.h>
#include <chain.h>
#include <primitives/blockhash.h>
#include <primitives/parentheader.h>
#include <scryptchain/chain_params.h>
#include <uint256.h>

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

class CSqliteWrapper;

namespace scryptchain {

struct ScryptHeaderIndex {
    BlockHash hash;
    BlockHash prevHash;
    int height{0};
    uint32_t nBits{0};
    uint32_t nTime{0};
    uint32_t nNonce{0};
    int32_t nVersion{0};
    uint256 hashMerkleRoot;
    arith_uint256 nChainWork;
    ScryptHeaderIndex *pprev{nullptr};

    bool IsAuxPow() const {
        return (nVersion >> 8) & 1;
    }

    CParentBlockHeader ToParentHeader() const {
        CParentBlockHeader hdr;
        hdr.nVersion = nVersion;
        hdr.hashPrevBlock = prevHash;
        hdr.hashMerkleRoot = hashMerkleRoot;
        hdr.nTime = nTime;
        hdr.nBits = nBits;
        hdr.nNonce = nNonce;
        return hdr;
    }
};

class ScryptHeaderDB {
public:
    ScryptHeaderDB(const std::string &chainName, const fs::path &dataDir);
    ~ScryptHeaderDB();

    bool LoadAll(std::unordered_map<BlockHash, std::unique_ptr<ScryptHeaderIndex>,
                                    BlockHasher> &headers,
                 BlockHash &tipHash, std::string &error);

    bool WriteHeader(const ScryptHeaderIndex &hdr, std::string &error);
    bool WriteBatch(const std::vector<ScryptHeaderIndex> &headers,
                    std::string &error);
    bool WriteTip(const BlockHash &hash, std::string &error);

private:
    std::unique_ptr<CSqliteWrapper> m_db;
    std::string m_tableName;
    std::string m_metaTable;
};

class ScryptHeaderValidator {
public:
    static bool ValidateHeader(const CParentBlockHeader &hdr,
                               const ScryptHeaderIndex *prev,
                               const ScryptChainParams &params,
                               int checkpointHeight,
                               std::string &error);

    static uint32_t GetNextWorkRequired(const ScryptHeaderIndex *tip,
                                        const ScryptChainParams &params);

private:
    static uint32_t GetNextWorkRequiredBitcoin(const ScryptHeaderIndex *tip,
                                               const ScryptChainParams &params);
    static uint32_t GetNextWorkRequiredDigiShield(
        const ScryptHeaderIndex *tip, const ScryptChainParams &params);
};

class ScryptHeaderChain {
public:
    ScryptHeaderChain(const ScryptChainParams &params, const fs::path &dataDir);
    ~ScryptHeaderChain();

    bool Initialize(std::string &error);

    bool AcceptHeader(const CParentBlockHeader &hdr, std::string &error);
    bool AcceptHeaders(const std::vector<CParentBlockHeader> &headers,
                       std::string &error);

    const ScryptHeaderIndex *GetTip() const;
    int GetHeight() const;
    bool IsSynced() const;
    const ScryptHeaderIndex *GetIndex(const BlockHash &hash) const;
    const ScryptChainParams &GetParams() const { return m_params; }

    std::vector<BlockHash> GetBlockLocator() const;
    int GetHighestCheckpoint() const;

private:
    const ScryptChainParams &m_params;
    std::unique_ptr<ScryptHeaderDB> m_db;

    mutable std::mutex m_cs;
    std::unordered_map<BlockHash, std::unique_ptr<ScryptHeaderIndex>,
                       BlockHasher>
        m_headers;
    ScryptHeaderIndex *m_tip{nullptr};
    int m_highestCheckpoint{0};

    void LinkHeaders();
    arith_uint256 GetBlockProof(uint32_t nBits) const;
};

} // namespace scryptchain

#endif // BITCOIN_SCRYPTCHAIN_HEADER_CHAIN_H
