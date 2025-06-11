// Copyright (c) 2012-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <coins.h>

#include <consensus/consensus.h>
#include <logging.h>
#include <random.h>
#include <version.h>

bool CCoinsView::GetCoin(const COutPoint &outpoint, Coin &coin) const {
    return false;
}
BlockHash CCoinsView::GetBestBlock() const {
    return BlockHash();
}
std::vector<BlockHash> CCoinsView::GetHeadBlocks() const {
    return std::vector<BlockHash>();
}
bool CCoinsView::BatchWrite(CCoinsMap &mapCoins, const BlockHash &hashBlock) {
    return false;
}
CCoinsViewCursor *CCoinsView::Cursor() const {
    return nullptr;
}
bool CCoinsView::HaveCoin(const COutPoint &outpoint) const {
    Coin coin;
    return GetCoin(outpoint, coin);
}

CCoinsViewBacked::CCoinsViewBacked(CCoinsView *viewIn) : base(viewIn) {}
bool CCoinsViewBacked::GetCoin(const COutPoint &outpoint, Coin &coin) const {
    return base->GetCoin(outpoint, coin);
}
bool CCoinsViewBacked::HaveCoin(const COutPoint &outpoint) const {
    return base->HaveCoin(outpoint);
}
BlockHash CCoinsViewBacked::GetBestBlock() const {
    return base->GetBestBlock();
}
std::vector<BlockHash> CCoinsViewBacked::GetHeadBlocks() const {
    return base->GetHeadBlocks();
}
void CCoinsViewBacked::SetBackend(CCoinsView &viewIn) {
    base = &viewIn;
}
bool CCoinsViewBacked::BatchWrite(CCoinsMap &mapCoins,
                                  const BlockHash &hashBlock) {
    return base->BatchWrite(mapCoins, hashBlock);
}
CCoinsViewCursor *CCoinsViewBacked::Cursor() const {
    return base->Cursor();
}
size_t CCoinsViewBacked::EstimateSize() const {
    return base->EstimateSize();
}

SaltedOutpointHasher::SaltedOutpointHasher()
    : k0(GetRand(std::numeric_limits<uint64_t>::max())),
      k1(GetRand(std::numeric_limits<uint64_t>::max())) {}

CCoinsViewCache::CCoinsViewCache(CCoinsView *baseIn)
    : CCoinsViewBacked(baseIn), cachedCoinsUsage(0) {}

size_t CCoinsViewCache::DynamicMemoryUsage() const {
    return memusage::DynamicUsage(cacheCoins) + cachedCoinsUsage;
}

CCoinsMap::iterator
CCoinsViewCache::FetchCoin(const COutPoint &outpoint) const {
    CCoinsMap::iterator it = cacheCoins.find(outpoint);
    if (it != cacheCoins.end()) {
        return it;
    }
    Coin tmp;
    if (!base->GetCoin(outpoint, tmp)) {
        return cacheCoins.end();
    }
    CCoinsMap::iterator ret =
        cacheCoins
            .emplace(std::piecewise_construct, std::forward_as_tuple(outpoint),
                     std::forward_as_tuple(std::move(tmp)))
            .first;
    if (ret->second.coin.IsSpent()) {
        // The parent only has an empty entry for this outpoint; we can consider
        // our version as fresh.
        ret->second.flags = CCoinsCacheEntry::FRESH;
    }
    cachedCoinsUsage += ret->second.coin.DynamicMemoryUsage();
    return ret;
}

bool CCoinsViewCache::GetCoin(const COutPoint &outpoint, Coin &coin) const {
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    if (it != cacheCoins.end()) {
        coin = it->second.coin;
        UpdateAccessTime(outpoint);
        return !coin.IsSpent();
    }
    if (!base->GetCoin(outpoint, coin)) {
        return false;
    }
    CCoinsCacheEntry &entry = cacheCoins[outpoint];
    entry.coin = coin;
    entry.flags = 0;
    cachedCoinsUsage += entry.coin.DynamicMemoryUsage();
    UpdateAccessTime(outpoint);
    return true;
}

void CCoinsViewCache::AddCoin(const COutPoint &outpoint, Coin coin,
                              bool possible_overwrite) {
    assert(!coin.IsSpent());
    if (coin.out.scriptPubKey.IsUnspendable()) {
        return;
    }

    CCoinsMap::iterator it = cacheCoins.find(outpoint);
    if (it != cacheCoins.end()) {
        if (!possible_overwrite) {
            // If an unspent version exists, don't modify it
            if (!it->second.coin.IsSpent()) {
                return;
            }
            // If the coin is spent, we can overwrite it
        }
        cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
    }
    CCoinsCacheEntry &entry = cacheCoins[outpoint];
    entry.coin = std::move(coin);
    entry.flags |= CCoinsCacheEntry::DIRTY |
                   (it == cacheCoins.end() ? CCoinsCacheEntry::FRESH : 0);
    cachedCoinsUsage += entry.coin.DynamicMemoryUsage();
    UpdateAccessTime(outpoint);

    // Check if we need to reallocate the cache
    if (cachedCoinsUsage > MAX_CACHE_SIZE) {
        ReallocateCache();
    }
}

void AddCoins(CCoinsViewCache &cache, const CTransaction &tx, int nHeight,
              bool check_for_overwrite) {
    bool fCoinbase = tx.IsCoinBase();
    const TxId txid = tx.GetId();
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        const COutPoint outpoint(txid, i);
        bool overwrite =
            check_for_overwrite ? cache.HaveCoin(outpoint) : fCoinbase;
        // Coinbase transactions can always be overwritten,
        // in order to correctly deal with the pre-BIP30 occurrences of
        // duplicate coinbase transactions.
        cache.AddCoin(outpoint, Coin(tx.vout[i], nHeight, fCoinbase),
                      overwrite);
    }
}

bool CCoinsViewCache::SpendCoin(const COutPoint &outpoint, Coin *moveto) {
    CCoinsMap::iterator it = FetchCoin(outpoint);
    if (it == cacheCoins.end()) {
        return false;
    }
    cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
    if (moveto) {
        *moveto = std::move(it->second.coin);
    }
    if (it->second.flags & CCoinsCacheEntry::FRESH) {
        cacheCoins.erase(it);
    } else {
        it->second.flags |= CCoinsCacheEntry::DIRTY;
        it->second.coin.Clear();
    }
    UpdateAccessTime(outpoint);
    return true;
}

static const Coin coinEmpty;

const Coin &CCoinsViewCache::AccessCoin(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    if (it == cacheCoins.end()) {
        return coinEmpty;
    }
    return it->second.coin;
}

bool CCoinsViewCache::HaveCoin(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = FetchCoin(outpoint);
    if (it != cacheCoins.end()) {
        UpdateAccessTime(outpoint);
        return !it->second.coin.IsSpent();
    }
    return base->HaveCoin(outpoint);
}

bool CCoinsViewCache::HaveCoinInCache(const COutPoint &outpoint) const {
    CCoinsMap::const_iterator it = cacheCoins.find(outpoint);
    return (it != cacheCoins.end() && !it->second.coin.IsSpent());
}

BlockHash CCoinsViewCache::GetBestBlock() const {
    if (hashBlock.IsNull()) {
        hashBlock = base->GetBestBlock();
    }
    return hashBlock;
}

void CCoinsViewCache::SetBestBlock(const BlockHash &hashBlockIn) {
    hashBlock = hashBlockIn;
}

bool CCoinsViewCache::BatchWrite(CCoinsMap &mapCoins,
                                 const BlockHash &hashBlockIn) {
    for (CCoinsMap::iterator it = mapCoins.begin(); it != mapCoins.end();
         it = mapCoins.erase(it)) {
        // Ignore non-dirty entries (optimization).
        if (!(it->second.flags & CCoinsCacheEntry::DIRTY)) {
            continue;
        }
        CCoinsMap::iterator itUs = cacheCoins.find(it->first);
        if (itUs == cacheCoins.end()) {
            // The parent cache does not have an entry, while the child cache
            // does. We can ignore it if it's both spent and FRESH in the child
            if (!(it->second.flags & CCoinsCacheEntry::FRESH &&
                  it->second.coin.IsSpent())) {
                // Create the coin in the parent cache, move the data up
                // and mark it as dirty.
                CCoinsCacheEntry &entry = cacheCoins[it->first];
                entry.coin = std::move(it->second.coin);
                cachedCoinsUsage += entry.coin.DynamicMemoryUsage();
                entry.flags = CCoinsCacheEntry::DIRTY;
                // We can mark it FRESH in the parent if it was FRESH in the
                // child. Otherwise it might have just been flushed from the
                // parent's cache and already exist in the grandparent
                if (it->second.flags & CCoinsCacheEntry::FRESH) {
                    entry.flags |= CCoinsCacheEntry::FRESH;
                }
            }
        } else {
            // Found the entry in the parent cache
            if ((it->second.flags & CCoinsCacheEntry::FRESH) &&
                !itUs->second.coin.IsSpent()) {
                // The coin was marked FRESH in the child cache, but the coin
                // exists in the parent cache. If this ever happens, it means
                // the FRESH flag was misapplied and there is a logic error in
                // the calling code.
                throw std::logic_error("FRESH flag misapplied to coin that "
                                       "exists in parent cache");
            }

            if ((itUs->second.flags & CCoinsCacheEntry::FRESH) &&
                it->second.coin.IsSpent()) {
                // The grandparent cache does not have an entry, and the coin
                // has been spent. We can just delete it from the parent cache.
                cachedCoinsUsage -= itUs->second.coin.DynamicMemoryUsage();
                cacheCoins.erase(itUs);
            } else {
                // A normal modification.
                cachedCoinsUsage -= itUs->second.coin.DynamicMemoryUsage();
                itUs->second.coin = std::move(it->second.coin);
                cachedCoinsUsage += itUs->second.coin.DynamicMemoryUsage();
                itUs->second.flags |= CCoinsCacheEntry::DIRTY;
                // NOTE: It isn't safe to mark the coin as FRESH in the parent
                // cache. If it already existed and was spent in the parent
                // cache then marking it FRESH would prevent that spentness
                // from being flushed to the grandparent.
            }
        }
    }
    hashBlock = hashBlockIn;
    return true;
}

bool CCoinsViewCache::Flush() {
    bool fOk = base->BatchWrite(cacheCoins, hashBlock);
    cacheCoins.clear();
    cachedCoinsUsage = 0;
    return fOk;
}

void CCoinsViewCache::Uncache(const COutPoint &outpoint) {
    CCoinsMap::iterator it = cacheCoins.find(outpoint);
    if (it != cacheCoins.end()) {
        // Only uncache if not modified (not DIRTY)
        if (!(it->second.flags & CCoinsCacheEntry::DIRTY)) {
            cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
            lastAccessTime.erase(outpoint);
            cacheCoins.erase(it);
        }
    }
}

unsigned int CCoinsViewCache::GetCacheSize() const {
    return cacheCoins.size();
}

bool CCoinsViewCache::HaveInputs(const CTransaction &tx) const {
    if (tx.IsCoinBase()) {
        return true;
    }

    for (size_t i = 0; i < tx.vin.size(); i++) {
        if (!HaveCoin(tx.vin[i].prevout)) {
            return false;
        }
    }

    return true;
}

//
void CCoinsViewCache::ReallocateCache() {
    // Sort by last access time
    std::vector<std::pair<COutPoint, int64_t>> sortedAccess;
    sortedAccess.reserve(lastAccessTime.size());
    for (const auto &item : lastAccessTime) {
        sortedAccess.emplace_back(item.first, item.second);
    }
    std::sort(sortedAccess.begin(), sortedAccess.end(),
              [](const auto &a, const auto &b) { return a.second < b.second; });

    // Remove least recently used entries until we're under the limit
    for (const auto &item : sortedAccess) {
        if (cachedCoinsUsage <= MAX_CACHE_SIZE * 0.8) { // Leave 20% headroom
            break;
        }
        const COutPoint &outpoint = item.first;
        auto it = cacheCoins.find(outpoint);
        if (it != cacheCoins.end() &&
            !(it->second.flags & CCoinsCacheEntry::DIRTY)) {
            cachedCoinsUsage -= it->second.coin.DynamicMemoryUsage();
            lastAccessTime.erase(outpoint);
            cacheCoins.erase(it);
        }
    }
}

void CCoinsViewCache::UpdateAccessTime(const COutPoint &outpoint) const {
    lastAccessTime[outpoint] = ++currentAccessTime;
}

// TODO: merge with similar definition in undo.h.
static const size_t MAX_OUTPUTS_PER_TX =
    MAX_TX_SIZE / ::GetSerializeSize(CTxOut(), PROTOCOL_VERSION);

const Coin &AccessByTxid(const CCoinsViewCache &view, const TxId &txid) {
    for (uint32_t n = 0; n < MAX_OUTPUTS_PER_TX; n++) {
        const Coin &alternate = view.AccessCoin(COutPoint(txid, n));
        if (!alternate.IsSpent()) {
            return alternate;
        }
    }

    return coinEmpty;
}

bool CCoinsViewErrorCatcher::GetCoin(const COutPoint &outpoint,
                                     Coin &coin) const {
    try {
        return CCoinsViewBacked::GetCoin(outpoint, coin);
    } catch (const std::runtime_error &e) {
        for (auto f : m_err_callbacks) {
            f();
        }
        LogPrintf("Error reading from database: %s\n", e.what());
        // Starting the shutdown sequence and returning false to the caller
        // would be interpreted as 'entry not found' (as opposed to unable to
        // read data), and could lead to invalid interpretation. Just exit
        // immediately, as we can't continue anyway, and all writes should be
        // atomic.
        std::abort();
    }
}
