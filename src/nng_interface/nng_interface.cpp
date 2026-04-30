// Copyright (c) 2021 The Logos Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <array>
#include <atomic>
#include <optional>

#include <blockdb.h>
#include <chainparams.h>
#include <config.h>
#include <consensus/consensus.h>
#include <consensus/validation.h>
#include <consensus/merkle.h>
#include <hash.h>
#include <logging.h>
#include <miner.h>
#include <net.h>
#include <policy/policy.h>
#include <node/coin.h>
#include <node/context.h>
#include <node/transaction.h>
#include <node/ui_interface.h>
#include <streams.h>
#include <timedata.h>
#include <undo.h>
#include <univalue.h>
#include <util/strencodings.h>
#include <util/time.h>
#include <util/translation.h>
#include <validation.h>
#include <validationinterface.h>

#include <nng/nng.h>
#include <nng/protocol/pubsub0/pub.h>
#include <nng/protocol/reqrep0/rep.h>

#include "nng_interface_generated.h"
#include <nng_interface/nng_interface.h>

enum class NngRpcWorkerState {
    UNINIT,
    RECV,
    SEND,
    CLOSED,
};

class NngRpcServer;

#define NNG_TRY_LOG(call)                                                      \
    do {                                                                       \
        int rv = (call);                                                       \
        if (rv != 0) {                                                         \
            LogPrintf("NNG Error: %s (at %s:%d: %s)\n", nng_strerror(rv),      \
                      __FILE__, __LINE__, #call);                              \
            return;                                                            \
        }                                                                      \
    } while (false)

#define NNG_TRY_ERROR(call, format_str)                                        \
    do {                                                                       \
        int rv = (call);                                                       \
        if (rv != 0) {                                                         \
            return InitError(strprintf(_(format_str), nng_strerror(rv)));      \
        }                                                                      \
    } while (false)

enum class NngRpcErrorCode {
    NO_RPC_ERROR = 0,
    NNG_ERROR,
    INVALID_FLATBUFFER_ENCODING,
    UNKNOWN_RPC_METHOD,
    BLOCK_ID_UNKNOWN_TYPE,
    BLOCK_NOT_FOUND,
    BLOCK_DATA_CORRUPTED,
    INVALID_BLOCK_SLICE,
    INVALID_MINING_REQUEST,
    MINING_TEMPLATE_BUILD_FAILED,
    MINING_SUBMIT_DECODE_FAILED,
};
struct RpcResult {
    NngRpcErrorCode error_code;
    std::vector<uint8_t> data = std::vector<uint8_t>();
};

std::string ErrorMsg(NngRpcErrorCode code) {
    switch (code) {
        case NngRpcErrorCode::NO_RPC_ERROR:
            return "No error";
        case NngRpcErrorCode::INVALID_FLATBUFFER_ENCODING:
            return "Invalid flatbuffer encoding";
        case NngRpcErrorCode::UNKNOWN_RPC_METHOD:
            return "Unknown RPC method";
        case NngRpcErrorCode::BLOCK_ID_UNKNOWN_TYPE:
            return "Unknown block ID type, only blockhash and block height "
                   "allowed";
        case NngRpcErrorCode::BLOCK_NOT_FOUND:
            return "Block not found";
        case NngRpcErrorCode::BLOCK_DATA_CORRUPTED:
            return "Block data corrupted";
        case NngRpcErrorCode::INVALID_BLOCK_SLICE:
            return "Invalid block slice";
        case NngRpcErrorCode::INVALID_MINING_REQUEST:
            return "Invalid mining request";
        case NngRpcErrorCode::MINING_TEMPLATE_BUILD_FAILED:
            return "Failed to build mining template";
        case NngRpcErrorCode::MINING_SUBMIT_DECODE_FAILED:
            return "Failed to decode mined block";
        default:
            return "Unknown error";
    }
}

class NngRpcServer;

class NngRpcWorker {
    NngRpcWorkerState m_state;
    nng_aio *m_aio;
    nng_ctx m_ctx;
    NngRpcServer *m_server;

    void HandleCallback();

public:
    NngRpcWorker();

    void Init(nng_socket sock, NngRpcServer *server);
    void Shutdown();
    static void Callback(void *arg);
};

class NngRpcServer {
    static const size_t NUM_WORKERS = 64;

    nng_socket m_sock;
    std::vector<NngRpcWorker> m_workers;
    const Consensus::Params &m_consensus;
    NodeContext &m_node;

    NngRpcErrorCode GetBlock(flatbuffers::FlatBufferBuilder &builder,
                             const NngInterface::GetBlockRequest *request);

    NngRpcErrorCode
    GetBlockRange(flatbuffers::FlatBufferBuilder &builder,
                  const NngInterface::GetBlockRangeRequest *request);

    NngRpcErrorCode
    GetBlockSlice(flatbuffers::FlatBufferBuilder &builder,
                  const NngInterface::GetBlockSliceRequest *request);

    NngRpcErrorCode
    GetUndoSlice(flatbuffers::FlatBufferBuilder &builder,
                 const NngInterface::GetUndoSliceRequest *request);

    NngRpcErrorCode GetMempool(flatbuffers::FlatBufferBuilder &builder,
                               const NngInterface::GetMempoolRequest *request);

    NngRpcErrorCode
    GetMiningTemplate(flatbuffers::FlatBufferBuilder &builder,
                      const NngInterface::GetMiningTemplateRequest *request);

    NngRpcErrorCode
    SubmitMinedBlock(flatbuffers::FlatBufferBuilder &builder,
                     const NngInterface::SubmitMinedBlockRequest *request);

    NngRpcErrorCode ValidateMinedBlockProposal(
        flatbuffers::FlatBufferBuilder &builder,
        const NngInterface::ValidateMinedBlockProposalRequest *request);

    NngRpcErrorCode
    GetMiningStatus(flatbuffers::FlatBufferBuilder &builder,
                    const NngInterface::GetMiningStatusRequest *request);

    NngRpcErrorCode SendRawTransaction(
        flatbuffers::FlatBufferBuilder &builder,
        const NngInterface::SendRawTransactionRequest *request);

public:
    NngRpcServer(const Consensus::Params &consensus, NodeContext &node)
        : m_consensus(consensus), m_node(node) {}

    NngRpcErrorCode HandleMsg(flatbuffers::FlatBufferBuilder &builder,
                              nng_msg *incoming_msg);
    bool Listen(const std::string &rpc_url);
    void Shutdown() {
        for (NngRpcWorker &worker : m_workers) {
            worker.Shutdown();
        }
        nng_close(m_sock);
    }
};

bool NngRpcServer::Listen(const std::string &rpc_url) {
    NNG_TRY_ERROR(nng_rep0_open(&m_sock), "Failed opening NNG rep0 socket: %s");
    std::string listen_failure_msg =
        strprintf("Failed listening on -nngrpc=%s: %%s", rpc_url);
    NNG_TRY_ERROR(nng_listen(m_sock, rpc_url.c_str(), NULL, 0),
                  listen_failure_msg.c_str());
    m_workers.resize(NUM_WORKERS);
    for (NngRpcWorker &worker : m_workers) {
        worker.Init(m_sock, this);
    }
    LogPrintf("NNG interface: RPC server listening at %s\n", rpc_url);
    return true;
}

NngRpcWorker::NngRpcWorker() {
    m_state = NngRpcWorkerState::UNINIT;
}

void NngRpcWorker::Init(nng_socket sock, NngRpcServer *server) {
    m_server = server;
    NNG_TRY_LOG(nng_aio_alloc(&m_aio, NngRpcWorker::Callback, this));
    NNG_TRY_LOG(nng_ctx_open(&m_ctx, sock));
    m_state = NngRpcWorkerState::RECV;
    nng_ctx_recv(m_ctx, m_aio);
}

void NngRpcWorker::Shutdown() {
    m_state = NngRpcWorkerState::CLOSED;
    NNG_TRY_LOG(nng_ctx_close(m_ctx));
    nng_aio_free(m_aio);
}

void NngRpcWorker::Callback(void *arg) {
    NngRpcWorker *worker = (NngRpcWorker *)arg;
    worker->HandleCallback();
}

void NngRpcWorker::HandleCallback() {
    switch (m_state) {
        case NngRpcWorkerState::UNINIT:
            LogPrintf("Error: Worker in state UNINIT\n");
            break;
        case NngRpcWorkerState::RECV: {
            NNG_TRY_LOG(nng_aio_result(m_aio));
            nng_msg *incoming_msg = nng_aio_get_msg(m_aio);
            flatbuffers::FlatBufferBuilder fbb;
            NngRpcErrorCode error_code = m_server->HandleMsg(fbb, incoming_msg);
            flatbuffers::FlatBufferBuilder result_fbb(fbb.GetSize() + 256);
            if (error_code == NngRpcErrorCode::NO_RPC_ERROR) {
                result_fbb.Finish(NngInterface::CreateRpcResult(
                    result_fbb, true, 0, result_fbb.CreateString(""),
                    result_fbb.CreateVector(fbb.GetBufferPointer(),
                                            fbb.GetSize())));
            } else {
                result_fbb.Finish(NngInterface::CreateRpcResult(
                    result_fbb, false, int32_t(error_code),
                    result_fbb.CreateString(ErrorMsg(error_code))));
            }
            nng_msg *outgoing_msg;
            NNG_TRY_LOG(nng_msg_alloc(&outgoing_msg, result_fbb.GetSize()));
            memcpy(nng_msg_body(outgoing_msg),
                   (void *)result_fbb.GetBufferPointer(), result_fbb.GetSize());
            nng_aio_set_msg(m_aio, outgoing_msg);
            m_state = NngRpcWorkerState::SEND;
            nng_ctx_send(m_ctx, m_aio);
            break;
        }
        case NngRpcWorkerState::SEND: {
            NNG_TRY_LOG(nng_aio_result(m_aio));
            m_state = NngRpcWorkerState::RECV;
            nng_ctx_recv(m_ctx, m_aio);
            break;
        }
        case NngRpcWorkerState::CLOSED: {
            break;
        }
    }
}

NngRpcErrorCode NngRpcServer::HandleMsg(flatbuffers::FlatBufferBuilder &fbb,
                                        nng_msg *incoming_msg) {
    flatbuffers::Verifier verifier((uint8_t *)nng_msg_body(incoming_msg),
                                   nng_msg_len(incoming_msg));
    if (!verifier.VerifyBuffer<NngInterface::RpcCall>()) {
        return NngRpcErrorCode::INVALID_FLATBUFFER_ENCODING;
    }
    const NngInterface::RpcCall *rpc =
        flatbuffers::GetRoot<NngInterface::RpcCall>(nng_msg_body(incoming_msg));
    switch (rpc->rpc_type()) {
        case NngInterface::RpcRequest_GetBlockRequest: {
            return GetBlock(fbb, rpc->rpc_as_GetBlockRequest());
        }
        case NngInterface::RpcRequest_GetBlockRangeRequest: {
            return GetBlockRange(fbb, rpc->rpc_as_GetBlockRangeRequest());
        }
        case NngInterface::RpcRequest_GetBlockSliceRequest: {
            return GetBlockSlice(fbb, rpc->rpc_as_GetBlockSliceRequest());
        }
        case NngInterface::RpcRequest_GetUndoSliceRequest: {
            return GetUndoSlice(fbb, rpc->rpc_as_GetUndoSliceRequest());
        }
        case NngInterface::RpcRequest_GetMempoolRequest: {
            return GetMempool(fbb, rpc->rpc_as_GetMempoolRequest());
        }
        case NngInterface::RpcRequest_GetMiningTemplateRequest: {
            return GetMiningTemplate(fbb,
                                     rpc->rpc_as_GetMiningTemplateRequest());
        }
        case NngInterface::RpcRequest_SubmitMinedBlockRequest: {
            return SubmitMinedBlock(fbb,
                                    rpc->rpc_as_SubmitMinedBlockRequest());
        }
        case NngInterface::RpcRequest_ValidateMinedBlockProposalRequest: {
            return ValidateMinedBlockProposal(
                fbb, rpc->rpc_as_ValidateMinedBlockProposalRequest());
        }
        case NngInterface::RpcRequest_GetMiningStatusRequest: {
            return GetMiningStatus(fbb,
                                   rpc->rpc_as_GetMiningStatusRequest());
        }
        case NngInterface::RpcRequest_SendRawTransactionRequest: {
            return SendRawTransaction(fbb,
                                      rpc->rpc_as_SendRawTransactionRequest());
        }
        default:
            return NngRpcErrorCode::UNKNOWN_RPC_METHOD;
    }
}

template <typename T>
NngRpcErrorCode GetBlockIndex(const T *request, CBlockIndex *&block_index) {
    switch (request->block_id_type()) {
        case NngInterface::BlockIdentifier_Height: {
            int32_t height = request->block_id_as_Height()->height();
            block_index = ::ChainActive().Tip()->GetAncestor(height);
            break;
        }
        case NngInterface::BlockIdentifier_Hash: {
            const NngInterface::Hash *hash =
                request->block_id_as_Hash()->hash();
            const std::vector<uint8_t> blockhash(hash->data()->begin(),
                                                 hash->data()->end());
            LOCK(cs_main);
            block_index = LookupBlockIndex(BlockHash(uint256(blockhash)));
            break;
        }
        default:
            return NngRpcErrorCode::BLOCK_ID_UNKNOWN_TYPE;
    }
    if (!block_index) {
        return NngRpcErrorCode::BLOCK_NOT_FOUND;
    }
    return NngRpcErrorCode::NO_RPC_ERROR;
}

NngInterface::Hash CreateFbsHash(const uint8_t *data) {
    std::array<uint8_t, 32> array;
    memcpy(array.data(), data, array.size());
    return NngInterface::Hash(flatbuffers::span<uint8_t, 32>{array});
}

flatbuffers::Offset<NngInterface::BlockHash>
CreateFbsBlockHash(flatbuffers::FlatBufferBuilder &fbb,
                   const BlockHash &blockhash) {
    NngInterface::Hash hash = CreateFbsHash(blockhash.data());
    return NngInterface::CreateBlockHash(fbb, &hash);
}

flatbuffers::Offset<NngInterface::BlockHeader>
CreateFbsBlockHeader(flatbuffers::FlatBufferBuilder &fbb,
                     const CBlockHeader &header) {
    CDataStream raw_header(SER_NETWORK, PROTOCOL_VERSION);
    raw_header << header;
    return NngInterface::CreateBlockHeader(
        fbb, fbb.CreateVector((uint8_t *)raw_header.data(), raw_header.size()),
        CreateFbsBlockHash(fbb, header.GetHash()),
        CreateFbsBlockHash(fbb, header.hashPrevBlock), header.nBits,
        header.GetBlockTime());
}

flatbuffers::Offset<NngInterface::TxId>
CreateFbsTxId(flatbuffers::FlatBufferBuilder &fbb, const TxId &txid) {
    NngInterface::Hash hash = CreateFbsHash(txid.data());
    return NngInterface::CreateTxId(fbb, &hash);
}

flatbuffers::Offset<NngInterface::TxOut>
CreateFbsTxOut(flatbuffers::FlatBufferBuilder &fbb,
               const CTxOut &spent_output) {
    return NngInterface::CreateTxOut(
        fbb, spent_output.nValue / Amount::satoshi(),
        fbb.CreateVector(spent_output.scriptPubKey.data(),
                         spent_output.scriptPubKey.size()));
}

flatbuffers::Offset<NngInterface::Coin>
CreateFbsCoin(flatbuffers::FlatBufferBuilder &fbb, const Coin &spent_coin) {
    const int32_t nHeight =
        spent_coin.GetHeight() == 0x7fff'ffff ? -1 : spent_coin.GetHeight();
    return NngInterface::CreateCoin(fbb,
                                    CreateFbsTxOut(fbb, spent_coin.GetTxOut()),
                                    spent_coin.IsCoinBase(), nHeight);
}

flatbuffers::Offset<
    flatbuffers::Vector<flatbuffers::Offset<NngInterface::Coin>>>
CreateFbsSpentCoins(flatbuffers::FlatBufferBuilder &fbb,
                    std::optional<const std::vector<Coin> *> spent_coins,
                    size_t &nUndoPos) {
    if (!spent_coins) {
        return 0;
    }
    std::vector<flatbuffers::Offset<NngInterface::Coin>> coins;
    for (const Coin &coin : **spent_coins) {
        nUndoPos +=
            GetSerializeSize(Using<TxInUndoFormatter>(coin), PROTOCOL_VERSION);
        coins.push_back(CreateFbsCoin(fbb, coin));
    }
    nUndoPos += GetSizeOfCompactSize(coins.size());
    return fbb.CreateVector(coins);
}

flatbuffers::Offset<NngInterface::Tx>
CreateFbsTxMempool(flatbuffers::FlatBufferBuilder &fbb,
                   const CTransactionRef &tx,
                   const std::vector<Coin> &spent_coins) {
    size_t nUndoPos;
    CDataStream tx_ser(SER_NETWORK, PROTOCOL_VERSION);
    tx_ser << tx;
    return NngInterface::CreateTx(
        fbb, CreateFbsTxId(fbb, tx->GetId()),
        fbb.CreateVector((uint8_t *)tx_ser.data(), tx_ser.size()),
        CreateFbsSpentCoins(fbb, std::optional(&spent_coins), nUndoPos));
}

flatbuffers::Offset<NngInterface::BlockTx>
CreateFbsBlockTx(flatbuffers::FlatBufferBuilder &fbb, const CTransactionRef &tx,
                 std::optional<const std::vector<Coin> *> spent_coins,
                 size_t &nDataPos, size_t &nUndoPos) {
    CDataStream tx_ser(SER_NETWORK, PROTOCOL_VERSION);
    tx_ser << tx;
    const size_t data_pos = nDataPos;
    const size_t undo_pos = spent_coins ? nUndoPos : 0;
    nDataPos += tx_ser.size();
    auto ffb_spent_coins = CreateFbsSpentCoins(fbb, spent_coins, nUndoPos);
    return NngInterface::CreateBlockTx(
        fbb,
        NngInterface::CreateTx(
            fbb, CreateFbsTxId(fbb, tx->GetId()),
            fbb.CreateVector((uint8_t *)tx_ser.data(), tx_ser.size()),
            ffb_spent_coins),
        data_pos, undo_pos, spent_coins ? nUndoPos - undo_pos : 0);
}

flatbuffers::Offset<NngInterface::BlockMetadata>
CreateFbsBlockMetadata(flatbuffers::FlatBufferBuilder &fbb,
                       const CBlockMetadataField &metadata_field) {
    return NngInterface::CreateBlockMetadata(
        fbb, metadata_field.nFieldId,
        fbb.CreateVector(metadata_field.vData.data(),
                         metadata_field.vData.size()));
}

size_t GetFirstBlockTxOffset(const CBlock &block, const CBlockIndex *pindex) {
    return pindex->nDataPos + ::GetSerializeSize(CBlockHeader()) +
           ::GetSerializeSize(block.vMetadata, CLIENT_VERSION) +
           GetSizeOfCompactSize(block.vtx.size());
}

size_t GetFirstUndoOffset(const CBlock &block, const CBlockIndex *pindex) {
    return pindex->nUndoPos + GetSizeOfCompactSize(block.vtx.size() - 1);
}

flatbuffers::Offset<NngInterface::Block>
CreateFbsBlock(flatbuffers::FlatBufferBuilder &fbb, const CBlock &block,
               const CBlockIndex *pindex) {
    size_t nDataPos = GetFirstBlockTxOffset(block, pindex);
    size_t nUndoPos = 0;
    CBlockUndo block_undo;
    if (pindex->nHeight) { // Genesis block doesn't have undo data
        nUndoPos = GetFirstUndoOffset(block, pindex);
        if (!UndoReadFromDisk(block_undo, pindex)) {
            return 0;
        }
    }
    std::vector<flatbuffers::Offset<NngInterface::BlockTx>> txs_fbs;
    for (size_t tx_idx = 0; tx_idx < block.vtx.size(); ++tx_idx) {
        std::optional<std::vector<Coin> *> spent_coins =
            tx_idx != 0
                ? std::optional(&block_undo.vtxundo[tx_idx - 1].vprevout)
                : std::nullopt;
        txs_fbs.push_back(CreateFbsBlockTx(fbb, block.vtx[tx_idx], spent_coins,
                                           nDataPos, nUndoPos));
    }
    std::vector<flatbuffers::Offset<NngInterface::BlockMetadata>> metadata;
    for (const CBlockMetadataField &metadata_field : block.vMetadata) {
        metadata.push_back(CreateFbsBlockMetadata(fbb, metadata_field));
    }
    return NngInterface::CreateBlock(
        fbb, CreateFbsBlockHeader(fbb, block.GetBlockHeader()),
        fbb.CreateVector(metadata), fbb.CreateVector(txs_fbs), pindex->nFile,
        pindex->nDataPos, pindex->nUndoPos);
}

/**
 * Convert a uint256 hash to canonical stratum hex encoding used by mining
 * clients: each 32-bit word is byte-reversed while preserving word order.
 */
static std::string HashToStratumHex(const uint256 &hash) {
    const uint8_t *data = hash.begin();
    std::string result;
    result.reserve(64);
    for (int i = 0; i < 32; i += 4) {
        for (int j = 3; j >= 0; j--) {
            result += strprintf("%02x", data[i + j]);
        }
    }
    return result;
}

/** Convert a 32-bit integer to little-endian hex as used by Stratum params. */
static std::string Uint32ToStratumHex(uint32_t val) {
    uint8_t buf[4];
    buf[0] = val & 0xff;
    buf[1] = (val >> 8) & 0xff;
    buf[2] = (val >> 16) & 0xff;
    buf[3] = (val >> 24) & 0xff;
    return HexStr(Span<const uint8_t>(buf, 4));
}

/** Convert raw bytes to lowercase hex preserving byte order. */
static std::string BytesToHex(const uint8_t *data, size_t len) {
    return HexStr(Span<const uint8_t>(data, len));
}

/**
 * Compute merkle branches for the coinbase (tx index 0), suitable for
 * Stratum-style coinbase-first merkle root reconstruction.
 */
static std::vector<uint256> ComputeMerkleBranches(const CBlock &block) {
    std::vector<uint256> branches;
    std::vector<uint256> leaves;
    for (const auto &tx : block.vtx) {
        leaves.push_back(tx->GetHash());
    }
    if (leaves.size() <= 1) {
        return branches;
    }

    std::vector<uint256> level = leaves;
    size_t index = 0;
    while (level.size() > 1) {
        size_t siblingIdx = index ^ 1;
        if (siblingIdx < level.size()) {
            branches.push_back(level[siblingIdx]);
        } else {
            branches.push_back(level[index]);
        }

        std::vector<uint256> nextLevel;
        for (size_t i = 0; i < level.size(); i += 2) {
            uint256 left = level[i];
            uint256 right = (i + 1 < level.size()) ? level[i + 1] : left;
            nextLevel.push_back(Hash(left, right));
        }
        level = std::move(nextLevel);
        index /= 2;
    }
    return branches;
}

/**
 * Split coinbase into coinbase1/coinbase2 for stratum extranonce insertion.
 *
 * The output is deterministic and updates scriptSig compact-size encoding to
 * account for reserved extranonce bytes.
 */
static std::pair<std::string, std::string>
SplitCoinbase(const CTransaction &coinbaseTx, size_t extranonce1Size,
              size_t extranonce2Size) {
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << coinbaseTx;
    std::vector<uint8_t> raw((const uint8_t *)ss.data(),
                             (const uint8_t *)ss.data() + ss.size());

    size_t pos = 0;
    pos += 4;  // version
    pos += 1;  // vin count (coinbase always one input)
    pos += 36; // prevout

    size_t scriptSigLenPos = pos;

    uint64_t origScriptSigLen = 0;
    int varintBytes = 0;
    if (raw[pos] < 0xfd) {
        origScriptSigLen = raw[pos];
        varintBytes = 1;
    } else if (raw[pos] == 0xfd) {
        origScriptSigLen = raw[pos + 1] | (raw[pos + 2] << 8);
        varintBytes = 3;
    } else {
        origScriptSigLen = raw[pos + 1] | (raw[pos + 2] << 8) |
                           (raw[pos + 3] << 16) |
                           ((uint64_t)raw[pos + 4] << 24);
        varintBytes = 5;
    }
    pos += varintBytes;

    size_t scriptSigStart = pos;
    size_t scriptSigEnd = pos + origScriptSigLen;

    uint64_t extranonceSpace = extranonce1Size + extranonce2Size;
    uint64_t newScriptSigLen = origScriptSigLen + extranonceSpace;

    std::vector<uint8_t> newVarint;
    if (newScriptSigLen < 0xfd) {
        newVarint.push_back((uint8_t)newScriptSigLen);
    } else if (newScriptSigLen <= 0xffff) {
        newVarint.push_back(0xfd);
        newVarint.push_back(newScriptSigLen & 0xff);
        newVarint.push_back((newScriptSigLen >> 8) & 0xff);
    } else {
        newVarint.push_back(0xfe);
        newVarint.push_back(newScriptSigLen & 0xff);
        newVarint.push_back((newScriptSigLen >> 8) & 0xff);
        newVarint.push_back((newScriptSigLen >> 16) & 0xff);
        newVarint.push_back((newScriptSigLen >> 24) & 0xff);
    }

    std::vector<uint8_t> cb1Data;
    cb1Data.insert(cb1Data.end(), raw.begin(), raw.begin() + scriptSigLenPos);
    cb1Data.insert(cb1Data.end(), newVarint.begin(), newVarint.end());
    cb1Data.insert(cb1Data.end(), raw.begin() + scriptSigStart,
                   raw.begin() + scriptSigEnd);

    std::vector<uint8_t> cb2Data(raw.begin() + scriptSigEnd, raw.end());
    return {HexStr(cb1Data), HexStr(cb2Data)};
}

/**
 * BIP22-inspired mapping used by mining submit/proposal responses.
 *
 * `reason == std::nullopt` means acceptance (JSON null in BIP22 terms).
 */
static NngInterface::MiningSubmitResult MapMiningSubmitReason(
    const std::optional<std::string> &reasonOpt) {
    if (!reasonOpt.has_value()) {
        return NngInterface::MiningSubmitResult_ACCEPTED;
    }
    const std::string &reason = *reasonOpt;
    if (reason == "duplicate") {
        return NngInterface::MiningSubmitResult_DUPLICATE;
    }
    if (reason == "duplicate-invalid") {
        return NngInterface::MiningSubmitResult_DUPLICATE_INVALID;
    }
    if (reason == "duplicate-inconclusive") {
        return NngInterface::MiningSubmitResult_DUPLICATE_INCONCLUSIVE;
    }
    if (reason == "inconclusive" || reason == "inconclusive-not-best-prevblk") {
        return NngInterface::MiningSubmitResult_INCONCLUSIVE;
    }
    return NngInterface::MiningSubmitResult_REJECTED;
}

class NngSubmitBlockStateCatcher final : public CValidationInterface {
public:
    uint256 hash;
    bool found;
    BlockValidationState state;

    explicit NngSubmitBlockStateCatcher(const uint256 &hashIn)
        : hash(hashIn), found(false), state() {}

protected:
    void BlockChecked(const CBlock &block,
                      const BlockValidationState &stateIn) override {
        if (block.GetHash() != hash) {
            return;
        }
        found = true;
        state = stateIn;
    }
};

NngRpcErrorCode
NngRpcServer::GetBlock(flatbuffers::FlatBufferBuilder &fbb,
                       const NngInterface::GetBlockRequest *request) {
    LOCK(cs_main);
    NngRpcErrorCode code;
    CBlockIndex *pindex;
    if ((code = GetBlockIndex(request, pindex)) !=
        NngRpcErrorCode::NO_RPC_ERROR) {
        return code;
    }
    CBlock block;
    if (!ReadBlockFromDisk(block, pindex, m_consensus)) {
        return NngRpcErrorCode::BLOCK_DATA_CORRUPTED;
    }
    fbb.Finish(NngInterface::CreateGetBlockResponse(
        fbb, CreateFbsBlock(fbb, block, pindex)));
    return NngRpcErrorCode::NO_RPC_ERROR;
}

NngRpcErrorCode
NngRpcServer::GetBlockRange(flatbuffers::FlatBufferBuilder &fbb,
                            const NngInterface::GetBlockRangeRequest *request) {
    LOCK(cs_main);
    const int32_t chain_height = ::ChainActive().Height();
    const int32_t start_height = request->start_height();
    uint32_t num_blocks = request->num_blocks();
    int32_t end_height = start_height + num_blocks - 1;
    if (end_height > chain_height) {
        end_height = chain_height;
        num_blocks = end_height - start_height + 1;
    }

    CBlockIndex *pindex = nullptr;
    if (start_height >= 0) {
        pindex = ::ChainActive().Tip()->GetAncestor(end_height);
    } else {
        num_blocks = 0;
    }
    std::vector<flatbuffers::Offset<NngInterface::Block>> blocks_fbs(
        num_blocks);
    for (auto block_fbs = blocks_fbs.rbegin();
         block_fbs != blocks_fbs.rend() && pindex != nullptr; ++block_fbs) {
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, m_consensus)) {
            return NngRpcErrorCode::BLOCK_DATA_CORRUPTED;
        }
        *block_fbs = CreateFbsBlock(fbb, block, pindex);
        pindex = pindex->pprev;
    }
    fbb.Finish(NngInterface::CreateGetBlockRangeResponse(
        fbb, fbb.CreateVector(blocks_fbs)));
    return NngRpcErrorCode::NO_RPC_ERROR;
}

NngRpcErrorCode
NngRpcServer::GetBlockSlice(flatbuffers::FlatBufferBuilder &fbb,
                            const NngInterface::GetBlockSliceRequest *request) {
    const FlatFilePos filePos(request->file_num(), request->data_pos());
    CAutoFile file(OpenBlockFile(filePos, true), SER_DISK, CLIENT_VERSION);
    std::vector<uint8_t> data(request->num_bytes());
    try {
        file.read((char *)data.data(), request->num_bytes());
    } catch (const std::exception &e) {
        return NngRpcErrorCode::INVALID_BLOCK_SLICE;
    }
    fbb.Finish(
        NngInterface::CreateGetBlockSliceResponse(fbb, fbb.CreateVector(data)));
    return NngRpcErrorCode::NO_RPC_ERROR;
}

NngRpcErrorCode
NngRpcServer::GetUndoSlice(flatbuffers::FlatBufferBuilder &fbb,
                           const NngInterface::GetUndoSliceRequest *request) {
    const FlatFilePos filePos(request->file_num(), request->undo_pos());
    CAutoFile file(OpenUndoFile(filePos, true), SER_DISK, CLIENT_VERSION);
    std::vector<uint8_t> data(request->num_bytes());
    try {
        file.read((char *)data.data(), request->num_bytes());
    } catch (const std::exception &e) {
        return NngRpcErrorCode::INVALID_BLOCK_SLICE;
    }
    fbb.Finish(
        NngInterface::CreateGetUndoSliceResponse(fbb, fbb.CreateVector(data)));
    return NngRpcErrorCode::NO_RPC_ERROR;
}

NngRpcErrorCode
NngRpcServer::GetMempool(flatbuffers::FlatBufferBuilder &fbb,
                         const NngInterface::GetMempoolRequest *request) {
    LOCK(m_node.mempool->cs);
    std::vector<flatbuffers::Offset<NngInterface::MempoolTx>> txs_fbs;
    for (const CTxMemPoolEntry &entry : m_node.mempool->mapTx) {
        std::map<COutPoint, Coin> spent_coins_map;
        for (const CTxIn &input : entry.GetSharedTx()->vin) {
            spent_coins_map[input.prevout] = Coin();
        }
        FindCoins(m_node, spent_coins_map);
        std::vector<Coin> spent_coin;
        spent_coin.reserve(spent_coins_map.size());
        for (const CTxIn &input : entry.GetSharedTx()->vin) {
            spent_coin.push_back(spent_coins_map[input.prevout]);
        }
        txs_fbs.push_back(NngInterface::CreateMempoolTx(
            fbb, CreateFbsTxMempool(fbb, entry.GetSharedTx(), spent_coin),
            entry.GetTime().count()));
    }
    fbb.Finish(
        NngInterface::CreateGetMempoolResponse(fbb, fbb.CreateVector(txs_fbs)));
    return NngRpcErrorCode::NO_RPC_ERROR;
}

NngRpcErrorCode NngRpcServer::GetMiningTemplate(
    flatbuffers::FlatBufferBuilder &fbb,
    const NngInterface::GetMiningTemplateRequest *request) {
    const Config &config = GetConfig();
    const CChainParams &chainparams = config.GetChainParams();

    LOCK(cs_main);
    CBlockIndex *pindexPrev = ::ChainActive().Tip();
    if (!pindexPrev) {
        return NngRpcErrorCode::MINING_TEMPLATE_BUILD_FAILED;
    }

    CScript coinbaseScript;
    if (request && request->coinbase_script()) {
        const auto &scriptBytes = *request->coinbase_script();
        if (scriptBytes.size() > 0) {
            std::vector<uint8_t> scriptVec(scriptBytes.begin(),
                                           scriptBytes.end());
            coinbaseScript = CScript(scriptVec.begin(), scriptVec.end());
        }
    }
    if (coinbaseScript.empty()) {
        // Keep behavior predictable for pure-template callers: this mirrors
        // getblocktemplate's scriptDummy behavior and avoids address parsing
        // policy decisions in the NNG layer.
        coinbaseScript = CScript() << OP_RETURN;
    }

    std::unique_ptr<CBlockTemplate> pblocktemplate;
    try {
        pblocktemplate = BlockAssembler(config, *m_node.mempool)
                             .CreateNewBlock(coinbaseScript);
    } catch (const std::exception &e) {
        LogPrintf("NNG mining template build failed: %s\n", e.what());
        return NngRpcErrorCode::MINING_TEMPLATE_BUILD_FAILED;
    }
    if (!pblocktemplate) {
        return NngRpcErrorCode::MINING_TEMPLATE_BUILD_FAILED;
    }

    CBlock *pblock = &pblocktemplate->block;
    UpdateTime(pblock, chainparams, pindexPrev);
    pblock->nNonce = 0;
    pblock->hashMerkleRoot = BlockMerkleRoot(*pblock);

    CDataStream blockStream(SER_NETWORK, PROTOCOL_VERSION);
    blockStream << *pblock;
    CDataStream headerStream(SER_NETWORK, PROTOCOL_VERSION);
    headerStream << pblock->GetBlockHeader();
    CDataStream coinbaseStream(SER_NETWORK, PROTOCOL_VERSION);
    coinbaseStream << *pblock->vtx[0];

    Amount coinbaseValue = Amount::zero();
    for (const auto &out : pblock->vtx[0]->vout) {
        coinbaseValue += out.nValue;
    }

    const uint32_t en1Size = request ? request->extranonce1_size() : 4;
    const uint32_t en2Size = request ? request->extranonce2_size() : 4;
    auto [cb1, cb2] = SplitCoinbase(*pblock->vtx[0], en1Size, en2Size);

    std::vector<flatbuffers::Offset<flatbuffers::String>> branchesFbs;
    for (const auto &branch : ComputeMerkleBranches(*pblock)) {
        branchesFbs.push_back(fbb.CreateString(branch.GetHex()));
    }

    std::vector<flatbuffers::Offset<NngInterface::MiningTemplateTx>> txsFbs;
    const bool includeTxs = request ? request->include_transactions() : true;
    if (includeTxs) {
        for (size_t i = 1; i < pblock->vtx.size(); ++i) {
            CDataStream txStream(SER_NETWORK, PROTOCOL_VERSION);
            txStream << *pblock->vtx[i];
            Amount fee = Amount::zero();
            int64_t sigops = 0;
            if (i < pblocktemplate->entries.size()) {
                fee = pblocktemplate->entries[i].fees;
                sigops = pblocktemplate->entries[i].sigOpCount;
            }
            txsFbs.push_back(NngInterface::CreateMiningTemplateTx(
                fbb,
                fbb.CreateVector((const uint8_t *)txStream.data(),
                                 txStream.size()),
                CreateFbsTxId(fbb, pblock->vtx[i]->GetId()),
                fee / SATOSHI, sigops));
        }
    }

    static std::atomic<uint64_t> s_templateId{1};
    const uint64_t templateId = s_templateId.fetch_add(1);

    arith_uint256 targetArith;
    targetArith.SetCompact(pblock->nBits);
    const uint256 targetU256 = ArithToUint256(targetArith);
    NngInterface::Hash targetHash = CreateFbsHash(targetU256.begin());

    const uint64_t mintime = pindexPrev->GetMedianTimePast() + 1;
    const uint64_t maxtime = GetAdjustedTime() + MAX_FUTURE_BLOCK_TIME;

    auto response = NngInterface::CreateGetMiningTemplateResponse(
        fbb, templateId,
        fbb.CreateVector((const uint8_t *)blockStream.data(), blockStream.size()),
        fbb.CreateVector((const uint8_t *)headerStream.data(),
                         headerStream.size()),
        CreateFbsBlockHash(fbb, pblock->hashPrevBlock), pindexPrev->nHeight + 1,
        pblock->nHeaderVersion, pblock->nBits, &targetHash,
        pblock->GetBlockTime(), mintime, maxtime, coinbaseValue / SATOSHI,
        fbb.CreateVector((const uint8_t *)coinbaseStream.data(),
                         coinbaseStream.size()),
        fbb.CreateVector(txsFbs), fbb.CreateString(cb1), fbb.CreateString(cb2),
        fbb.CreateVector(branchesFbs),
        fbb.CreateString(HashToStratumHex(pblock->hashPrevBlock)),
        fbb.CreateString(Uint32ToStratumHex(pblock->nBits)),
        fbb.CreateString(BytesToHex(pblock->vTime.data(), pblock->vTime.size())));
    fbb.Finish(response);
    return NngRpcErrorCode::NO_RPC_ERROR;
}

NngRpcErrorCode NngRpcServer::SubmitMinedBlock(
    flatbuffers::FlatBufferBuilder &fbb,
    const NngInterface::SubmitMinedBlockRequest *request) {
    if (!request || !request->block()) {
        return NngRpcErrorCode::INVALID_MINING_REQUEST;
    }

    CBlock block;
    try {
        std::vector<uint8_t> raw(request->block()->begin(), request->block()->end());
        CDataStream ss(raw, SER_NETWORK, PROTOCOL_VERSION);
        ss >> block;
    } catch (...) {
        return NngRpcErrorCode::MINING_SUBMIT_DECODE_FAILED;
    }

    if (block.vtx.empty() || !block.vtx[0]->IsCoinBase()) {
        auto resp = NngInterface::CreateSubmitMinedBlockResponse(
            fbb, NngInterface::MiningSubmitResult_INVALID_COINBASE, false,
            fbb.CreateString("Block does not start with a coinbase"),
            CreateFbsBlockHash(fbb, BlockHash(block.GetHash())));
        fbb.Finish(resp);
        return NngRpcErrorCode::NO_RPC_ERROR;
    }

    const Config &config = GetConfig();
    const uint256 hash = block.GetHash();
    {
        LOCK(cs_main);
        const CBlockIndex *pindex = LookupBlockIndex(BlockHash(hash));
        if (pindex) {
            NngInterface::MiningSubmitResult code =
                pindex->IsValid(BlockValidity::SCRIPTS)
                    ? NngInterface::MiningSubmitResult_DUPLICATE
                    : (pindex->nStatus.isInvalid()
                           ? NngInterface::MiningSubmitResult_DUPLICATE_INVALID
                           : NngInterface::MiningSubmitResult_DUPLICATE_INCONCLUSIVE);
            std::string reason =
                code == NngInterface::MiningSubmitResult_DUPLICATE
                    ? "duplicate"
                    : (code == NngInterface::MiningSubmitResult_DUPLICATE_INVALID
                           ? "duplicate-invalid"
                           : "duplicate-inconclusive");
            auto resp = NngInterface::CreateSubmitMinedBlockResponse(
                fbb, code, false, fbb.CreateString(reason),
                CreateFbsBlockHash(fbb, BlockHash(hash)));
            fbb.Finish(resp);
            return NngRpcErrorCode::NO_RPC_ERROR;
        }
    }

    bool new_block = false;
    auto blockptr = std::make_shared<CBlock>(std::move(block));
    auto sc = std::make_shared<NngSubmitBlockStateCatcher>(hash);
    RegisterSharedValidationInterface(sc);
    bool accepted = m_node.chainman->ProcessNewBlock(config, blockptr,
                                                      /* fForceProcessing */ true,
                                                      &new_block);
    UnregisterSharedValidationInterface(sc);

    std::optional<std::string> bip22Reason = std::string("inconclusive");
    if (!new_block && accepted) {
        bip22Reason = std::string("duplicate");
    } else if (sc->found) {
        if (sc->state.IsValid()) {
            bip22Reason = std::nullopt;
        } else if (sc->state.IsInvalid()) {
            std::string reject = sc->state.GetRejectReason();
            bip22Reason = reject.empty() ? std::optional<std::string>("rejected")
                                         : std::optional<std::string>(reject);
        } else {
            bip22Reason = std::string("inconclusive");
        }
    }

    const auto result = MapMiningSubmitReason(bip22Reason);
    const bool blockAccepted =
        result == NngInterface::MiningSubmitResult_ACCEPTED;
    const std::string reason = bip22Reason.has_value() ? *bip22Reason : "";
    auto resp = NngInterface::CreateSubmitMinedBlockResponse(
        fbb, result, blockAccepted, fbb.CreateString(reason),
        CreateFbsBlockHash(fbb, BlockHash(hash)));
    fbb.Finish(resp);
    return NngRpcErrorCode::NO_RPC_ERROR;
}

NngRpcErrorCode NngRpcServer::ValidateMinedBlockProposal(
    flatbuffers::FlatBufferBuilder &fbb,
    const NngInterface::ValidateMinedBlockProposalRequest *request) {
    if (!request || !request->block()) {
        return NngRpcErrorCode::INVALID_MINING_REQUEST;
    }

    CBlock block;
    try {
        std::vector<uint8_t> raw(request->block()->begin(), request->block()->end());
        CDataStream ss(raw, SER_NETWORK, PROTOCOL_VERSION);
        ss >> block;
    } catch (...) {
        return NngRpcErrorCode::MINING_SUBMIT_DECODE_FAILED;
    }

    const Config &config = GetConfig();
    const CChainParams &chainparams = config.GetChainParams();

    std::optional<std::string> bip22Reason = std::nullopt;
    {
        LOCK(cs_main);
        const uint256 hash = block.GetHash();
        const CBlockIndex *pindex = LookupBlockIndex(BlockHash(hash));
        if (pindex) {
            if (pindex->IsValid(BlockValidity::SCRIPTS)) {
                bip22Reason = std::string("duplicate");
            } else if (pindex->nStatus.isInvalid()) {
                bip22Reason = std::string("duplicate-invalid");
            } else {
                bip22Reason = std::string("duplicate-inconclusive");
            }
        } else {
            CBlockIndex *const pindexPrev = ::ChainActive().Tip();
            if (!pindexPrev || block.hashPrevBlock != pindexPrev->GetBlockHash()) {
                bip22Reason = std::string("inconclusive-not-best-prevblk");
            } else {
                BlockValidationState state;
                TestBlockValidity(state, chainparams, block, pindexPrev,
                                  BlockValidationOptions(config)
                                      .withCheckPoW(false)
                                      .withCheckMerkleRoot(true));
                if (state.IsValid()) {
                    bip22Reason = std::nullopt;
                } else if (state.IsInvalid()) {
                    std::string reject = state.GetRejectReason();
                    bip22Reason = reject.empty()
                                      ? std::optional<std::string>("rejected")
                                      : std::optional<std::string>(reject);
                } else {
                    bip22Reason = std::string("inconclusive");
                }
            }
        }
    }

    const auto result = MapMiningSubmitReason(bip22Reason);
    auto resp = NngInterface::CreateValidateMinedBlockProposalResponse(
        fbb, result, result == NngInterface::MiningSubmitResult_ACCEPTED,
        fbb.CreateString(bip22Reason.has_value() ? *bip22Reason : ""));
    fbb.Finish(resp);
    return NngRpcErrorCode::NO_RPC_ERROR;
}

NngRpcErrorCode NngRpcServer::SendRawTransaction(
    flatbuffers::FlatBufferBuilder &fbb,
    const NngInterface::SendRawTransactionRequest *request) {
    if (!request || !request->raw_tx()) {
        return NngRpcErrorCode::INVALID_MINING_REQUEST;
    }

    CMutableTransaction mtx;
    try {
        std::vector<uint8_t> raw(request->raw_tx()->begin(), request->raw_tx()->end());
        CDataStream ss(raw, SER_NETWORK, PROTOCOL_VERSION);
        ss >> mtx;
    } catch (...) {
        TxId zeroTxid{};
        auto resp = NngInterface::CreateSendRawTransactionResponse(
            fbb, NngInterface::SendRawTransactionResult_DESERIALIZATION_ERROR,
            false, CreateFbsTxId(fbb, zeroTxid),
            fbb.CreateString("decode-failed"));
        fbb.Finish(resp);
        return NngRpcErrorCode::NO_RPC_ERROR;
    }

    CTransactionRef tx(MakeTransactionRef(std::move(mtx)));
    const uint64_t maxFeeRateSatPerKb = request->max_fee_rate();
    Amount max_raw_tx_fee = DEFAULT_MAX_RAW_TX_FEE_RATE.GetFee(GetVirtualTransactionSize(*tx));
    if (maxFeeRateSatPerKb > 0) {
        const CFeeRate configured(int64_t(maxFeeRateSatPerKb) * Amount::satoshi());
        max_raw_tx_fee = configured.GetFee(GetVirtualTransactionSize(*tx));
    }

    std::string err_string;
    const Config &config = GetConfig();
    TransactionError err = BroadcastTransaction(
        m_node, config, tx, err_string, max_raw_tx_fee, request->relay(),
        request->wait_callback());

    NngInterface::SendRawTransactionResult result =
        NngInterface::SendRawTransactionResult_MEMPOOL_ERROR;
    switch (err) {
        case TransactionError::OK:
            result = NngInterface::SendRawTransactionResult_ACCEPTED;
            break;
        case TransactionError::ALREADY_IN_CHAIN:
            result = NngInterface::SendRawTransactionResult_ALREADY_IN_CHAIN;
            break;
        case TransactionError::MAX_FEE_EXCEEDED:
            result = NngInterface::SendRawTransactionResult_MAX_FEE_EXCEEDED;
            break;
        case TransactionError::MISSING_INPUTS:
        case TransactionError::MEMPOOL_REJECTED:
            result = NngInterface::SendRawTransactionResult_MEMPOOL_REJECTED;
            break;
        case TransactionError::MEMPOOL_ERROR:
        default:
            result = NngInterface::SendRawTransactionResult_MEMPOOL_ERROR;
            break;
    }

    auto resp = NngInterface::CreateSendRawTransactionResponse(
        fbb, result, err == TransactionError::OK, CreateFbsTxId(fbb, tx->GetId()),
        fbb.CreateString(err_string));
    fbb.Finish(resp);
    return NngRpcErrorCode::NO_RPC_ERROR;
}

NngRpcErrorCode
NngRpcServer::GetMiningStatus(flatbuffers::FlatBufferBuilder &fbb,
                              const NngInterface::GetMiningStatusRequest *request) {
    LOCK(cs_main);
    const CBlockIndex *tip = ::ChainActive().Tip();
    if (!tip) {
        return NngRpcErrorCode::BLOCK_NOT_FOUND;
    }

    uint32_t peers = 0;
    if (m_node.connman) {
        peers = m_node.connman->GetNodeCount(CConnman::CONNECTIONS_ALL);
    }

    auto resp = NngInterface::CreateGetMiningStatusResponse(
        fbb, CreateFbsBlockHash(fbb, tip->GetBlockHash()), tip->nHeight,
        ::ChainstateActive().IsInitialBlockDownload(),
        static_cast<uint64_t>(m_node.mempool->size()), peers,
        GetAdjustedTime(), fbb.CreateString(tip->nChainWork.GetHex()),
        fbb.CreateString(GetConfig().GetChainParams().NetworkIDString()));
    fbb.Finish(resp);
    return NngRpcErrorCode::NO_RPC_ERROR;
}

class NngPubServer final : public CValidationInterface {
public:
    NngPubServer(std::set<std::string> enabled_messages)
        : m_enabled_messages(enabled_messages) {}

    bool Listen(const std::string &pub_url) {
        NNG_TRY_ERROR(nng_pub0_open(&m_sock),
                      "Failed opening NNG pub0 socket: %s");
        std::string listen_failure_msg =
            strprintf("Failed listening on -nngpub=%s: %%s", pub_url);
        NNG_TRY_ERROR(nng_listen(m_sock, pub_url.c_str(), NULL, 0),
                      listen_failure_msg.c_str());
        LogPrintf("NNG interface: pubsub server listening at %s\n", pub_url);
        RegisterValidationInterface(this);
        return true;
    }

    void Shutdown() { nng_close(m_sock); }

private:
    nng_socket m_sock;
    std::set<std::string> m_enabled_messages;
    // Monotonic process-local epoch used by miningwrkchg.
    std::atomic<uint64_t> m_templateEpoch{1};

    void BroadcastMessage(const std::string msg_type,
                          const flatbuffers::FlatBufferBuilder &fbb) {
        std::vector<uint8_t> msg;
        msg.resize(12 + fbb.GetSize());
        memcpy(msg.data(), msg_type.data(), msg_type.size());
        memcpy(msg.data() + 12, fbb.GetBufferPointer(), fbb.GetSize());
        NNG_TRY_LOG(nng_send(m_sock, msg.data(), msg.size(), 0));
    }

    void EmitMiningWorkChanged(NngInterface::MiningWorkChangeReason reason,
                               const CBlockIndex *tip) {
        if (!IsMessageEnabled(MSG_MININGWRKCHG) || tip == nullptr) {
            return;
        }
        // This signal is intentionally small and constant-shape to keep event
        // latency low for external stratum coordinators that refresh jobs on
        // every tip/mempool invalidation.
        flatbuffers::FlatBufferBuilder fbb;
        fbb.Finish(NngInterface::CreateMiningWorkChanged(
            fbb, reason, CreateFbsBlockHash(fbb, tip->GetBlockHash()),
            tip->nHeight, GetTime(), m_templateEpoch.fetch_add(1)));
        BroadcastMessage(MSG_MININGWRKCHG, fbb);
    }

    void UpdatedBlockTip(const CBlockIndex *pindexNew,
                         const CBlockIndex *pindexFork,
                         bool fInitialDownload) override {
        if (IsMessageEnabled(MSG_UPDATEBLKTIP)) {
            flatbuffers::FlatBufferBuilder fbb;
            fbb.Finish(NngInterface::CreateUpdatedBlockTip(
                fbb, CreateFbsBlockHash(fbb, pindexNew->GetBlockHash())));
            BroadcastMessage(MSG_UPDATEBLKTIP, fbb);
        }
        const bool reorg = pindexFork != nullptr &&
                           pindexNew != nullptr &&
                           pindexNew->pprev != pindexFork;
        EmitMiningWorkChanged(reorg ? NngInterface::MiningWorkChangeReason_REORG
                                    : NngInterface::MiningWorkChangeReason_NEW_TIP,
                              pindexNew);
    }

    void
    TransactionAddedToMempool(const CTransactionRef &ptx,
                              const std::vector<Coin> &spent_coins,
                              uint64_t mempool_sequence) override {
        if (IsMessageEnabled(MSG_MEMPOOLTXADD)) {
            flatbuffers::FlatBufferBuilder fbb;
            fbb.Finish(NngInterface::CreateTransactionAddedToMempool(
                fbb, NngInterface::CreateMempoolTx(
                         fbb, CreateFbsTxMempool(fbb, ptx, spent_coins),
                         GetAdjustedTime())));
            BroadcastMessage(MSG_MEMPOOLTXADD, fbb);
        }

        if (IsMessageEnabled(MSG_MININGWRKCHG)) {
            const CBlockIndex *tip = nullptr;
            {
                LOCK(cs_main);
                tip = ::ChainActive().Tip();
            }
            EmitMiningWorkChanged(
                NngInterface::MiningWorkChangeReason_MEMPOOL_REFRESH, tip);
        }
    }

    void TransactionRemovedFromMempool(const CTransactionRef &ptx,
                                       MemPoolRemovalReason reason,
                                       uint64_t mempool_sequence) override {
        if (IsMessageEnabled(MSG_MEMPOOLTXREM)) {
            flatbuffers::FlatBufferBuilder fbb;
            fbb.Finish(NngInterface::CreateTransactionRemovedFromMempool(
                fbb, CreateFbsTxId(fbb, ptx->GetId())));
            BroadcastMessage(MSG_MEMPOOLTXREM, fbb);
        }

        if (IsMessageEnabled(MSG_MININGWRKCHG)) {
            const CBlockIndex *tip = nullptr;
            {
                LOCK(cs_main);
                tip = ::ChainActive().Tip();
            }
            EmitMiningWorkChanged(
                NngInterface::MiningWorkChangeReason_MEMPOOL_REFRESH, tip);
        }
    }

    void BlockConnected(const std::shared_ptr<const CBlock> &block,
                        const CBlockIndex *pindex) override {
        if (!IsMessageEnabled(MSG_BLKCONNECTED)) {
            return;
        }
        flatbuffers::FlatBufferBuilder fbb;
        fbb.Finish(NngInterface::CreateBlockConnected(
            fbb, CreateFbsBlock(fbb, *block, pindex), /*txs_conflicted=*/0));
        BroadcastMessage(MSG_BLKCONNECTED, fbb);
    }

    void BlockDisconnected(const std::shared_ptr<const CBlock> &block,
                           const CBlockIndex *pindex) override {
        if (!IsMessageEnabled(MSG_BLKDISCONCTD)) {
            return;
        }
        flatbuffers::FlatBufferBuilder fbb;
        fbb.Finish(NngInterface::CreateBlockDisconnected(
            fbb, CreateFbsBlock(fbb, *block, pindex)));
        BroadcastMessage(MSG_BLKDISCONCTD, fbb);
    }

    void ChainStateFlushed(const CBlockLocator &locator) override {
        if (!IsMessageEnabled(MSG_CHAINSTFLUSH)) {
            return;
        }
        if (locator.vHave.size() == 0) {
            return;
        }
        flatbuffers::FlatBufferBuilder fbb;
        fbb.Finish(NngInterface::CreateChainStateFlushed(
            fbb, CreateFbsBlockHash(fbb, locator.vHave[0])));
        BroadcastMessage(MSG_CHAINSTFLUSH, fbb);
    }

    bool IsMessageEnabled(const std::string &msg) {
        return m_enabled_messages.find(msg) != m_enabled_messages.end();
    }
};

std::unique_ptr<NngRpcServer> g_rpc_server;
std::unique_ptr<NngPubServer> g_pub_server;

bool RunRpcServer(NodeContext &node, const Consensus::Params &consensus) {
    if (gArgs.IsArgSet("-nngrpc")) {
        std::string rpc_url = gArgs.GetArg("-nngrpc", "");
        g_rpc_server = std::make_unique<NngRpcServer>(consensus, node);
        if (!g_rpc_server->Listen(rpc_url)) {
            return false;
        }
    } else {
        g_rpc_server = nullptr;
    }
    return true;
}

bool RunPubServer() {
    if (gArgs.IsArgSet("-nngpub")) {
        std::string pub_url = gArgs.GetArg("-nngpub", "");
        std::vector<std::string> vEnabledMessages = gArgs.GetArgs("-nngpubmsg");
        std::set<std::string> enabled_messages(vEnabledMessages.begin(),
                                               vEnabledMessages.end());
        for (const std::string &enabled_message : enabled_messages) {
            if (std::find(AVAILABLE_PUB_MESSAGES.begin(),
                          AVAILABLE_PUB_MESSAGES.end(),
                          enabled_message) == AVAILABLE_PUB_MESSAGES.end()) {
                return InitError(
                    strprintf(_("Invalid message type '%s' in -nngpubmsg."),
                              enabled_message));
            }
        }
        if (enabled_messages.empty()) {
            LogPrintf("Warning: Specified -nngpub, but no -nngpubmsg "
                      "enabled.\n");
        }
        g_pub_server = std::make_unique<NngPubServer>(enabled_messages);
        if (!g_pub_server->Listen(pub_url)) {
            return false;
        }
    } else {
        g_pub_server = nullptr;
    }
    return true;
}

bool StartNngInterface(NodeContext &node,
                       const Consensus::Params &consensus) {
    if (!RunRpcServer(node, consensus)) {
        return false;
    }
    if (!RunPubServer()) {
        return false;
    }
    return true;
}

void StopNngInterface() {
    if (g_rpc_server) {
        g_rpc_server->Shutdown();
    }
    if (g_pub_server) {
        g_pub_server->Shutdown();
    }
}
