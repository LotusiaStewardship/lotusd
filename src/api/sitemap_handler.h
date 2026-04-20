// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_API_SITEMAP_HANDLER_H
#define BITCOIN_API_SITEMAP_HANDLER_H

#include <httpserver.h>
#include <string>

namespace api {

// Handler for /sitemap.xml, /sitemap-*.xml and /robots.txt.
// Generates dynamic sitemaps capped at 50,000 URLs per shard
// (per the sitemaps.org / Google specification).
//
// All responses are served from an in-memory cache populated by the
// background preheater (see Start/StopSitemapPreheater). On a cache miss
// the handler falls back to a synchronous build; on a hit it returns
// instantly and triggers an async rebuild if the entry is past its TTL.
// This guarantees crawlers (Google in particular) never time out on us.
bool HandleSitemap(HTTPRequest *req, const std::string &path);

// True if the given path is a sitemap or robots route this handler owns.
bool IsSitemapPath(const std::string &path);

// Spawns a background thread that builds every sitemap shard (index,
// static, blocks, profiles, addresses, robots) at startup, then re-warms
// the cache periodically so freshly mined blocks and new profiles show up
// without ever blocking a crawler request.
void StartSitemapPreheater();

// Joins the preheater thread and clears the cache.
void StopSitemapPreheater();

} // namespace api

#endif // BITCOIN_API_SITEMAP_HANDLER_H
