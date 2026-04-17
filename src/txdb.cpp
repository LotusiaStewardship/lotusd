// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// CCoinsViewDB and CBlockTreeDB have been replaced by SQLite implementations:
//   - CCoinsViewSqlite (src/sqlite/coins_view_sqlite.h)
//   - CBlockTreeSqlite (src/sqlite/block_tree_sqlite.h)
// The generic key-value CDBWrapper in dbwrapper.h also uses SQLite.
