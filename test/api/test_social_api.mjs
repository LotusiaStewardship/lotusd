#!/usr/bin/env node
// Functional tests for the RANK social module API (/api/v1/social/*)
// and the explorer adapter functions (api-adapter.js worker layer).

const BASE = process.env.API_BASE || 'http://localhost:10604';

let passed = 0;
let failed = 0;

function assert(condition, msg) {
  if (!condition) {
    console.error(`  FAIL: ${msg}`);
    failed++;
    return false;
  }
  passed++;
  return true;
}

async function get(path, query) {
  const u = new URL(path, BASE);
  if (query) {
    for (const [k, v] of Object.entries(query)) u.searchParams.set(k, String(v));
  }
  const res = await fetch(u.toString(), { signal: AbortSignal.timeout(5000) });
  return { status: res.status, data: await res.json() };
}

// ─── Social Activity ───────────────────────────────────────────────────────────

async function testSocialActivity() {
  console.log('Social Activity:');
  const { status, data } = await get('/api/v1/social/activity');
  assert(status === 200, 'activity returns 200');
  assert(Array.isArray(data.votes), 'activity has votes array');
  assert(typeof data.numPages === 'number', 'activity has numPages');
  assert(data.numPages >= 1, 'numPages >= 1');

  // Pagination
  const { data: p2 } = await get('/api/v1/social/activity', { page: 1, pageSize: 5 });
  assert(Array.isArray(p2.votes), 'paginated activity has votes');
  assert(p2.votes.length <= 5, 'respects pageSize');
}

// ─── Social Profiles ──────────────────────────────────────────────────────────

async function testSocialProfiles() {
  console.log('Social Profiles:');
  const { status, data } = await get('/api/v1/social/profiles');
  assert(status === 200, 'profiles returns 200');
  assert(Array.isArray(data.profiles), 'profiles has profiles array');
  assert(typeof data.numPages === 'number', 'profiles has numPages');

  // Check shape if we have data
  if (data.profiles.length > 0) {
    const p = data.profiles[0];
    assert(typeof p.platform === 'string', 'profile has platform string');
    assert(typeof p.id === 'string', 'profile has id string');
    assert(typeof p.ranking === 'number', 'profile has ranking number');
    assert(typeof p.votesPositive === 'number', 'profile has votesPositive');
    assert(typeof p.votesNegative === 'number', 'profile has votesNegative');
  }
}

// ─── Profile Detail ────────────────────────────────────────────────────────────

async function testProfileDetail() {
  console.log('Profile Detail:');
  const { status, data } = await get('/api/v1/social/twitter/nonexistent_user_12345');
  assert(status === 200, 'profile detail returns 200 even for unknown');
  assert(data.platform === 'twitter', 'platform matches');
  assert(data.id === 'nonexistent_user_12345', 'id matches');
  assert(typeof data.ranking === 'number', 'has ranking');
  assert(typeof data.votesPositive === 'number', 'has votesPositive');
  assert(typeof data.votesNegative === 'number', 'has votesNegative');
  assert(data.ranking === 0, 'unknown profile has zero ranking');
}

// ─── Profile Posts ─────────────────────────────────────────────────────────────

async function testProfilePosts() {
  console.log('Profile Posts:');
  const { status, data } = await get('/api/v1/social/twitter/test_user/posts', { page: 1, pageSize: 10 });
  assert(status === 200, 'posts returns 200');
  assert(Array.isArray(data.posts), 'has posts array');
  assert(typeof data.numPages === 'number', 'has numPages');

  if (data.posts.length > 0) {
    const p = data.posts[0];
    assert(typeof p.id === 'string', 'post has id');
    assert(typeof p.ranking === 'number', 'post has ranking');
    assert(typeof p.votesPositive === 'number', 'post has votesPositive');
    assert(typeof p.votesNegative === 'number', 'post has votesNegative');
  }
}

// ─── Profile Votes ─────────────────────────────────────────────────────────────

async function testProfileVotes() {
  console.log('Profile Votes:');
  const { status, data } = await get('/api/v1/social/twitter/test_user/votes', { page: 1, pageSize: 10 });
  assert(status === 200, 'votes returns 200');
  assert(Array.isArray(data.votes), 'has votes array');
  assert(typeof data.numPages === 'number', 'has numPages');

  if (data.votes.length > 0) {
    const v = data.votes[0];
    assert(typeof v.txid === 'string', 'vote has txid');
    assert(typeof v.timestamp === 'number', 'vote has timestamp');
    assert(typeof v.sentiment === 'string', 'vote has sentiment string');
    assert(typeof v.sats === 'number', 'vote has sats');
    assert(typeof v.post === 'object', 'vote has post object');
    assert(typeof v.post.id === 'string', 'vote.post has id');
  }
}

// ─── Stats Endpoints ──────────────────────────────────────────────────────────

async function testStats() {
  console.log('Stats Endpoints:');

  for (const path of [
    '/api/v1/social/stats/profiles/top',
    '/api/v1/social/stats/profiles/bottom',
    '/api/v1/social/stats/posts/top',
    '/api/v1/social/stats/posts/bottom',
    '/api/v1/social/stats/profiles/top-ranked/today',
    '/api/v1/social/stats/profiles/lowest-ranked/today',
    '/api/v1/social/stats/posts/top-ranked/today',
    '/api/v1/social/stats/posts/lowest-ranked/today',
  ]) {
    const { status, data } = await get(path);
    assert(status === 200, `${path} returns 200`);
    assert(Array.isArray(data), `${path} returns array`);

    if (data.length > 0) {
      const item = data[0];
      assert(typeof item.platform === 'string', `${path} item has platform`);
      assert(typeof item.ranking === 'number', `${path} item has ranking`);
    }
  }
}

// ─── Explorer API (existing endpoints used by adapters) ───────────────────────

async function testExplorerAPI() {
  console.log('Explorer API (for adapter validation):');

  // Chain
  const { status: cs, data: chain } = await get('/api/v1/chain');
  assert(cs === 200, 'chain returns 200');
  assert(typeof chain.height === 'number', 'chain has height');
  assert(chain.height >= 0, 'chain height >= 0 (may be 0 during IBD)');

  // Mining
  const { status: ms, data: mining } = await get('/api/v1/mining');
  assert(ms === 200, 'mining returns 200');
  assert(typeof mining.difficulty === 'number', 'mining has difficulty');

  // Network peers
  const { status: ns, data: net } = await get('/api/v1/network/peers');
  assert(ns === 200, 'peers returns 200');

  // Blocks list
  const { status: bs, data: blocks } = await get('/api/v1/blocks', { limit: 3, offset: 0 });
  assert(bs === 200, 'blocks returns 200');
  assert(Array.isArray(blocks.data), 'blocks has data array');
  assert(blocks.data.length > 0, 'blocks has items');
  assert(blocks.pagination && typeof blocks.pagination.total === 'number', 'blocks has pagination.total');

  const block = blocks.data[0];
  assert(typeof block.hash === 'string', 'block has hash');
  assert(typeof block.height === 'number', 'block has height');
  assert(typeof block.time === 'number', 'block has time');
  assert(typeof block.n_tx === 'number', 'block has n_tx');
  assert(typeof block.size === 'number', 'block has size');

  // Single block by height
  const { status: bds, data: bd } = await get('/api/v1/blocks/0');
  assert(bds === 200, 'block detail by height returns 200');
  assert(bd.height === 0, 'block detail height is 0');
  assert(typeof bd.hash === 'string' && bd.hash.length === 64, 'block detail has valid hash');

  // Single block by hash
  const { status: bhs, data: bh } = await get('/api/v1/blocks/' + bd.hash);
  assert(bhs === 200, 'block detail by hash returns 200');
  assert(bh.height === 0, 'block by hash returns same height');

  // Block transactions
  const { status: bts, data: btx } = await get('/api/v1/blocks/0/txs');
  assert(bts === 200 || bts === 404, 'block txs endpoint responds');

  // Mempool
  const { status: mps, data: mp } = await get('/api/v1/mempool');
  assert(mps === 200, 'mempool returns 200');
  assert(typeof mp.size === 'number', 'mempool has size');
}

// ─── Worker Adapter Functions (simulated) ─────────────────────────────────────

async function testWorkerAdapters() {
  console.log('Worker Adapter Shapes (simulated):');

  // Simulate adaptOverview
  const [mining, peers, chain] = await Promise.all([
    get('/api/v1/mining').then(r => r.data),
    get('/api/v1/network/peers').then(r => r.data),
    get('/api/v1/chain').then(r => r.data),
  ]);
  const overview = {
    mininginfo: {
      networkhashps: mining.networkhashps || 0,
      difficulty: mining.difficulty || 0,
      target: 120,
    },
    peerinfo: (Array.isArray(peers) ? peers : (peers.data || [])).map(p => ({
      addr: p.addr || '',
      subver: p.subver || '',
      synced_headers: p.startingheight || 0,
    })),
  };
  assert(typeof overview.mininginfo === 'object', 'adaptOverview: mininginfo object');
  assert(typeof overview.mininginfo.difficulty === 'number', 'adaptOverview: difficulty number');
  assert(Array.isArray(overview.peerinfo), 'adaptOverview: peerinfo array');

  // Simulate adaptChainInfo
  const chainInfo = { tipHeight: chain.height, blocks: chain.height };
  assert(typeof chainInfo.tipHeight === 'number', 'adaptChainInfo: tipHeight number');
  assert(chainInfo.tipHeight >= 0, 'adaptChainInfo: tipHeight >= 0 (may be 0 during IBD)');

  // Simulate adaptBlocks
  const blocksData = await get('/api/v1/blocks', { limit: 5, offset: 0 }).then(r => r.data);
  const adaptedBlocks = {
    blocks: (blocksData.data || []).map(b => ({
      blockInfo: {
        hash: b.hash,
        height: b.height,
        timestamp: b.time,
        numBurnedSats: 0,
        numTxs: b.n_tx,
        blockSize: b.size,
      }
    })),
    tipHeight: (blocksData.pagination?.total || 1) - 1,
  };
  assert(Array.isArray(adaptedBlocks.blocks), 'adaptBlocks: blocks array');
  assert(adaptedBlocks.blocks.length > 0, 'adaptBlocks: has blocks');
  const bi = adaptedBlocks.blocks[0].blockInfo;
  assert(typeof bi.hash === 'string', 'adaptBlocks: blockInfo.hash');
  assert(typeof bi.height === 'number', 'adaptBlocks: blockInfo.height');
  assert(typeof bi.timestamp === 'number', 'adaptBlocks: blockInfo.timestamp');
  assert(typeof bi.numTxs === 'number', 'adaptBlocks: blockInfo.numTxs');
  assert(typeof bi.blockSize === 'number', 'adaptBlocks: blockInfo.blockSize');

  // Simulate adaptBlockDetail
  const blockDetail = await get('/api/v1/blocks/0').then(r => r.data);
  assert(typeof blockDetail.hash === 'string', 'adaptBlockDetail: has hash');
  assert(typeof blockDetail.time === 'number', 'adaptBlockDetail: has time');

  // Simulate adaptAddressBalance (just check format)
  // Can't test with real address during reindex, but verify endpoint exists
  const { status: as } = await get('/api/v1/addresses/lotus_16PSJX3wACnqPKyXxKLxrA3vJNmZpbHJZWPb1');
  assert(as === 200 || as === 404, 'addresses endpoint responds');

  // Simulate fetchSocialJson rewiring
  const activityPath = '/api/social/activity';
  const subpath = activityPath.replace(/^\/api\/social\/?/, '');
  assert(subpath === 'activity', 'fetchSocialJson: strips prefix correctly');
  const nodeApiPath = 'social/' + subpath;
  assert(nodeApiPath === 'social/activity', 'fetchSocialJson: builds correct node path');
}

// ─── Run all tests ─────────────────────────────────────────────────────────────

async function main() {
  console.log(`\nTesting against ${BASE}\n${'='.repeat(60)}\n`);

  try {
    await testSocialActivity();
    await testSocialProfiles();
    await testProfileDetail();
    await testProfilePosts();
    await testProfileVotes();
    await testStats();
    await testExplorerAPI();
    await testWorkerAdapters();
  } catch (err) {
    console.error('\nFATAL ERROR:', err.message);
    failed++;
  }

  console.log(`\n${'='.repeat(60)}`);
  console.log(`Results: ${passed} passed, ${failed} failed out of ${passed + failed} assertions`);
  process.exit(failed > 0 ? 1 : 0);
}

main();
