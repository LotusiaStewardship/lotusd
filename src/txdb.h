// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_TXDB_H
#define BITCOIN_TXDB_H

#include <cstdint>

//! min. -dbcache (MiB)
static constexpr int64_t MIN_DB_CACHE_MB = 4;
//! max. -dbcache (MiB)
static constexpr int64_t MAX_DB_CACHE_MB = sizeof(void *) > 4 ? 16384 : 1024;
//! -dbcache default (MiB)
static constexpr int64_t DEFAULT_DB_CACHE_MB = 1024;
//! -dbbatchsize default (bytes)
static constexpr int64_t DEFAULT_DB_BATCH_SIZE = 16 << 20;
//! Max memory allocated to block tree DB specific cache, if no -txindex (MiB)
static constexpr int64_t MAX_BLOCK_DB_CACHE_MB = 2;
//! Max memory allocated to block tree DB specific cache, if -txindex (MiB)
static constexpr int64_t MAX_TX_INDEX_CACHE_MB = 1024;
//! Max memory allocated to all block filter index caches combined in MiB.
static constexpr int64_t MAX_FILTER_INDEX_CACHE_MB = 1024;
//! Max memory allocated to coin DB specific cache (MiB)
static constexpr int64_t MAX_COINS_DB_CACHE_MB = 8;

#endif // BITCOIN_TXDB_H
