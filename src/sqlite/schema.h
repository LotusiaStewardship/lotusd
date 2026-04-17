// Copyright (c) 2024 The Logos Foundation
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_SQLITE_SCHEMA_H
#define BITCOIN_SQLITE_SCHEMA_H

#include <string>

struct sqlite3;

namespace sqlite_schema {

extern const char *UTXO_TABLE;
extern const char *BLOCK_INDEX_TABLE;
extern const char *BLOCK_FILE_INFO_TABLE;
extern const char *TRANSACTIONS_TABLE;
extern const char *TX_INPUTS_TABLE;
extern const char *TX_OUTPUTS_TABLE;
extern const char *ADDRESS_BALANCES_TABLE;
extern const char *ADDRESS_HISTORY_TABLE;
extern const char *META_TABLE;

extern const char *ALL_INDEXES;

void CreateAllTables(sqlite3 *db);
void CreateAllIndexes(sqlite3 *db);

} // namespace sqlite_schema

#endif // BITCOIN_SQLITE_SCHEMA_H
