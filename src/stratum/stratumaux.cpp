// Copyright (c) 2025 Tobias Ruck and Alexandre Guillioud
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <stratum/stratumaux.h>

#include <arith_uint256.h>
#include <chainparams.h>
#include <config.h>
#include <consensus/merkle.h>
#include <script/script.h>
#include <hash.h>
#include <miner.h>
#include <pow/pow.h>
#include <primitives/auxpow.h>
#include <primitives/parentheader.h>
#include <streams.h>
#include <util/strencodings.h>
#include <util/translation.h>
#include <validation.h>

namespace stratum {

static std::unique_ptr<StratumAuxManager> g_globalAuxMgr;

void InitGlobalAuxManager(CChainState &chainstate, const CTxMemPool *mempool,
                          const CChainParams &params) {
    g_globalAuxMgr = std::make_unique<StratumAuxManager>(
        chainstate, mempool, params, CScript() << OP_TRUE);
}

StratumAuxManager *GetGlobalAuxManager() {
    return g_globalAuxMgr.get();
}

void StopGlobalAuxManager() {
    g_globalAuxMgr.reset();
}

StratumAuxManager::StratumAuxManager(CChainState &chainstate,
                                     const CTxMemPool *mempool,
                                     const CChainParams &params,
                                     const CScript &coinbaseScript)
    : m_chainstate(chainstate), m_mempool(mempool), m_params(params),
      m_coinbaseScript(coinbaseScript) {}

void StratumAuxManager::SetCoinbaseScript(const CScript &script) {
    LOCK(m_mutex);
    m_coinbaseScript = script;
}

bool StratumAuxManager::CreateAuxWork(AuxWorkTemplate &out,
                                      std::string &error) {
    LOCK(m_mutex);
    LOCK(cs_main);

    const CBlockIndex *pindexPrev = m_chainstate.m_chain.Tip();
    if (!pindexPrev) {
        error = "No chain tip available";
        return false;
    }

    BlockAssembler assembler(::GetConfig(), m_chainstate, m_mempool);
    auto blockTemplate = assembler.CreateNewBlock(m_coinbaseScript);
    if (!blockTemplate) {
        error = "Failed to create block template";
        return false;
    }

    CBlock &block = blockTemplate->block;

    // CreateNewBlock doesn't compute hashMerkleRoot; compute it now
    block.hashMerkleRoot = BlockMerkleRoot(block);

    // Build the StratumJob underlying this aux work
    StratumJob job;
    job.jobId = 0;
    job.height = pindexPrev->nHeight + 1;

    CBlockHeader hdr;
    hdr.nVersion = VersionWithAuxPow(block.nVersion, true);
    hdr.hashPrevBlock = block.hashPrevBlock;
    hdr.hashMerkleRoot = block.hashMerkleRoot;
    hdr.nTime = block.nTime;
    hdr.nBits = block.nBits;
    hdr.nNonce = block.nNonce;
    uint256 auxBlockHash = hdr.GetHash();

    bool fNegative, fOverflow;
    arith_uint256 target;
    target.SetCompact(block.nBits, &fNegative, &fOverflow);

    Amount coinbaseValue = Amount::zero();
    if (!block.vtx.empty() && !block.vtx[0]->vout.empty()) {
        for (const auto &txout : block.vtx[0]->vout) {
            coinbaseValue += txout.nValue;
        }
    }

    out.auxBlockHash = auxBlockHash;
    out.nChainId = AUXPOW_CHAIN_ID;
    out.prevBlockHash = block.hashPrevBlock;
    out.coinbaseValue = coinbaseValue;
    out.nBits = block.nBits;
    out.height = job.height;
    out.target = target;
    out.underlyingJob = std::move(job);

    m_pendingWork[auxBlockHash] = out;
    m_workInsertOrder.push_back(auxBlockHash);

    while (m_pendingWork.size() > 32) {
        uint256 oldest = m_workInsertOrder.front();
        m_workInsertOrder.pop_front();
        m_pendingWork.erase(oldest);
    }

    return true;
}

MergeMineCommitment StratumAuxManager::BuildCommitment(
    const uint256 &auxBlockHash,
    const std::vector<uint256> &otherAuxHashes) const {

    MergeMineCommitment commitment;

    // Build the chain merkle tree
    std::vector<uint256> leaves;
    leaves.push_back(auxBlockHash);
    for (const auto &h : otherAuxHashes) {
        leaves.push_back(h);
    }

    // Tree size must be a power of 2
    uint32_t treeSize = 1;
    uint32_t merkleHeight = 0;
    while (treeSize < leaves.size()) {
        treeSize <<= 1;
        merkleHeight++;
    }

    // Pad to power-of-2 with zero hashes
    while (leaves.size() < treeSize) {
        leaves.push_back(uint256());
    }

    // Find the correct nonce that places Dogecoin at the expected index
    // using the CalcExpectedMerkleTreeIndex LCG
    uint32_t dogeIndex = 0;
    uint32_t nonce = 0;
    if (merkleHeight > 0) {
        for (nonce = 0; nonce < 0xFFFFFFFF; nonce++) {
            uint32_t idx = CalcExpectedMerkleTreeIndex(nonce, AUXPOW_CHAIN_ID,
                                                       merkleHeight);
            if (idx < treeSize) {
                dogeIndex = idx;
                break;
            }
        }
        // Place the Doge hash at the correct index
        if (dogeIndex != 0) {
            std::swap(leaves[0], leaves[dogeIndex]);
        }
    }

    commitment.nTreeSize = treeSize;
    commitment.nMergeMineNonce = nonce;
    commitment.nChainIndex = dogeIndex;

    // Compute chain merkle branch for the Doge leaf
    if (merkleHeight == 0) {
        commitment.chainMerkleRoot = auxBlockHash;
    } else {
        // Build the merkle tree and extract the branch for dogeIndex
        std::vector<uint256> level = leaves;
        size_t index = dogeIndex;
        while (level.size() > 1) {
            size_t siblingIdx = index ^ 1;
            if (siblingIdx < level.size()) {
                commitment.chainMerkleBranch.push_back(level[siblingIdx]);
            } else {
                commitment.chainMerkleBranch.push_back(level[index]);
            }
            std::vector<uint256> nextLevel;
            for (size_t i = 0; i < level.size(); i += 2) {
                uint256 left = level[i];
                uint256 right = (i + 1 < level.size()) ? level[i + 1] : left;
                nextLevel.push_back(Hash(Span(left), Span(right)));
            }
            level = nextLevel;
            index /= 2;
        }
        commitment.chainMerkleRoot = level[0];
    }

    // Build the coinbase payload: MERGE_MINE_PREFIX + root (big-endian) +
    // treeSize (LE) + nonce (LE)
    commitment.coinbasePayload.clear();
    commitment.coinbasePayload.insert(commitment.coinbasePayload.end(),
                                       MERGE_MINE_PREFIX.begin(),
                                       MERGE_MINE_PREFIX.end());

    // Root hash in big-endian (reversed)
    uint256 rootBE = commitment.chainMerkleRoot;
    std::reverse(rootBE.begin(), rootBE.end());
    commitment.coinbasePayload.insert(commitment.coinbasePayload.end(),
                                       rootBE.begin(), rootBE.end());

    // Tree size (4 bytes, little-endian)
    uint32_t ts = commitment.nTreeSize;
    commitment.coinbasePayload.push_back(ts & 0xff);
    commitment.coinbasePayload.push_back((ts >> 8) & 0xff);
    commitment.coinbasePayload.push_back((ts >> 16) & 0xff);
    commitment.coinbasePayload.push_back((ts >> 24) & 0xff);

    // Merge nonce (4 bytes, little-endian)
    uint32_t mn = commitment.nMergeMineNonce;
    commitment.coinbasePayload.push_back(mn & 0xff);
    commitment.coinbasePayload.push_back((mn >> 8) & 0xff);
    commitment.coinbasePayload.push_back((mn >> 16) & 0xff);
    commitment.coinbasePayload.push_back((mn >> 24) & 0xff);

    return commitment;
}

bool StratumAuxManager::AssembleAuxPow(
    const AuxWorkTemplate &work,
    const AuxPowSubmission &submission,
    std::shared_ptr<CAuxPow> &out,
    std::string &error) const {

    auto auxpow = std::make_shared<CAuxPow>();

    if (submission.coinbaseMerkleIndex != 0) {
        error = "AuxPow coinbase must be at merkle index 0";
        return false;
    }

    if (VersionChainId(submission.parentHeader.nVersion) == AUXPOW_CHAIN_ID) {
        error = "AuxPow parent has our chain ID";
        return false;
    }

    auxpow->coinbaseTx = submission.parentCoinbaseTx;
    auxpow->hashBlock = submission.parentBlockHash;
    auxpow->vMerkleBranch = submission.coinbaseMerkleBranch;
    auxpow->nIndex = submission.coinbaseMerkleIndex;

    MergeMineCommitment commitment = BuildCommitment(work.auxBlockHash);
    auxpow->vChainMerkleBranch = commitment.chainMerkleBranch;
    auxpow->nChainIndex = commitment.nChainIndex;

    auxpow->parentBlock = submission.parentHeader;

    const Consensus::Params &consensus = m_params.GetConsensus();
    std::string checkError;
    if (!auxpow->CheckAuxBlockHash(work.auxBlockHash,
                                    AUXPOW_CHAIN_ID, consensus,
                                    checkError)) {
        error = checkError;
        return false;
    }

    out = std::move(auxpow);
    return true;
}

bool StratumAuxManager::ValidateParentPow(
    const CParentBlockHeader &parentHeader, uint32_t nBits,
    const Consensus::Params &params) const {
    BlockHash powHash = parentHeader.GetPowHash();
    return CheckProofOfWork(powHash, nBits, params);
}

bool StratumAuxManager::SubmitAuxBlock(const AuxWorkTemplate &work,
                                        std::shared_ptr<CAuxPow> auxpow,
                                        ChainstateManager &chainman) {
    auto block = std::make_shared<CBlock>();
    // Rebuild block from the aux work template
    block->nVersion = VersionWithAuxPow(work.underlyingJob.height, true);
    block->hashPrevBlock = work.prevBlockHash;
    block->nBits = work.nBits;

    // Attach the AuxPoW proof as metadata
    block->SetAuxPow(std::move(auxpow));
    block->hashMerkleRoot = BlockMerkleRoot(*block);

    bool newBlock = false;
    return chainman.ProcessNewBlock(::GetConfig(), block,
                                    /*force_processing=*/true,
                                    &newBlock);
}

std::optional<AuxWorkTemplate>
StratumAuxManager::GetWork(const uint256 &auxBlockHash) const {
    LOCK(m_mutex);
    auto it = m_pendingWork.find(auxBlockHash);
    if (it == m_pendingWork.end()) {
        return std::nullopt;
    }
    return it->second;
}

void StratumAuxManager::RemoveWork(const uint256 &auxBlockHash) {
    LOCK(m_mutex);
    m_pendingWork.erase(auxBlockHash);
}

void StratumAuxManager::PruneWork(size_t keepCount) {
    LOCK(m_mutex);
    while (m_pendingWork.size() > keepCount && !m_workInsertOrder.empty()) {
        uint256 oldest = m_workInsertOrder.front();
        m_workInsertOrder.pop_front();
        m_pendingWork.erase(oldest);
    }
}

} // namespace stratum
