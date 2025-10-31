// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_HTTPEXPLORER_H
#define BITCOIN_HTTPEXPLORER_H

/**
 * Initialize HTTP block explorer
 */
bool InitHTTPExplorer();

/**
 * Interrupt HTTP block explorer  
 */
void InterruptHTTPExplorer();

/**
 * Shutdown HTTP block explorer
 */
void StopHTTPExplorer();

#endif // BITCOIN_HTTPEXPLORER_H

