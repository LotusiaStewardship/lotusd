// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <auxmining/auxmining.h>
#include <consensus/merkle.h>
#include <consensus/params.h>
#include <crypto/scrypt.h>
#include <hash.h>
#include <pow/auxpow.h>
#include <pow/pow.h>
#include <primitives/auxpow.h>
#include <primitives/block.h>
#include <primitives/parentheader.h>
#include <streams.h>
#include <uint256.h>

#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(auxpow_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(scrypt_hash_basic) {
    // Verify Scrypt hashing produces a non-zero result on a zeroed 80-byte
    // input (basic smoke test).
    uint8_t input[80] = {};
    uint8_t output[32] = {};
    scrypt_1024_1_1_256(input, output);

    bool allZero = true;
    for (int i = 0; i < 32; i++) {
        if (output[i] != 0) {
            allZero = false;
            break;
        }
    }
    BOOST_CHECK(!allZero);
}

BOOST_AUTO_TEST_CASE(parent_header_pow_hash) {
    CParentBlockHeader header;
    header.nVersion = 1;
    header.nTime = 1234567890;
    header.nBits = 0x1e0ffff0;
    header.nNonce = 42;

    BlockHash powHash = header.GetPowHash();
    BlockHash identityHash = header.GetHash();

    // PoW hash (Scrypt) and identity hash (SHA256d) should differ
    BOOST_CHECK(powHash != identityHash);
    // Both should be non-null
    BOOST_CHECK(!powHash.IsNull());
    BOOST_CHECK(!identityHash.IsNull());
}

BOOST_AUTO_TEST_CASE(merkle_root_for_branch) {
    uint256 leaf = uint256S(
        "aabbccddaabbccddaabbccddaabbccddaabbccddaabbccddaabbccddaabbccdd");

    // Empty branch: root is the leaf itself
    std::vector<uint256> emptyBranch;
    uint256 root = ComputeMerkleRootForBranch(leaf, emptyBranch, 0);
    BOOST_CHECK_EQUAL(root, leaf);

    // Single-element branch
    uint256 sibling = uint256S(
        "1122334411223344112233441122334411223344112233441122334411223344");
    std::vector<uint256> branch = {sibling};

    uint256 rootLeft = ComputeMerkleRootForBranch(leaf, branch, 0);
    uint256 rootRight = ComputeMerkleRootForBranch(leaf, branch, 1);

    // Left and right should produce different roots
    BOOST_CHECK(rootLeft != rootRight);
    // Both should be non-null
    BOOST_CHECK(!rootLeft.IsNull());
    BOOST_CHECK(!rootRight.IsNull());
}

BOOST_AUTO_TEST_CASE(calc_expected_merkle_tree_index) {
    // For a single-chain tree (height=0), index should always be 0
    uint32_t idx = CalcExpectedMerkleTreeIndex(0, AUXPOW_CHAIN_ID, 0);
    BOOST_CHECK_EQUAL(idx, 0u);

    // For height=1 (2 slots), the index should be 0 or 1
    idx = CalcExpectedMerkleTreeIndex(0, AUXPOW_CHAIN_ID, 1);
    BOOST_CHECK(idx < 2);

    // Different nonces should (generally) give different indices for large
    // enough trees
    uint32_t idx1 = CalcExpectedMerkleTreeIndex(1, AUXPOW_CHAIN_ID, 4);
    uint32_t idx2 = CalcExpectedMerkleTreeIndex(2, AUXPOW_CHAIN_ID, 4);
    // Both should be in range
    BOOST_CHECK(idx1 < 16);
    BOOST_CHECK(idx2 < 16);
}

BOOST_AUTO_TEST_CASE(auxpow_metadata_roundtrip) {
    // Create a CAuxPow, store it in a CBlock's metadata, and read it back
    CAuxPow auxpow;
    auxpow.nIndex = 0;
    auxpow.nChainIndex = 0;
    auxpow.parentBlock.nVersion = 0x00620004;
    auxpow.parentBlock.nTime = 1700000000;
    auxpow.parentBlock.nBits = 0x1e0fffff;
    auxpow.parentBlock.nNonce = 12345;

    // Create a minimal coinbase tx for the parent
    CMutableTransaction mtx;
    mtx.nVersion = 1;
    mtx.vin.resize(1);
    mtx.vin[0].scriptSig = CScript() << std::vector<uint8_t>(4, 0x00);
    mtx.vout.resize(1);
    mtx.vout[0].nValue = Amount::zero();
    auxpow.coinbaseTx = MakeTransactionRef(std::move(mtx));

    CBlock block;
    block.nHeaderVersion = 1;
    block.nBits = 0x1c100000;

    // Store and retrieve
    block.SetAuxPow(auxpow);
    BOOST_CHECK(block.HasAuxPow());

    CAuxPow retrieved;
    BOOST_CHECK(block.GetAuxPow(retrieved));
    BOOST_CHECK_EQUAL(retrieved.nIndex, auxpow.nIndex);
    BOOST_CHECK_EQUAL(retrieved.nChainIndex, auxpow.nChainIndex);
    BOOST_CHECK_EQUAL(retrieved.parentBlock.nVersion,
                      auxpow.parentBlock.nVersion);
    BOOST_CHECK_EQUAL(retrieved.parentBlock.nTime, auxpow.parentBlock.nTime);
    BOOST_CHECK_EQUAL(retrieved.parentBlock.nNonce, auxpow.parentBlock.nNonce);
}

BOOST_AUTO_TEST_CASE(auxpow_metadata_absent) {
    CBlock block;
    block.nHeaderVersion = 1;
    BOOST_CHECK(!block.HasAuxPow());

    CAuxPow auxpow;
    BOOST_CHECK(!block.GetAuxPow(auxpow));
}

BOOST_AUTO_TEST_CASE(merge_mine_commitment_single_chain) {
    uint256 auxHash = uint256S(
        "deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef");

    MergeMineCommitment commitment =
        BuildMergeMineCommitment(auxHash, AUXPOW_CHAIN_ID);

    BOOST_CHECK_EQUAL(commitment.nTreeSize, 1u);
    BOOST_CHECK_EQUAL(commitment.nChainIndex, 0u);
    BOOST_CHECK_EQUAL(commitment.chainMerkleRoot, auxHash);

    // Payload: 4 (prefix) + 32 (root) + 4 (treeSize) + 4 (nonce) = 44 bytes
    BOOST_CHECK_EQUAL(commitment.coinbasePayload.size(), 44u);

    // First 4 bytes should be FABE6D6D
    BOOST_CHECK_EQUAL(commitment.coinbasePayload[0], 0xfa);
    BOOST_CHECK_EQUAL(commitment.coinbasePayload[1], 0xbe);
    BOOST_CHECK_EQUAL(commitment.coinbasePayload[2], 0x6d);
    BOOST_CHECK_EQUAL(commitment.coinbasePayload[3], 0x6d);
}

BOOST_AUTO_TEST_CASE(parsed_auxpow_coinbase_valid) {
    uint256 auxHash = uint256S(
        "aabbccdd00112233aabbccdd00112233aabbccdd00112233aabbccdd00112233");

    MergeMineCommitment commitment =
        BuildMergeMineCommitment(auxHash, AUXPOW_CHAIN_ID);

    // Build a coinbase script containing the commitment
    CScript coinbaseScript;
    coinbaseScript << std::vector<uint8_t>(4, 0x00); // height placeholder
    coinbaseScript.insert(coinbaseScript.end(),
                          commitment.coinbasePayload.begin(),
                          commitment.coinbasePayload.end());

    // Parse it
    ParsedAuxPowCoinbase parsed;
    std::string strError;
    bool ok = ParsedAuxPowCoinbase::Parse(coinbaseScript,
                                          commitment.chainMerkleRoot, parsed,
                                          strError);
    BOOST_CHECK_MESSAGE(ok, strError);
    BOOST_CHECK_EQUAL(parsed.nTreeSize, commitment.nTreeSize);
    BOOST_CHECK_EQUAL(parsed.nMergeMineNonce, commitment.nMergeMineNonce);
}

BOOST_AUTO_TEST_CASE(check_auxpow_native_pow_works) {
    // Verify that a block without AuxPoW metadata is validated using native PoW
    Consensus::Params params;
    params.powLimit = uint256S(
        "7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    params.auxpowActivationHeight = 100;
    params.auxpowPowLimit = uint256S(
        "7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");

    CBlock block;
    block.nHeaderVersion = 1;
    block.nBits = UintToArith256(params.powLimit).GetCompact();
    block.nHeight = 1;
    block.SetBlockTime(1700000000);

    // With max powLimit, any hash should pass
    BOOST_CHECK(CheckAuxProofOfWork(block, 1, params));
}

BOOST_AUTO_TEST_CASE(auxpow_before_activation_rejected) {
    Consensus::Params params;
    params.powLimit = uint256S(
        "7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    params.auxpowActivationHeight = 100;
    params.auxpowPowLimit = uint256S(
        "7fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
    params.nAuxPowChainId = 0x4C;

    CBlock block;
    block.nHeaderVersion = 1;
    block.nBits = UintToArith256(params.powLimit).GetCompact();
    block.nHeight = 50;

    // Add a dummy AuxPoW
    CAuxPow auxpow;
    auxpow.nIndex = 0;
    auxpow.nChainIndex = 0;
    auxpow.parentBlock.nVersion = 1;
    CMutableTransaction mtx;
    mtx.nVersion = 1;
    mtx.vin.resize(1);
    mtx.vout.resize(1);
    mtx.vout[0].nValue = Amount::zero();
    auxpow.coinbaseTx = MakeTransactionRef(std::move(mtx));

    block.SetAuxPow(auxpow);

    // Before activation height, AuxPoW should be rejected
    BOOST_CHECK(!CheckAuxProofOfWork(block, 50, params));
}

BOOST_AUTO_TEST_SUITE_END()
