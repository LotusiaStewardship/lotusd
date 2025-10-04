// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2017-2020 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SCRIPT_INTERPRETER_H
#define BITCOIN_SCRIPT_INTERPRETER_H

#include <primitives/transaction.h>
#include <script/script_error.h>
#include <script/script_exec_data.h>
#include <script/script_flags.h>
#include <script/script_metrics.h>
#include <script/sighashtype.h>

#include <cstdint>
#include <optional>
#include <vector>

class CPubKey;
class CScript;
class CTransaction;
class uint256;

template <class T>
bool SignatureHash(uint256 &sighashOut,
                   const std::optional<ScriptExecutionData> &execdata,
                   const CScript &scriptCode, const T &txTo, unsigned int nIn,
                   SigHashType sigHashType, const Amount amount,
                   const PrecomputedTransactionData *cache = nullptr,
                   uint32_t flags = SCRIPT_ENABLE_SIGHASH_FORKID);

class BaseSignatureChecker {
public:
    virtual bool VerifySignature(const std::vector<uint8_t> &vchSig,
                                 const CPubKey &vchPubKey,
                                 const uint256 &sighash) const;

    virtual bool CheckSig(const std::vector<uint8_t> &vchSigIn,
                          const std::vector<uint8_t> &vchPubKey,
                          const std::optional<ScriptExecutionData> &execdata,
                          const CScript &scriptCode, uint32_t flags) const {
        return false;
    }

    virtual bool CheckLockTime(const CScriptNum &nLockTime) const {
        return false;
    }

    virtual bool CheckSequence(const CScriptNum &nSequence) const {
        return false;
    }

    // Covenant introspection support
    virtual bool HasTransaction() const { return false; }
    virtual int GetTxVersion() const { return 0; }
    virtual size_t GetTxInputCount() const { return 0; }
    virtual size_t GetTxOutputCount() const { return 0; }
    virtual uint32_t GetTxLockTime() const { return 0; }
    virtual bool GetTxOutput(size_t index, CTxOut &out) const { return false; }
    virtual unsigned int GetInputIndex() const { return 0; }
    virtual Amount GetAmount() const { return Amount::zero(); }
    virtual const CScript *GetScriptPubKey() const { return nullptr; }

    virtual ~BaseSignatureChecker() {}
};

template <class T>
class GenericTransactionSignatureChecker : public BaseSignatureChecker {
private:
    const T *txTo;
    unsigned int nIn;
    const Amount amount;
    const PrecomputedTransactionData *txdata;

public:
    GenericTransactionSignatureChecker(
        const T *txToIn, unsigned int nInIn, const Amount &amountIn,
        const PrecomputedTransactionData &txdataIn)
        : txTo(txToIn), nIn(nInIn), amount(amountIn), txdata(&txdataIn) {}

    // The overridden functions are now final.
    bool CheckSig(const std::vector<uint8_t> &vchSigIn,
                  const std::vector<uint8_t> &vchPubKey,
                  const std::optional<ScriptExecutionData> &execdata,
                  const CScript &scriptCode,
                  uint32_t flags) const final override;
    bool CheckLockTime(const CScriptNum &nLockTime) const final override;
    bool CheckSequence(const CScriptNum &nSequence) const final override;
    
    // Covenant introspection support
    bool HasTransaction() const final override { 
        return txTo != nullptr; 
    }
    int GetTxVersion() const final override { 
        return txTo ? txTo->nVersion : 0; 
    }
    size_t GetTxInputCount() const final override { 
        return txTo ? txTo->vin.size() : 0; 
    }
    size_t GetTxOutputCount() const final override { 
        return txTo ? txTo->vout.size() : 0; 
    }
    uint32_t GetTxLockTime() const final override { 
        return txTo ? txTo->nLockTime : 0; 
    }
    bool GetTxOutput(size_t index, CTxOut &out) const final override {
        if (txTo && index < txTo->vout.size()) {
            out = CTxOut(txTo->vout[index]);
            return true;
        }
        return false;
    }
    unsigned int GetInputIndex() const final override {
        return nIn;
    }
    Amount GetAmount() const final override {
        return amount;
    }
    const CScript *GetScriptPubKey() const final override {
        if (txdata && txdata->m_spent_outputs.size() > nIn) {
            return &txdata->m_spent_outputs[nIn].scriptPubKey;
        }
        return nullptr;
    }
};

using TransactionSignatureChecker =
    GenericTransactionSignatureChecker<CTransaction>;
using MutableTransactionSignatureChecker =
    GenericTransactionSignatureChecker<CMutableTransaction>;

bool EvalScript(std::vector<std::vector<uint8_t>> &stack, const CScript &script,
                uint32_t flags, const BaseSignatureChecker &checker,
                ScriptExecutionMetrics &metrics, ScriptExecutionData &execdata,
                ScriptError *error = nullptr);
static inline bool EvalScript(std::vector<std::vector<uint8_t>> &stack,
                              const CScript &script, uint32_t flags,
                              const BaseSignatureChecker &checker,
                              ScriptError *error = nullptr) {
    ScriptExecutionMetrics dummymetrics;
    ScriptExecutionData dummyexecdata{script};
    return EvalScript(stack, script, flags, checker, dummymetrics,
                      dummyexecdata, error);
}

/**
 * Execute an unlocking and locking script together.
 *
 * Upon success, metrics will hold the accumulated script metrics.
 * (upon failure, the results should not be relied on)
 */
bool VerifyScript(const CScript &scriptSig, const CScript &scriptPubKey,
                  uint32_t flags, const BaseSignatureChecker &checker,
                  ScriptExecutionMetrics &metricsOut,
                  ScriptError *serror = nullptr);
static inline bool VerifyScript(const CScript &scriptSig,
                                const CScript &scriptPubKey, uint32_t flags,
                                const BaseSignatureChecker &checker,
                                ScriptError *serror = nullptr) {
    ScriptExecutionMetrics dummymetrics;
    return VerifyScript(scriptSig, scriptPubKey, flags, checker, dummymetrics,
                        serror);
}

int FindAndDelete(CScript &script, const CScript &b);

#endif // BITCOIN_SCRIPT_INTERPRETER_H
