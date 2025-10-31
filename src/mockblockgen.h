// Copyright (c) 2025 The Lotus developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MOCKBLOCKGEN_H
#define BITCOIN_MOCKBLOCKGEN_H

#include <memory>

class CScheduler;
struct NodeContext;

/**
 * Start the mock block generator thread
 * Automatically generates blocks every N seconds for testing
 */
bool StartMockBlockGenerator(NodeContext &node, int block_interval_seconds);

/**
 * Stop the mock block generator thread
 */
void StopMockBlockGenerator();

/**
 * Check if mock block generator is running
 */
bool IsMockBlockGeneratorRunning();

/**
 * Check if we're in mock block mode (for validation bypass)
 */
bool IsMockBlockMode();

#endif // BITCOIN_MOCKBLOCKGEN_H

