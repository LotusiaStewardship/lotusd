// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/dashboard_handler.h>
#include <rpc/protocol.h>
#include <util/system.h>

#include <sqlite3.h>

#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

namespace api {

static const char *DASHBOARD_HTML = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<link rel="icon" type="image/svg+xml" href="/favicon.svg">
<link rel="alternate icon" type="image/x-icon" href="/favicon.ico">
<link rel="apple-touch-icon" href="/favicon.svg">
<link rel="mask-icon" href="/favicon.svg" color="#60a5fa">
<!--SEO_HEAD-->
<script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.7/dist/chart.umd.min.js"></script>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{--bg:#0c0e14;--card:#151820;--border:#1e2230;--text:#c8ccd4;--dim:#6b7280;
--accent:#60a5fa;--accent-light:#1a2744;--green:#34d399;--orange:#fbbf24;--red:#f87171;
--link:#60a5fa;--font:'Segoe UI',system-ui,-apple-system,sans-serif;
--nav-bg:#111318;--nav-border:#1e2230;--hover:#1a1e2a;--table-stripe:#12151c;
--stat-bg:#151820;--chart-border:#1e2230}
body{background:var(--bg);color:var(--text);font-family:var(--font);font-size:14px;line-height:1.5;margin:0}
a{color:var(--link);text-decoration:none;cursor:pointer}
a:hover{text-decoration:underline}

.topnav{background:var(--nav-bg);border-bottom:1px solid var(--nav-border);padding:0 24px;display:flex;align-items:center;height:56px;gap:24px;position:sticky;top:0;z-index:100}
.topnav .logo{display:flex;align-items:center;gap:8px;font-size:18px;font-weight:700;color:var(--accent);white-space:nowrap;cursor:pointer}
.nav-links{display:flex;gap:4px;margin-left:8px}
.nav-link{padding:8px 16px;border-radius:6px;cursor:pointer;font-size:14px;font-weight:500;color:var(--dim);transition:.15s;white-space:nowrap}
.nav-link:hover{color:var(--text);background:var(--hover)}
.nav-link.active{color:var(--accent);background:var(--accent-light);font-weight:600}
.nav-right{margin-left:auto;font-size:11px;color:var(--dim);padding:4px 12px;border-radius:12px;background:var(--card);border:1px solid var(--border);white-space:nowrap;font-weight:500;font-variant-numeric:tabular-nums}

.search-bar{padding:12px 24px;background:var(--nav-bg);border-bottom:1px solid var(--nav-border)}
.search-wrap{max-width:800px;margin:0 auto;position:relative}
.search-wrap input{width:100%;padding:10px 16px 10px 40px;border:1px solid var(--border);border-radius:8px;font-size:14px;outline:none;background:var(--bg);color:var(--text)}
.search-hint{max-width:800px;margin:6px auto 0;font-size:11px;color:var(--dim);padding:0 4px}
.search-wrap input:focus{border-color:var(--accent);box-shadow:0 0 0 3px rgba(96,165,250,.15)}
.search-wrap .search-icon{position:absolute;left:12px;top:50%;transform:translateY(-50%);color:var(--dim);font-size:16px}
.search-btn{position:absolute;right:6px;top:50%;transform:translateY(-50%);padding:6px 18px;background:var(--accent);color:#fff;border:none;border-radius:6px;cursor:pointer;font-weight:600;font-size:13px}
.search-btn:hover{opacity:.9}

.main{max-width:1200px;margin:0 auto;padding:20px 24px}
.panel{display:none}.panel.active{display:block}

.card{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:16px;margin-bottom:16px}
.card h2{font-size:14px;font-weight:600;color:var(--text);margin-bottom:12px;padding-bottom:8px;border-bottom:1px solid var(--border)}

.stat-cards{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:12px;margin-bottom:20px}
.stat-card{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:14px 16px;text-align:center}
.stat-card .sc-label{font-size:12px;color:var(--dim);margin-bottom:4px;text-transform:uppercase;letter-spacing:.3px}
.stat-card .sc-value{font-size:18px;font-weight:700;color:var(--text);font-variant-numeric:tabular-nums}
.stat-card .sc-icon{font-size:20px;margin-bottom:4px}

.sub-tabs{display:flex;gap:2px;margin-bottom:14px}
.sub-tab{padding:5px 14px;border-radius:4px;cursor:pointer;font-size:13px;font-weight:500;color:var(--dim);border:1px solid transparent;transition:.15s}
.sub-tab:hover{color:var(--text);background:var(--hover)}
.sub-tab.active{color:var(--accent);border-color:var(--accent);background:var(--accent-light);font-weight:600}

table{width:100%;border-collapse:collapse}
thead th{text-align:left;color:var(--dim);font-size:12px;text-transform:uppercase;letter-spacing:.4px;
padding:8px 10px;border-bottom:2px solid var(--border);font-weight:600;white-space:nowrap}
tbody td{padding:8px 10px;border-bottom:1px solid var(--border);font-size:13px;font-variant-numeric:tabular-nums}
tbody tr:nth-child(even){background:var(--table-stripe)}
tbody tr:hover td{background:var(--hover)}
.mono{font-family:'SF Mono',SFMono-Regular,Consolas,monospace;font-size:12px}
.hash{white-space:nowrap;display:inline-block;vertical-align:middle}
.text-right{text-align:right}
.text-center{text-align:center}
/* Horizontal-scroll wrapper for tables that would otherwise overflow
   their card on narrow viewports. Used by all social hub previews. */
.table-scroll{overflow-x:auto;-webkit-overflow-scrolling:touch;
  margin:0 -4px;padding:0 4px}
.table-scroll table{min-width:100%;width:max-content}
.table-scroll table.fill{width:100%;min-width:0}
.table-scroll tbody td{white-space:nowrap}
.table-scroll tbody td.wrap{white-space:normal}
.table-scroll::-webkit-scrollbar{height:6px}
.table-scroll::-webkit-scrollbar-thumb{background:var(--border);border-radius:3px}
.plat-badge{display:inline-flex;align-items:center;padding:1px 6px;border-radius:3px;
  background:var(--accent-light);color:var(--accent);font-size:10px;font-weight:600;
  text-transform:uppercase;margin-right:4px;line-height:1.4;vertical-align:middle}
.profile-link{display:inline-flex;align-items:center;gap:4px;white-space:nowrap;max-width:100%}
.profile-link .pn{overflow:hidden;text-overflow:ellipsis;max-width:180px}

.pager{display:flex;align-items:center;justify-content:space-between;margin-top:12px;font-size:13px;color:var(--dim)}
.pager-info{font-size:13px}
.pager-btns{display:flex;gap:4px}
.pager-btn{padding:4px 12px;border:1px solid var(--border);border-radius:4px;cursor:pointer;font-size:13px;background:var(--card);color:var(--text)}
.pager-btn:hover{background:var(--hover)}
.pager-btn.active{background:var(--accent);color:#fff;border-color:var(--accent)}
.pager-btn.disabled{opacity:.4;pointer-events:none}

.chart-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(400px,1fr));gap:16px;margin-bottom:16px}
.chart-box{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:16px}
.chart-box h3{font-size:14px;font-weight:600;margin-bottom:4px;color:var(--text)}
.chart-period{display:flex;gap:2px;margin-bottom:10px}
.chart-period .cp-btn{padding:3px 10px;border-radius:3px;cursor:pointer;font-size:12px;color:var(--dim);border:1px solid transparent}
.chart-period .cp-btn:hover{color:var(--text);background:var(--hover)}
.chart-period .cp-btn.active{color:var(--accent);border-color:var(--accent);background:var(--accent-light)}
.chart-wrap{position:relative;width:100%;height:220px}
.chart-wrap canvas{position:absolute;top:0;left:0;width:100%;height:100%}

.richlist-layout{display:grid;grid-template-columns:1fr 320px;gap:16px}
@media(max-width:900px){.richlist-layout{grid-template-columns:1fr}.chart-grid{grid-template-columns:1fr}}

.badge{display:inline-block;padding:1px 8px;border-radius:10px;font-size:11px;font-weight:700}
.badge-green{background:#064e3b;color:var(--green)}
.badge-red{background:#7f1d1d;color:var(--red)}

.api-list{list-style:none}
.api-list li{padding:10px 14px;border-bottom:1px solid var(--border);display:flex;gap:12px;align-items:flex-start}
.api-list li:last-child{border-bottom:none}
.api-list .method{font-weight:700;color:#fff;background:#2563eb;padding:2px 8px;border-radius:3px;font-size:11px;white-space:nowrap;min-width:42px;text-align:center}
.api-list .path{font-family:monospace;font-size:13px;color:var(--text);font-weight:600;min-width:260px}
.api-list .desc{color:var(--dim);font-size:13px;flex:1}

.empty{color:var(--dim);font-style:italic;padding:16px;text-align:center}

/* Detail pages */
.back-btn{display:inline-flex;align-items:center;gap:6px;padding:6px 14px;border-radius:6px;cursor:pointer;font-size:13px;color:var(--accent);border:1px solid var(--border);background:var(--card);margin-bottom:14px}
.back-btn:hover{background:var(--hover);text-decoration:none}
.detail-header{background:var(--card);border:1px solid var(--border);border-radius:8px;padding:16px;margin-bottom:16px}
.detail-header .dh-title{font-size:13px;color:var(--dim);margin-bottom:4px}
.detail-header .dh-value{font-family:'SF Mono',SFMono-Regular,Consolas,monospace;font-size:13px;color:var(--text);word-break:break-all}
.detail-header .dh-link{color:var(--link);cursor:pointer}
.info-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:1px;background:var(--border);border-radius:8px;overflow:hidden;margin-bottom:16px;border:1px solid var(--border)}
.info-cell{background:var(--card);padding:12px 14px}
.info-cell .ic-label{font-size:11px;color:var(--dim);text-transform:uppercase;letter-spacing:.3px;margin-bottom:2px}
.info-cell .ic-value{font-size:14px;font-weight:600;color:var(--text);font-variant-numeric:tabular-nums}
.io-grid{display:grid;grid-template-columns:1fr 40px 1fr;gap:16px;margin-bottom:16px}
.io-grid .io-arrow{display:flex;align-items:center;justify-content:center;font-size:24px;color:var(--dim)}
.io-box{background:var(--card);border:1px solid var(--border);border-radius:8px;overflow:hidden}
.io-box h3{font-size:13px;font-weight:600;color:var(--text);padding:10px 14px;border-bottom:1px solid var(--border);background:var(--stat-bg)}
.io-row{padding:8px 14px;border-bottom:1px solid var(--border);font-size:13px;display:flex;justify-content:space-between;gap:8px}
.io-row:last-child{border-bottom:none}
.io-row .io-addr{font-family:'SF Mono',SFMono-Regular,Consolas,monospace;font-size:11px;color:var(--link);cursor:pointer;word-break:break-all;flex:1}
.io-row .io-amt{white-space:nowrap;font-weight:600}
.io-row .io-coinbase{color:var(--green);font-style:italic}
@media(max-width:768px){
.topnav{padding:0 12px;gap:12px;overflow-x:auto}
.nav-link{padding:6px 10px;font-size:13px}
.main{padding:12px}
.card{padding:12px}
.card h2{font-size:13px}
.stat-cards{grid-template-columns:repeat(auto-fit,minmax(130px,1fr))}
.stat-card{padding:10px 12px}
.stat-card .sc-value{font-size:15px}
.search-bar{padding:8px 12px}
.io-grid{grid-template-columns:1fr}
.io-grid .io-arrow{transform:rotate(90deg)}
.sub-tab{padding:5px 10px;font-size:12px}
thead th,tbody td{padding:6px 8px;font-size:12px}
.profile-link .pn{max-width:120px}
.hub-grid{grid-template-columns:1fr !important}
}
</style>
</head>
<body>

<div class="topnav">
<div class="logo" onclick="navigate('explorer')">&#x1F338; LOTUS</div>
<div class="nav-links" id="main-nav">
<div class="nav-link active" data-tab="explorer">Explorer</div>
<div class="nav-link" data-tab="network">Network</div>
<div class="nav-link" data-tab="top100">Top 100</div>
<div class="nav-link" data-tab="stats">Stats</div>
<div class="nav-link" data-tab="apidocs">API</div>
<div class="nav-link" data-tab="social">Social</div>
</div>
<div class="nav-right" id="last-update"></div>
</div>

<div class="search-bar">
<div class="search-wrap">
<span class="search-icon">&#x1F50D;</span>
<input id="search-input" type="text" placeholder="Search block height/hash, txid, address, @handle, or platform:handle">
<button class="search-btn" id="search-btn">SEARCH</button>
</div>
<div class="search-hint">Tip: <code>@elonmusk</code> jumps to a Twitter profile, <code>nostr:npub1...</code> to a Nostr profile.</div>
</div>

<div class="main">

<div id="explorer" class="panel active">
<div class="card">
<h2>Latest Blocks</h2>
<div id="explorer-show">
<label style="font-size:13px;color:var(--dim)">Show
<select id="explorer-limit" style="padding:2px 6px;border:1px solid var(--border);border-radius:4px;font-size:13px;background:var(--bg);color:var(--text)">
<option value="10" selected>10</option><option value="25">25</option><option value="50">50</option><option value="100">100</option>
</select> entries</label>
</div>
<div id="explorer-table" class="empty">Loading...</div>
<div id="explorer-pager" class="pager"></div>
</div>
</div>

<div id="blockdetail" class="panel">
<div id="blockdetail-content" class="empty">Loading...</div>
</div>

<div id="txdetail" class="panel">
<div id="txdetail-content" class="empty">Loading...</div>
</div>

<div id="addressdetail" class="panel">
<div id="addressdetail-content" class="empty">Loading...</div>
</div>

<div id="network" class="panel">
<div class="card">
<div class="sub-tabs" id="net-sub-tabs">
<div class="sub-tab active" data-sub="connections">Connections</div>
<div class="sub-tab" data-sub="addnodes">Add Nodes</div>
</div>
<div id="net-connections" class="sub-panel active"><div class="empty">Loading...</div></div>
<div id="net-addnodes" class="sub-panel" style="display:none"><div class="empty">Loading...</div></div>
</div>
</div>

<div id="top100" class="panel">
<div class="richlist-layout">
<div class="card">
<div class="sub-tabs" id="rich-sub-tabs">
<div class="sub-tab active" data-sub="balance">Balance</div>
<div class="sub-tab" data-sub="received">Received</div>
</div>
<div id="rich-balance" class="sub-panel active">
<h2>Top 100 - Current Balance</h2>
<div id="rich-balance-table" class="empty">Loading...</div>
</div>
<div id="rich-received" class="sub-panel" style="display:none">
<h2>Top 100 - Total Received</h2>
<div id="rich-received-table" class="empty">Loading...</div>
</div>
</div>
<div class="card">
<h2>Wealth Distribution</h2>
<canvas id="wealth-chart" style="max-height:280px"></canvas>
<div id="wealth-legend" style="margin-top:12px;font-size:13px"></div>
</div>
</div>
</div>

<div id="stats" class="panel">
<div class="stat-cards" id="stats-cards"><div class="empty" style="grid-column:1/-1">Loading...</div></div>
<div class="chart-grid" id="stats-charts"></div>
</div>

<div id="apidocs" class="panel">
<div class="card">
<h2>Lotus Node REST API</h2>
<p style="color:var(--dim);margin-bottom:12px;font-size:13px">
All endpoints are served from <a href="/api/v1/openapi.json">/api/v1/</a> on this node.
<a href="/api/v1/openapi.json" target="_blank">Download OpenAPI spec (JSON)</a>
</p>
<ul class="api-list" id="api-list"><li class="empty">Loading endpoints...</li></ul>
</div>
</div>

<div id="social" class="panel">
<div class="card">
<div class="sub-tabs" id="social-sub-tabs">
<div class="sub-tab active" data-sub="hub">Hub</div>
<div class="sub-tab" data-sub="activity">Activity</div>
<div class="sub-tab" data-sub="trending">Trending</div>
<div class="sub-tab" data-sub="profiles">Profiles</div>
</div>

<div id="social-hub" class="sub-panel active">
<div class="stat-cards" id="social-hub-stats"><div class="empty" style="grid-column:1/-1">Loading stats...</div></div>
<div class="hub-grid" style="display:grid;grid-template-columns:repeat(auto-fit,minmax(440px,1fr));gap:16px;margin-top:16px">
<div class="card"><h2>Latest Votes <a class="back-btn" style="float:right;margin:0;padding:2px 10px;font-size:11px" onclick="navigate('social/activity')">View all &rarr;</a></h2><div id="social-hub-activity" class="empty">Loading...</div></div>
<div class="card"><h2>Trending Profiles Today <a class="back-btn" style="float:right;margin:0;padding:2px 10px;font-size:11px" onclick="navigate('social/trending')">View all &rarr;</a></h2><div id="social-hub-trending-profiles" class="empty">Loading...</div></div>
<div class="card"><h2>Top Posts Today <a class="back-btn" style="float:right;margin:0;padding:2px 10px;font-size:11px" onclick="navigate('social/trending')">View all &rarr;</a></h2><div id="social-hub-trending-posts" class="empty">Loading...</div></div>
<div class="card"><h2>Profile Leaderboard <a class="back-btn" style="float:right;margin:0;padding:2px 10px;font-size:11px" onclick="navigate('social/profiles')">View all &rarr;</a></h2><div id="social-hub-leaderboard" class="empty">Loading...</div></div>
</div>
</div>

<div id="social-activity" class="sub-panel" style="display:none">
<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;flex-wrap:wrap;gap:8px">
<div><h2 style="border:none;padding:0;margin:0">Vote Activity</h2><div style="font-size:12px;color:var(--dim);margin-top:4px">Live RANK votes across all profiles and platforms.</div></div>
<div style="display:flex;align-items:center;gap:8px">
<label style="font-size:12px;color:var(--dim)">Show
<select id="social-activity-size" style="padding:2px 6px;border:1px solid var(--border);border-radius:4px;background:var(--bg);color:var(--text);font-size:13px">
<option value="25" selected>25</option><option value="50">50</option><option value="100">100</option>
</select></label>
</div>
</div>
<div id="social-activity-table" class="empty">Loading...</div>
<div id="social-activity-pager" class="pager"></div>
</div>

<div id="social-trending" class="sub-panel" style="display:none">
<div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(360px,1fr));gap:16px">
<div class="card"><h2>&#x1F525; Top Profiles Today</h2><div id="social-trending-tp" class="empty">Loading...</div></div>
<div class="card"><h2>&#x1F4C9; Lowest Profiles Today</h2><div id="social-trending-lp" class="empty">Loading...</div></div>
<div class="card"><h2>&#x1F525; Top Posts Today</h2><div id="social-trending-tposts" class="empty">Loading...</div></div>
<div class="card"><h2>&#x1F4C9; Lowest Posts Today</h2><div id="social-trending-lposts" class="empty">Loading...</div></div>
</div>
</div>

<div id="social-profiles" class="sub-panel" style="display:none">
<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;flex-wrap:wrap;gap:8px">
<div><h2 style="border:none;padding:0;margin:0">All Profiles</h2><div style="font-size:12px;color:var(--dim);margin-top:4px">Every social profile ranked on-chain via Lotus RANK.</div></div>
<div style="display:flex;align-items:center;gap:8px">
<input id="social-profile-filter" type="text" placeholder="Filter by handle..." style="padding:6px 10px;border:1px solid var(--border);border-radius:4px;background:var(--bg);color:var(--text);font-size:13px;width:200px">
<label style="font-size:12px;color:var(--dim)">Show
<select id="social-profiles-size" style="padding:2px 6px;border:1px solid var(--border);border-radius:4px;background:var(--bg);color:var(--text);font-size:13px">
<option value="25" selected>25</option><option value="50">50</option><option value="100">100</option><option value="200">200</option>
</select></label>
</div>
</div>
<div id="social-profiles-table" class="empty">Loading...</div>
<div id="social-profiles-pager" class="pager"></div>
</div>
</div>
</div>

<div id="socialprofile" class="panel">
<div id="socialprofile-content" class="empty">Loading profile...</div>
</div>

<div id="socialpost" class="panel">
<div id="socialpost-content" class="empty">Loading post...</div>
</div>

</div>

<script>
const $=s=>document.querySelector(s);
const $$=s=>document.querySelectorAll(s);
const api=async p=>{try{const r=await fetch('/api/v1/'+p);if(!r.ok)return null;const ca=r.headers.get('X-Cached-At');if(ca){const age=Math.floor(Date.now()/1000)-Number(ca);$('#last-update').textContent=age<2?'Live':'Cached '+age+'s ago';$('#last-update').style.color=age<10?'var(--green)':age<30?'var(--orange)':'var(--red)'}else{$('#last-update').textContent='Live';$('#last-update').style.color='var(--green)'}return r.json()}catch(e){return null}};
const esc=s=>String(s??'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
const num=n=>Number(n||0).toLocaleString();
const xpi=sats=>(Number(sats||0)/1e6).toLocaleString(undefined,{minimumFractionDigits:2,maximumFractionDigits:2});
const pct=(v,t)=>t>0?((v/t)*100).toFixed(2):'0.00';
const fmtDate=ts=>ts?new Date(ts*1000).toUTCString():'—';
const fmtSize=b=>{if(b>=1e9)return(b/1e9).toFixed(2)+' GB';if(b>=1e6)return(b/1e6).toFixed(2)+' MB';if(b>=1e3)return(b/1e3).toFixed(2)+' KB';return b+' B'};
const mid=(s,n)=>{if(!s)return'—';n=n||16;if(s.length<=n)return s;const half=Math.floor(n/2);return s.slice(0,half)+'...'+s.slice(-half)};

/* --- NAVIGATION --- */
function showPanel(id){
  $$('.panel').forEach(x=>x.classList.remove('active'));
  const p=document.getElementById(id);
  if(p)p.classList.add('active');
  // Map detail panels back to their parent nav tab so the link stays
  // highlighted while on /block/, /tx/, /address/, /social/...
  let navTab=id;
  if(id==='blockdetail')navTab='explorer';
  else if(id==='txdetail')navTab='explorer';
  else if(id==='addressdetail')navTab='top100';
  else if(id==='socialprofile'||id==='socialpost')navTab='social';
  $$('.nav-link').forEach(x=>{
    x.classList.toggle('active',x.dataset.tab===navTab);
  });
}

function navigate(route,push){
  if(push!==false){
    const url=route==='explorer'?'/dashboard':'/dashboard/'+route;
    history.pushState({route:route},'',url);
  }
  const tabs=['explorer','network','top100','stats','apidocs','social'];
  if(tabs.includes(route)){
    showPanel(route);
    if(route==='social'){
      switchSocialSub('hub');
    }
    loadTab(route);
    return;
  }
  if(route.startsWith('block/')){
    showPanel('blockdetail');
    loadBlockDetail(route.slice(6));
    return;
  }
  if(route.startsWith('tx/')){
    showPanel('txdetail');
    loadTxDetail(route.slice(3));
    return;
  }
  if(route.startsWith('address/')){
    showPanel('addressdetail');
    loadAddressDetail(route.slice(8));
    return;
  }
  if(route.startsWith('social/')){
    const rest=route.slice(7);
    // social/activity, social/trending, social/profiles
    if(rest==='activity'||rest==='trending'||rest==='profiles'){
      showPanel('social');
      switchSocialSub(rest);
      loadSocialSub(rest);
      return;
    }
    // social/{platform}/{profileId}/post/{postId}
    // social/{platform}/{profileId}
    const segs=rest.split('/').filter(Boolean);
    if(segs.length>=4&&segs[2]==='post'){
      showPanel('socialpost');
      loadSocialPost(segs[0],decodeURIComponent(segs[1]),decodeURIComponent(segs[3]));
      return;
    }
    if(segs.length>=2){
      showPanel('socialprofile');
      loadSocialProfile(segs[0],decodeURIComponent(segs[1]));
      return;
    }
    showPanel('social');
    switchSocialSub('hub');
    loadTab('social');
    return;
  }
  showPanel('explorer');
  loadTab('explorer');
}

function switchSocialSub(sub){
  document.querySelectorAll('#social-sub-tabs .sub-tab').forEach(x=>{
    x.classList.toggle('active',x.dataset.sub===sub);
  });
  ['hub','activity','trending','profiles'].forEach(s=>{
    const el=document.getElementById('social-'+s);
    if(el)el.style.display=(s===sub?'block':'none');
  });
}

$$('.nav-link').forEach(t=>t.onclick=function(){navigate(this.dataset.tab)});
window.onpopstate=function(e){navigate(e.state&&e.state.route||'explorer',false)};

function parseRoute(){
  const p=location.pathname.replace(/\/+$/,'').replace(/^\/dashboard\/?/,'');
  if(!p)return 'explorer';
  return p;
}

function wireSubTabs(containerId){
  const c=document.getElementById(containerId);
  if(!c)return;
  c.querySelectorAll('.sub-tab').forEach(t=>t.onclick=function(){
    c.querySelectorAll('.sub-tab').forEach(x=>x.classList.remove('active'));
    this.classList.add('active');
    const parent=c.closest('.card')||c.closest('.panel');
    if(parent){
      parent.querySelectorAll('.sub-panel').forEach(x=>x.style.display='none');
      const target=document.getElementById(containerId.replace('-sub-tabs','-')+this.dataset.sub);
      if(target)target.style.display='block';
    }
  });
}
wireSubTabs('net-sub-tabs');
wireSubTabs('rich-sub-tabs');

// Social sub-tabs use route-based navigation so URLs are crawlable.
document.querySelectorAll('#social-sub-tabs .sub-tab').forEach(t=>{
  t.onclick=function(){
    const sub=this.dataset.sub;
    if(sub==='hub')navigate('social');
    else navigate('social/'+sub);
  };
});

/* --- SEARCH --- */
function doSearch(){
  const q=$('#search-input').value.trim();
  if(!q)return;
  // platform:handle  or  @handle  -> social profile search (default twitter)
  const at=q.match(/^@([A-Za-z0-9_]{1,32})$/);
  if(at){navigate('social/twitter/'+encodeURIComponent(at[1]));return}
  const ph=q.match(/^(twitter|nostr|telegram|unknown):(.+)$/i);
  if(ph){navigate('social/'+ph[1].toLowerCase()+'/'+encodeURIComponent(ph[2]));return}
  if(/^\d+$/.test(q)){
    navigate('block/'+q);
  } else if(/^[0-9a-fA-F]{64}$/.test(q)){
    navigate('tx/'+q);
  } else {
    navigate('address/'+encodeURIComponent(q));
  }
}
$('#search-btn').onclick=doSearch;
$('#search-input').onkeydown=e=>{if(e.key==='Enter')doSearch()};

/* --- EXPLORER TAB --- */
let explorerPage=0;
async function loadExplorer(){
  const limit=parseInt($('#explorer-limit').value)||10;
  const offset=explorerPage*limit;
  const d=await api('blocks?limit='+limit+'&offset='+offset);
  if(!d||!d.data||!d.data.length){$('#explorer-table').innerHTML='<p class="empty">No blocks yet</p>';$('#explorer-pager').innerHTML='';return}
  let h='<table><thead><tr><th>Height</th><th>Block Hash</th><th>Size</th><th># TX</th><th>Difficulty</th><th>Timestamp</th></tr></thead><tbody>';
  d.data.forEach(b=>{
    h+='<tr><td><a onclick="navigate(\'block/'+b.height+'\')"><strong>'+num(b.height)+'</strong></a></td>'+
      '<td class="mono"><a class="hash" title="'+esc(b.hash)+'" onclick="navigate(\'block/'+esc(b.hash)+'\')">'+mid(esc(b.hash),24)+'</a></td>'+
      '<td>'+fmtSize(b.size||0)+'</td>'+
      '<td class="text-center">'+num(b.n_tx)+'</td>'+
      '<td class="text-right">'+Number(b.difficulty||0).toFixed(2)+'</td>'+
      '<td>'+fmtDate(b.time)+'</td></tr>';
  });
  h+='</tbody></table>';
  $('#explorer-table').innerHTML=h;

  const total=d.pagination?d.pagination.total:0;
  const pages=Math.ceil(total/limit);
  const from=offset+1, to=Math.min(offset+limit,total);
  let pg='<div class="pager-info">Showing '+from+' to '+to+' of '+num(total)+' entries</div><div class="pager-btns">';
  pg+='<div class="pager-btn'+(explorerPage===0?' disabled':'')+'" onclick="explorerPage=Math.max(0,explorerPage-1);loadExplorer()">Previous</div>';
  const startP=Math.max(0,explorerPage-2), endP=Math.min(pages,startP+5);
  for(let i=startP;i<endP;i++){
    pg+='<div class="pager-btn'+(i===explorerPage?' active':'')+'" onclick="explorerPage='+i+';loadExplorer()">'+(i+1)+'</div>';
  }
  if(endP<pages)pg+='<div class="pager-btn" onclick="explorerPage='+(pages-1)+';loadExplorer()">'+pages+'</div>';
  pg+='<div class="pager-btn'+(explorerPage>=pages-1?' disabled':'')+'" onclick="explorerPage=Math.min('+(pages-1)+',explorerPage+1);loadExplorer()">Next</div>';
  pg+='</div>';
  $('#explorer-pager').innerHTML=pg;
}
$('#explorer-limit').onchange=()=>{explorerPage=0;loadExplorer()};

/* --- BLOCK DETAIL --- */
async function loadBlockDetail(id){
  const el=$('#blockdetail-content');
  el.innerHTML='<div class="empty">Loading block...</div>';
  const blk=await api('blocks/'+encodeURIComponent(id));
  if(!blk||blk.error){el.innerHTML='<div class="empty">Block not found</div>';return}
  const txs=await api('blocks/'+blk.height+'/txs?limit=100');

  let h='<a class="back-btn" onclick="navigate(\'explorer\')">&#x2190; Back to blocks</a>';
  h+='<div class="detail-header"><div class="dh-title">Block Hash</div><div class="dh-value">'+esc(blk.hash)+'</div></div>';
  h+='<div class="info-grid">';
  h+='<div class="info-cell"><div class="ic-label">Height</div><div class="ic-value">'+num(blk.height)+'</div></div>';
  h+='<div class="info-cell"><div class="ic-label">Confirmations</div><div class="ic-value">'+num(blk.confirmations)+'</div></div>';
  h+='<div class="info-cell"><div class="ic-label">Difficulty</div><div class="ic-value">'+Number(blk.difficulty||0).toFixed(6)+'</div></div>';
  h+='<div class="info-cell"><div class="ic-label">Size</div><div class="ic-value">'+fmtSize(blk.size||0)+'</div></div>';
  h+='<div class="info-cell"><div class="ic-label">Transactions</div><div class="ic-value">'+num(blk.n_tx)+'</div></div>';
  h+='<div class="info-cell"><div class="ic-label">Timestamp</div><div class="ic-value">'+fmtDate(blk.time)+'</div></div>';
  h+='</div>';

  if(blk.previous_hash){
    h+='<div class="detail-header"><div class="dh-title">Previous Block</div><div class="dh-value"><a class="dh-link" onclick="navigate(\'block/'+esc(blk.previous_hash)+'\')">'+esc(blk.previous_hash)+'</a></div></div>';
  }

  if(txs&&txs.data&&txs.data.length){
    h+='<div class="card"><h2>Transactions ('+txs.data.length+' Total)</h2>';
    h+='<table><thead><tr><th>Transaction ID</th><th>Position</th></tr></thead><tbody>';
    txs.data.forEach(tx=>{
      h+='<tr><td class="mono"><a onclick="navigate(\'tx/'+esc(tx.txid)+'\')">'+esc(tx.txid)+'</a></td>'+
        '<td>'+tx.block_pos+'</td></tr>';
    });
    h+='</tbody></table></div>';
  }

  el.innerHTML=h;
}

/* --- TX DETAIL --- */
async function loadTxDetail(txid){
  const el=$('#txdetail-content');
  el.innerHTML='<div class="empty">Loading transaction...</div>';
  const [tx,inputs,outputs]=await Promise.all([
    api('txs/'+txid),
    api('txs/'+txid+'/inputs'),
    api('txs/'+txid+'/outputs')
  ]);
  if(!tx||tx.error){el.innerHTML='<div class="empty">Transaction not found</div>';return}

  const isCoinbase=tx.input_count===0;
  const feeSats=tx.fee_sats||0;

  let h='<a class="back-btn" onclick="history.back()">&#x2190; Back</a>';
  h+='<div class="detail-header"><div class="dh-title">Transaction ID</div><div class="dh-value">'+esc(tx.txid)+'</div></div>';
  h+='<div class="info-grid">';
  h+='<div class="info-cell"><div class="ic-label">Block Height</div><div class="ic-value"><a onclick="navigate(\'block/'+tx.block_height+'\')">'+num(tx.block_height)+'</a></div></div>';
  h+='<div class="info-cell"><div class="ic-label">Confirmations</div><div class="ic-value">'+num(tx.confirmations)+'</div></div>';
  h+='<div class="info-cell"><div class="ic-label">Inputs</div><div class="ic-value">'+num(tx.input_count)+'</div></div>';
  h+='<div class="info-cell"><div class="ic-label">Outputs</div><div class="ic-value">'+num(tx.output_count)+'</div></div>';
  h+='<div class="info-cell"><div class="ic-label">Total Output</div><div class="ic-value">'+xpi(tx.output_value_sats)+' XPI</div></div>';
  h+='<div class="info-cell"><div class="ic-label">Fee</div><div class="ic-value">'+num(feeSats)+' sats</div></div>';
  h+='</div>';

  h+='<div class="io-grid">';

  h+='<div class="io-box"><h3>Inputs'+(isCoinbase?' (Coinbase)':'')+'</h3>';
  if(isCoinbase){
    h+='<div class="io-row"><span class="io-coinbase">New Coins (Coinbase)</span></div>';
  } else if(inputs&&inputs.length){
    inputs.forEach(inp=>{
      const addr=inp.address||'Unknown';
      h+='<div class="io-row"><span class="io-addr" onclick="navigate(\'address/'+esc(addr)+'\')">'+esc(addr)+'</span><span class="io-amt">'+xpi(inp.value_sats)+' XPI</span></div>';
    });
  } else {
    h+='<div class="io-row"><span class="io-coinbase">No inputs</span></div>';
  }
  h+='</div>';

  h+='<div class="io-arrow">&#x2192;</div>';

  h+='<div class="io-box"><h3>Outputs</h3>';
  if(outputs&&outputs.length){
    outputs.forEach(out=>{
      const addr=out.address;
      if(addr){
        h+='<div class="io-row"><span class="io-addr" onclick="navigate(\'address/'+esc(addr)+'\')">'+esc(addr)+'</span><span class="io-amt'+(out.spent?'':' style="color:var(--green)"')+'">'+xpi(out.value_sats)+' XPI</span></div>';
      } else {
        const label=out.value_sats>0?'OP_RETURN (burn)':'OP_RETURN';
        h+='<div class="io-row"><span style="color:var(--dim);font-style:italic">'+label+'</span><span class="io-amt" style="color:var(--red)">'+xpi(out.value_sats)+' XPI</span></div>';
      }
    });
  } else {
    h+='<div class="io-row"><span style="color:var(--dim)">No outputs</span></div>';
  }
  h+='</div>';
  h+='</div>';

  el.innerHTML=h;
}

/* --- ADDRESS DETAIL --- */
let addrTxPage=0,addrTxAddr='';
async function loadAddressDetail(addr){
  const el=$('#addressdetail-content');
  el.innerHTML='<div class="empty">Loading address...</div>';
  addrTxAddr=addr;
  addrTxPage=0;
  const info=await api('addresses/'+encodeURIComponent(addr));
  if(!info||info.error){el.innerHTML='<div class="empty">Address not found or has no activity</div>';return}

  let h='<a class="back-btn" onclick="history.back()">&#x2190; Back</a>';
  h+='<div class="detail-header"><div class="dh-title">Address</div><div class="dh-value">'+esc(info.address)+'</div></div>';
  h+='<div class="info-grid">';
  h+='<div class="info-cell"><div class="ic-label">Balance (XPI)</div><div class="ic-value" style="color:var(--green)">'+xpi(info.balance_sats)+'</div></div>';
  h+='<div class="info-cell"><div class="ic-label">Total Received (XPI)</div><div class="ic-value">'+xpi(info.received_sats)+'</div></div>';
  h+='<div class="info-cell"><div class="ic-label">Total Sent (XPI)</div><div class="ic-value">'+xpi(info.sent_sats)+'</div></div>';
  h+='<div class="info-cell"><div class="ic-label">Transactions</div><div class="ic-value">'+num(info.tx_count)+'</div></div>';
  h+='<div class="info-cell"><div class="ic-label">UTXOs</div><div class="ic-value">'+num(info.utxo_count)+'</div></div>';
  h+='<div class="info-cell"><div class="ic-label">First Seen Block</div><div class="ic-value"><a onclick="navigate(\'block/'+info.first_height+'\')">'+num(info.first_height)+'</a></div></div>';
  h+='</div>';

  h+='<div class="card"><h2>Latest Transactions</h2>';
  h+='<div id="addr-tx-list" class="empty">Loading...</div>';
  h+='<div id="addr-tx-pager" class="pager"></div>';
  h+='</div>';

  el.innerHTML=h;
  loadAddrTxPage();
}

async function loadAddrTxPage(){
  const limit=25;
  const offset=addrTxPage*limit;
  const d=await api('addresses/'+encodeURIComponent(addrTxAddr)+'/txs?limit='+limit+'&offset='+offset);
  if(!d||!d.data||!d.data.length){$('#addr-tx-list').innerHTML='<p class="empty">No transactions</p>';$('#addr-tx-pager').innerHTML='';return}

  let h='<table><thead><tr><th>Block Height</th><th>Transaction ID</th><th>Amount (XPI)</th></tr></thead><tbody>';
  d.data.forEach(tx=>{
    const amt=tx.net_value||0;
    const color=amt>=0?'color:var(--green)':'color:var(--red)';
    const sign=amt>=0?'+':'';
    h+='<tr><td><a onclick="navigate(\'block/'+tx.block_height+'\')">'+num(tx.block_height)+'</a></td>'+
      '<td class="mono"><a onclick="navigate(\'tx/'+esc(tx.txid)+'\')">'+mid(esc(tx.txid),24)+'</a></td>'+
      '<td class="text-right" style="'+color+';font-weight:600">'+sign+xpi(amt)+'</td></tr>';
  });
  h+='</tbody></table>';
  $('#addr-tx-list').innerHTML=h;

  const total=d.pagination?d.pagination.total:0;
  const pages=Math.ceil(total/limit);
  const from=offset+1, to=Math.min(offset+limit,total);
  let pg='<div class="pager-info">Showing '+from+' to '+to+' of '+num(total)+'</div><div class="pager-btns">';
  pg+='<div class="pager-btn'+(addrTxPage===0?' disabled':'')+'" onclick="addrTxPage=Math.max(0,addrTxPage-1);loadAddrTxPage()">Previous</div>';
  pg+='<div class="pager-btn'+(addrTxPage>=pages-1?' disabled':'')+'" onclick="addrTxPage=Math.min('+(pages-1)+',addrTxPage+1);loadAddrTxPage()">Next</div>';
  pg+='</div>';
  $('#addr-tx-pager').innerHTML=pg;
}

/* --- NETWORK TAB --- */
async function loadNetwork(){
  const [peers,nodes]=await Promise.all([api('network/peers'),api('network/nodes')]);
  if(peers&&peers.length){
    let h='<div style="margin-bottom:8px;font-size:13px;color:var(--dim)">'+peers.length+' connections</div>';
    h+='<table><thead><tr><th>Address</th><th>Protocol</th><th>Sub-version</th><th>Direction</th><th>Ping</th></tr></thead><tbody>';
    peers.forEach(p=>{
      const ping=p.ping_ms!=null&&p.ping_ms>=0?(p.ping_ms).toFixed(0)+' ms':'—';
      h+='<tr><td class="mono" style="font-size:12px">'+esc(p.addr)+'</td>'+
        '<td>'+esc(p.protocol_version||70016)+'</td>'+
        '<td style="max-width:260px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap">'+esc(p.subver||p.user_agent||'')+'</td>'+
        '<td>'+(p.inbound?'Inbound':'Outbound')+'</td><td>'+ping+'</td></tr>';
    });
    h+='</tbody></table>';
    $('#net-connections').innerHTML=h;
  } else { $('#net-connections').innerHTML='<p class="empty">No peers connected</p>'; }

  if(nodes){
    let h='<table><thead><tr><th>#</th><th>addnode command</th></tr></thead><tbody>';
    (nodes.addnode||[]).forEach((s,i)=>{h+='<tr><td>'+(i+1)+'</td><td class="mono" style="font-size:12px">'+esc(s)+'</td></tr>'});
    h+='</tbody></table>';
    if(nodes.onetry&&nodes.onetry.length){
      h+='<h2 style="margin-top:16px">One-try</h2><table><thead><tr><th>#</th><th>command</th></tr></thead><tbody>';
      nodes.onetry.forEach((s,i)=>{h+='<tr><td>'+(i+1)+'</td><td class="mono" style="font-size:12px">'+esc(s)+'</td></tr>'});
      h+='</tbody></table>';
    }
    $('#net-addnodes').innerHTML=h;
  } else { $('#net-addnodes').innerHTML='<p class="empty">Not available</p>'; }
}

/* --- TOP 100 TAB --- */
let wealthChart=null;
async function loadTop100(){
  const [bal,rcv,wealth]=await Promise.all([api('addresses?limit=100&sort=balance'),api('addresses?limit=100&sort=received'),api('addresses?mode=wealth')]);
  if(bal&&bal.data&&bal.data.length){
    const totalSats=bal.data.reduce((s,a)=>s+(a.balance_sats||0),0);
    let h='<table><thead><tr><th>#</th><th>Address</th><th>Balance (XPI)</th><th>%</th></tr></thead><tbody>';
    bal.data.forEach((a,i)=>{
      h+='<tr><td>'+(i+1)+'</td><td class="mono" style="font-size:12px"><a onclick="navigate(\'address/'+esc(a.address)+'\')">'+esc(a.address)+'</a></td>'+
        '<td class="text-right">'+xpi(a.balance_sats)+'</td><td class="text-right">'+pct(a.balance_sats,totalSats)+'</td></tr>';
    });
    h+='</tbody></table>';
    $('#rich-balance-table').innerHTML=h;
  } else { $('#rich-balance-table').innerHTML='<p class="empty">No data</p>'; }

  if(rcv&&rcv.data&&rcv.data.length){
    let h='<table><thead><tr><th>#</th><th>Address</th><th>Total Received (XPI)</th></tr></thead><tbody>';
    rcv.data.forEach((a,i)=>{
      h+='<tr><td>'+(i+1)+'</td><td class="mono" style="font-size:12px"><a onclick="navigate(\'address/'+esc(a.address)+'\')">'+esc(a.address)+'</a></td>'+
        '<td class="text-right">'+xpi(a.received_sats||a.balance_sats)+'</td></tr>';
    });
    h+='</tbody></table>';
    $('#rich-received-table').innerHTML=h;
  } else { $('#rich-received-table').innerHTML='<p class="empty">No data</p>'; }

  const colors=['#ef5350','#66bb6a','#42a5f5','#ffa726','#ab47bc','#424242'];
  function renderWealth(labels,values){
    const total=values.reduce((s,v)=>s+v,0);
    if(wealthChart){wealthChart.destroy();wealthChart=null}
    const ctx=document.getElementById('wealth-chart');
    if(ctx){wealthChart=new Chart(ctx,{type:'doughnut',data:{labels:labels,datasets:[{data:values,backgroundColor:colors.slice(0,labels.length),borderWidth:1,borderColor:'#151820'}]},options:{responsive:true,maintainAspectRatio:true,plugins:{legend:{display:false}}}})}
    let lg='<table style="font-size:13px"><thead><tr><th></th><th>Tier</th><th>Amount (XPI)</th><th>%</th></tr></thead><tbody>';
    labels.forEach((l,i)=>{lg+='<tr><td><span style="display:inline-block;width:12px;height:12px;border-radius:2px;background:'+(colors[i]||'#888')+'"></span></td><td>'+esc(l)+'</td><td class="text-right">'+xpi(values[i])+'</td><td class="text-right">'+pct(values[i],total)+'</td></tr>'});
    lg+='<tr style="font-weight:700"><td></td><td>Total</td><td class="text-right">'+xpi(total)+'</td><td class="text-right">100.00</td></tr></tbody></table>';
    $('#wealth-legend').innerHTML=lg;
  }
  if(wealth&&wealth.buckets&&wealth.buckets.length){
    renderWealth(wealth.buckets.map(b=>b.label||b.range),wealth.buckets.map(b=>b.total_sats||0));
  } else if(bal&&bal.data&&bal.data.length){
    const s=i=>bal.data.slice(i,i+25).reduce((s,a)=>s+(a.balance_sats||0),0);
    renderWealth(['Top 1-25','Top 26-50','Top 51-75','Top 76-100'],[s(0),s(25),s(50),s(75)]);
  }
}

/* --- STATS TAB --- */
const chartInstances={};
async function loadStats(){
  const cards=await api('stats/cards');
  if(cards){
    const hashrate=cards.hashrate!=null?(cards.hashrate/1e6).toFixed(3):'—';
    const diff=Number(cards.difficulty||0).toFixed(5);
    const mp=(cards.mempool_count||0)+' ('+fmtSize(cards.mempool_bytes||0)+')';
    const supply=xpi(cards.total_supply_sats);
    const burned=xpi(cards.burned_sats);
    const inflation=cards.total_supply_sats>0?((cards.burned_sats||0)/cards.total_supply_sats*100).toFixed(3):'0';
    let h='';
    [['&#x2699;','Hashrate (MH/s)',hashrate],['&#x26A1;','Difficulty','&#x20B3; '+diff],['&#x1F4E6;','Mempool',mp],
     ['&#x1F4B0;','Total Supply (XPI)',supply],['&#x1F525;','Burned Supply (XPI)',burned],['&#x1F4C8;','Burn Rate %','&#x20B3; '+inflation]
    ].forEach(([icon,label,value])=>{
      h+='<div class="stat-card"><div class="sc-icon">'+icon+'</div><div class="sc-label">'+label+'</div><div class="sc-value">'+value+'</div></div>';
    });
    $('#stats-cards').innerHTML=h;
  }
  let chartsHtml='';
  const chartDefs=[
    {id:'chart-difficulty',title:'Block Difficulty',periods:['week','month','quarter','year'],defaultP:'week',field:'difficulty',type:'line',color:'#42a5f5'},
    {id:'chart-hashrate',title:'Network Hashrate',periods:['week','month','quarter','year'],defaultP:'week',field:'hashrate',type:'line',color:'#66bb6a'},
    {id:'chart-mempool',title:'Mempool Transactions',periods:['day','week','month'],defaultP:'day',field:'tx_count',type:'bar',color:'#ef5350',mempool:true},
    {id:'chart-supply',title:'Total Supply (XPI)',periods:['week','month','quarter','year'],defaultP:'month',field:'total_supply_sats',type:'line',color:'#ffa726',isSats:true},
    {id:'chart-burned',title:'Burned Supply (XPI)',periods:['week','month','quarter','year'],defaultP:'month',field:'burned_supply_sats',type:'line',color:'#ef5350',isSats:true}
  ];
  chartDefs.forEach(cd=>{
    let pbtns='';cd.periods.forEach(p=>{pbtns+='<span class="cp-btn'+(p===cd.defaultP?' active':'')+'" data-chart="'+cd.id+'" data-period="'+p+'" onclick="loadChart(\''+cd.id+'\',\''+p+'\')">'+p.charAt(0).toUpperCase()+p.slice(1)+'</span>'});
    chartsHtml+='<div class="chart-box"><h3>'+cd.title+'</h3><div class="chart-period">'+pbtns+'</div><div class="chart-wrap"><canvas id="'+cd.id+'"></canvas></div></div>';
  });
  $('#stats-charts').innerHTML=chartsHtml;
  window._chartDefs=chartDefs;
  chartDefs.forEach(cd=>loadChart(cd.id,cd.defaultP));
}
window.loadChart=async function(chartId,period){
  const cd=window._chartDefs.find(c=>c.id===chartId);if(!cd)return;
  $$('[data-chart="'+chartId+'"]').forEach(b=>{b.classList.remove('active');if(b.dataset.period===period)b.classList.add('active')});
  const endpoint=cd.mempool?'mempool/history?period='+period:'stats/charts?period='+period;
  const d=await api(endpoint);if(!d||!d.series||!d.series.length)return;
  const labels=d.series.map(p=>{const dt=new Date(p.ts*1000);return dt.toLocaleDateString(undefined,{month:'short',day:'numeric'})+' '+dt.toLocaleTimeString(undefined,{hour:'2-digit',minute:'2-digit'})});
  let values=d.series.map(p=>p[cd.field]||0);if(cd.isSats)values=values.map(v=>v/1e6);
  if(chartInstances[chartId]){chartInstances[chartId].destroy()}
  const ctx=document.getElementById(chartId);if(!ctx)return;
  chartInstances[chartId]=new Chart(ctx,{type:cd.type,
    data:{labels:labels,datasets:[{label:cd.title,data:values,borderColor:cd.color,backgroundColor:cd.type==='bar'?cd.color+'99':cd.color+'22',borderWidth:2,pointRadius:0,fill:cd.type==='line',tension:.3}]},
    options:{responsive:true,maintainAspectRatio:false,plugins:{legend:{display:false}},
      scales:{x:{display:true,ticks:{maxTicksLimit:8,font:{size:10},color:'#999'},grid:{display:false}},y:{display:true,ticks:{font:{size:10},color:'#666',callback:function(v){return cd.isSats?num(v):v}},grid:{color:'#1e2230'}}},
      interaction:{intersect:false,mode:'index'}}});
};

/* --- API DOCS TAB --- */
async function loadAPIDocs(){
  const spec=await api('openapi.json');
  if(!spec||!spec.paths){$('#api-list').innerHTML='<li class="empty">Could not load API spec</li>';return}
  let h='';
  Object.keys(spec.paths).sort().forEach(p=>{
    Object.keys(spec.paths[p]).forEach(m=>{
      const op=spec.paths[p][m];const method=m.toUpperCase();
      const mbg=method==='POST'?'background:#1565c0':'background:#2563eb';
      h+='<li><span class="method" style="'+mbg+'">'+method+'</span><span class="path">'+esc(p)+'</span><span class="desc">'+esc(op.summary||op.description||'')+'</span></li>';
    });
  });
  $('#api-list').innerHTML=h;
}

/* --- SOCIAL --- */
const SOCIAL_PLATFORM_ICONS={twitter:'&#x1D54F;',nostr:'&#x26A1;',telegram:'&#x2708;',unknown:'&#x2753;'};
function platBadge(p){const k=(p||'unknown').toLowerCase();return '<span class="plat-badge">'+esc(k)+'</span>'}
function profileLink(platform,id,opts){opts=opts||{};const full=String(id||'');const t=opts.short?mid(full,28):full;return '<a class="profile-link" title="'+esc(full)+'" onclick="navigate(\'social/'+esc(platform||'unknown')+'/'+encodeURIComponent(full)+'\')">'+platBadge(platform)+'<span class="pn">'+esc(t)+'</span></a>'}
function postLink(platform,profileId,postId){return '<a onclick="navigate(\'social/'+esc(platform||'unknown')+'/'+encodeURIComponent(profileId||'')+'/post/'+encodeURIComponent(postId||'')+'\')">'+esc(postId||'—')+'</a>'}
function externalPostLink(platform,profileId,postId){
  if(!postId)return '—';
  let url=null;
  if(platform==='twitter')url='https://x.com/'+encodeURIComponent(profileId)+'/status/'+encodeURIComponent(postId);
  else if(platform==='nostr')url='https://snort.social/e/'+encodeURIComponent(postId);
  else if(platform==='telegram')url='https://t.me/'+encodeURIComponent(profileId)+'/'+encodeURIComponent(postId);
  if(!url)return esc(postId);
  return '<a target="_blank" rel="noopener noreferrer" href="'+esc(url)+'">'+esc(postId)+' &#x2197;</a>';
}
function sentimentHtml(s,sats){
  const isPos=s==='positive'||s>0;
  const color=isPos?'var(--green)':'var(--red)';
  const arrow=isPos?'&#x2191;':'&#x2193;';
  const label=isPos?'positive':'negative';
  return '<span style="color:'+color+';font-weight:600">'+arrow+' '+label+(sats!=null?' &middot; '+num(sats)+' sats':'')+'</span>';
}
function ratioPill(pos,neg){
  const t=Math.max(0,Number(pos||0))+Math.max(0,Number(neg||0));
  if(t===0)return '<span style="color:var(--dim);font-size:11px">no votes</span>';
  const p=(Number(pos||0)/t)*100;
  const bg=p>=66?'#064e3b':p>=33?'#78350f':'#7f1d1d';
  const fg=p>=66?'var(--green)':p>=33?'var(--orange)':'var(--red)';
  return '<span style="display:inline-block;padding:1px 8px;border-radius:10px;font-size:11px;font-weight:700;background:'+bg+';color:'+fg+'">'+p.toFixed(0)+'% &uarr; / '+(100-p).toFixed(0)+'% &darr;</span>';
}
function rankCell(r){
  const n=Number(r||0);
  const c=n>0?'var(--green)':n<0?'var(--red)':'var(--dim)';
  return '<span style="color:'+c+';font-weight:600">'+(n>=0?'+':'')+num(n)+'</span>';
}

function loadSocial(){loadSocialHub()}

async function loadSocialHub(){
  // Hub stats: total profiles, total votes, total sats burned (positive+neg)
  const [profilesPayload,activityPayload,topProf,topPosts]=await Promise.all([
    api('social/profiles?page=1&pageSize=200'),
    api('social/activity?page=1&pageSize=10'),
    api('social/stats/profiles/top'),
    api('social/stats/posts/top')
  ]);
  const profiles=(profilesPayload&&profilesPayload.profiles)||[];
  const totalProfiles=profilesPayload?(profilesPayload.numPages*200):0;
  let totalSats=0,totalPos=0,totalNeg=0;
  profiles.forEach(p=>{totalPos+=Number(p.votesPositive||0);totalNeg+=Number(p.votesNegative||0)});
  const cards=[
    ['&#x1F465;','Profiles Tracked',num(profiles.length)+(profilesPayload&&profilesPayload.numPages>1?'+':'')],
    ['&#x1F44D;','Positive Votes',num(totalPos)],
    ['&#x1F44E;','Negative Votes',num(totalNeg)],
    ['&#x1F525;','Top Profile',profiles[0]?profiles[0].id:'—']
  ];
  let sh='';cards.forEach(([i,l,v])=>{sh+='<div class="stat-card"><div class="sc-icon">'+i+'</div><div class="sc-label">'+l+'</div><div class="sc-value" style="font-size:15px;word-break:break-all">'+esc(v)+'</div></div>'});
  $('#social-hub-stats').innerHTML=sh;

  // Activity preview — wrap in .table-scroll so cells stay nowrap and
  // the whole table scrolls horizontally on narrow viewports.
  const acts=(activityPayload&&activityPayload.votes)||[];
  if(acts.length){
    let h='<div class="table-scroll"><table><thead><tr><th>Profile</th><th>Vote</th><th>Post</th></tr></thead><tbody>';
    acts.slice(0,10).forEach(v=>{
      h+='<tr><td>'+profileLink(v.platform||'twitter',v.profileId||'',{short:true})+'</td>'+
        '<td>'+sentimentHtml(v.sentiment,v.sats)+'</td>'+
        '<td class="mono" style="font-size:11px">'+postLink(v.platform||'twitter',v.profileId,v.postId)+'</td></tr>';
    });
    h+='</tbody></table></div>';$('#social-hub-activity').innerHTML=h;
  } else { $('#social-hub-activity').innerHTML='<p class="empty">No RANK votes yet</p>'; }

  // Trending profiles preview
  const tp=Array.isArray(topProf)?topProf:[];
  if(tp.length){
    let h='<div class="table-scroll"><table class="fill"><thead><tr><th>#</th><th>Profile</th><th class="text-right">Ranking</th></tr></thead><tbody>';
    tp.slice(0,10).forEach((p,i)=>{
      h+='<tr><td>'+(i+1)+'</td><td>'+profileLink(p.platform,p.profileId,{short:true})+'</td><td class="text-right">'+rankCell(p.ranking)+'</td></tr>';
    });
    h+='</tbody></table></div>';$('#social-hub-trending-profiles').innerHTML=h;
  } else { $('#social-hub-trending-profiles').innerHTML='<p class="empty">No data yet</p>'; }

  // Top posts preview
  const tposts=Array.isArray(topPosts)?topPosts:[];
  if(tposts.length){
    let h='<div class="table-scroll"><table><thead><tr><th>#</th><th>Profile</th><th>Post</th><th class="text-right">Ranking</th></tr></thead><tbody>';
    tposts.slice(0,10).forEach((p,i)=>{
      h+='<tr><td>'+(i+1)+'</td><td>'+profileLink(p.platform,p.profileId,{short:true})+'</td>'+
        '<td class="mono" style="font-size:11px">'+postLink(p.platform,p.profileId,p.postId)+'</td>'+
        '<td class="text-right">'+rankCell(p.ranking)+'</td></tr>';
    });
    h+='</tbody></table></div>';$('#social-hub-trending-posts').innerHTML=h;
  } else { $('#social-hub-trending-posts').innerHTML='<p class="empty">No data yet</p>'; }

  // Leaderboard preview (top 10 from full profiles list)
  if(profiles.length){
    let h='<div class="table-scroll"><table class="fill"><thead><tr><th>#</th><th>Profile</th><th class="text-right">Ranking</th><th class="text-right">Votes</th></tr></thead><tbody>';
    profiles.slice(0,10).forEach((p,i)=>{
      h+='<tr><td>'+(i+1)+'</td><td>'+profileLink(p.platform,p.id||p.profileId,{short:true})+'</td>'+
        '<td class="text-right">'+rankCell(p.ranking)+'</td>'+
        '<td class="text-right">'+ratioPill(p.votesPositive,p.votesNegative)+'</td></tr>';
    });
    h+='</tbody></table></div>';$('#social-hub-leaderboard').innerHTML=h;
  } else { $('#social-hub-leaderboard').innerHTML='<p class="empty">No profiles yet</p>'; }
}

function loadSocialSub(sub){
  if(sub==='activity')loadSocialActivity();
  else if(sub==='trending')loadSocialTrending();
  else if(sub==='profiles')loadSocialProfiles();
}

let socActPage=1;
async function loadSocialActivity(){
  const ps=parseInt($('#social-activity-size').value)||25;
  const d=await api('social/activity?page='+socActPage+'&pageSize='+ps);
  const votes=(d&&d.votes)||[];
  const numPages=d?d.numPages:1;
  if(!votes.length){$('#social-activity-table').innerHTML='<p class="empty">No vote activity</p>';$('#social-activity-pager').innerHTML='';return}
  let h='<div class="table-scroll"><table><thead><tr><th>TXID</th><th>First Seen</th><th>Profile</th><th>Vote</th><th>Post</th><th class="text-right">Sats</th></tr></thead><tbody>';
  votes.forEach(v=>{
    h+='<tr>'+
      '<td class="mono" style="font-size:11px"><a onclick="navigate(\'tx/'+esc(v.txid)+'\')">'+mid(esc(v.txid),18)+'</a></td>'+
      '<td>'+fmtDate(v.firstSeen||v.timestamp||v.block_time)+'</td>'+
      '<td>'+profileLink(v.platform||'twitter',v.profileId||'',{short:true})+'</td>'+
      '<td>'+sentimentHtml(v.sentiment,v.sats)+'</td>'+
      '<td class="mono" style="font-size:11px">'+postLink(v.platform||'twitter',v.profileId,v.postId)+'</td>'+
      '<td class="text-right">'+num(v.sats||0)+'</td></tr>';
  });
  h+='</tbody></table></div>';$('#social-activity-table').innerHTML=h;
  renderPager('social-activity-pager',socActPage,numPages,n=>{socActPage=n;loadSocialActivity()});
}

async function loadSocialTrending(){
  const [tp,lp,tposts,lposts]=await Promise.all([
    api('social/stats/profiles/top'),
    api('social/stats/profiles/bottom'),
    api('social/stats/posts/top'),
    api('social/stats/posts/bottom')
  ]);
  function profTbl(arr){
    if(!arr||!arr.length)return '<p class="empty">No data yet</p>';
    let h='<div class="table-scroll"><table class="fill"><thead><tr><th>#</th><th>Profile</th><th class="text-right">Ranking</th></tr></thead><tbody>';
    arr.forEach((p,i)=>{h+='<tr><td>'+(i+1)+'</td><td>'+profileLink(p.platform,p.profileId)+'</td><td class="text-right">'+rankCell(p.ranking)+'</td></tr>'});
    h+='</tbody></table></div>';return h;
  }
  function postTbl(arr){
    if(!arr||!arr.length)return '<p class="empty">No data yet</p>';
    let h='<div class="table-scroll"><table><thead><tr><th>#</th><th>Profile</th><th>Post</th><th class="text-right">Ranking</th></tr></thead><tbody>';
    arr.forEach((p,i)=>{h+='<tr><td>'+(i+1)+'</td><td>'+profileLink(p.platform,p.profileId,{short:true})+'</td>'+
      '<td class="mono" style="font-size:11px">'+postLink(p.platform,p.profileId,p.postId)+'</td>'+
      '<td class="text-right">'+rankCell(p.ranking)+'</td></tr>'});
    h+='</tbody></table></div>';return h;
  }
  $('#social-trending-tp').innerHTML=profTbl(tp);
  $('#social-trending-lp').innerHTML=profTbl(lp);
  $('#social-trending-tposts').innerHTML=postTbl(tposts);
  $('#social-trending-lposts').innerHTML=postTbl(lposts);
}

let socProfPage=1;
async function loadSocialProfiles(){
  const ps=parseInt($('#social-profiles-size').value)||25;
  const filter=($('#social-profile-filter')&&$('#social-profile-filter').value||'').trim().toLowerCase();
  const d=await api('social/profiles?page='+socProfPage+'&pageSize='+ps);
  let profiles=(d&&d.profiles)||[];
  const numPages=d?d.numPages:1;
  if(filter)profiles=profiles.filter(p=>String(p.id||'').toLowerCase().includes(filter));
  if(!profiles.length){$('#social-profiles-table').innerHTML='<p class="empty">No profiles match</p>';$('#social-profiles-pager').innerHTML='';return}
  let h='<div class="table-scroll"><table class="fill"><thead><tr><th>#</th><th>Profile</th><th class="text-right">Ranking</th><th class="text-right">+Votes</th><th class="text-right">-Votes</th><th class="text-right">Ratio</th></tr></thead><tbody>';
  const startN=(socProfPage-1)*ps;
  profiles.forEach((p,i)=>{
    h+='<tr><td>'+(startN+i+1)+'</td><td>'+profileLink(p.platform,p.id||p.profileId)+'</td>'+
      '<td class="text-right">'+rankCell(p.ranking)+'</td>'+
      '<td class="text-right" style="color:var(--green)">+'+num(p.votesPositive||0)+'</td>'+
      '<td class="text-right" style="color:var(--red)">'+num(p.votesNegative||0)+'</td>'+
      '<td class="text-right">'+ratioPill(p.votesPositive,p.votesNegative)+'</td></tr>';
  });
  h+='</tbody></table></div>';$('#social-profiles-table').innerHTML=h;
  renderPager('social-profiles-pager',socProfPage,numPages,n=>{socProfPage=n;loadSocialProfiles()});
}

function renderPager(elId,page,numPages,onJump){
  const el=document.getElementById(elId);if(!el)return;
  if(numPages<=1){el.innerHTML='';return}
  let pg='<div class="pager-info">Page '+page+' of '+num(numPages)+'</div><div class="pager-btns">';
  pg+='<div class="pager-btn'+(page<=1?' disabled':'')+'" data-jump="'+(page-1)+'">Previous</div>';
  const startP=Math.max(1,page-2),endP=Math.min(numPages,startP+4);
  for(let i=startP;i<=endP;i++){pg+='<div class="pager-btn'+(i===page?' active':'')+'" data-jump="'+i+'">'+i+'</div>'}
  if(endP<numPages)pg+='<div class="pager-btn" data-jump="'+numPages+'">'+numPages+'</div>';
  pg+='<div class="pager-btn'+(page>=numPages?' disabled':'')+'" data-jump="'+(page+1)+'">Next</div>';
  pg+='</div>';
  el.innerHTML=pg;
  el.querySelectorAll('[data-jump]').forEach(b=>{b.onclick=()=>{const n=parseInt(b.dataset.jump);if(!isNaN(n)&&n>=1&&n<=numPages)onJump(n)}});
}

document.addEventListener('change',e=>{
  if(e.target.id==='social-activity-size'){socActPage=1;loadSocialActivity()}
  else if(e.target.id==='social-profiles-size'){socProfPage=1;loadSocialProfiles()}
});
document.addEventListener('input',e=>{
  if(e.target.id==='social-profile-filter')loadSocialProfiles();
});

/* --- SOCIAL PROFILE DETAIL --- */
let profPostsPage=1,profVotesPage=1,curProfPlatform='',curProfId='';
async function loadSocialProfile(platform,profileId){
  curProfPlatform=platform;curProfId=profileId;profPostsPage=1;profVotesPage=1;
  const el=$('#socialprofile-content');
  el.innerHTML='<div class="empty">Loading profile...</div>';
  const detail=await api('social/'+encodeURIComponent(platform)+'/'+encodeURIComponent(profileId));
  if(!detail||detail.error){el.innerHTML='<div class="empty">Profile not found</div>';return}

  let h='<a class="back-btn" onclick="navigate(\'social/profiles\')">&#x2190; Back to profiles</a>';
  // Hero card
  let extLink='';
  if(platform==='twitter')extLink='<a target="_blank" rel="noopener noreferrer" href="https://x.com/'+esc(profileId)+'">View on X &#x2197;</a>';
  else if(platform==='telegram')extLink='<a target="_blank" rel="noopener noreferrer" href="https://t.me/'+esc(profileId)+'">View on Telegram &#x2197;</a>';
  h+='<div class="detail-header" style="display:flex;align-items:center;gap:16px;flex-wrap:wrap">';
  h+='<div style="width:64px;height:64px;border-radius:50%;background:var(--accent-light);display:flex;align-items:center;justify-content:center;font-size:28px;color:var(--accent);font-weight:700;flex-shrink:0">'+esc((profileId||'?').charAt(0).toUpperCase())+'</div>';
  h+='<div style="flex:1;min-width:200px"><div class="dh-title" style="margin-bottom:6px">'+platBadge(platform)+'<span style="color:var(--dim);font-size:11px">PROFILE</span></div>'+
    '<div style="font-size:20px;font-weight:700;color:var(--text);word-break:break-all">'+esc(profileId)+'</div>'+
    (extLink?'<div style="margin-top:8px;font-size:13px">'+extLink+'</div>':'')+'</div></div>';

  // Stat cards
  h+='<div class="stat-cards">';
  h+='<div class="stat-card"><div class="sc-icon">&#x1F3C6;</div><div class="sc-label">Ranking</div><div class="sc-value">'+rankCell(detail.ranking)+'</div></div>';
  h+='<div class="stat-card"><div class="sc-icon">&#x1F44D;</div><div class="sc-label">Positive Votes</div><div class="sc-value" style="color:var(--green)">'+num(detail.votesPositive)+'</div></div>';
  h+='<div class="stat-card"><div class="sc-icon">&#x1F44E;</div><div class="sc-label">Negative Votes</div><div class="sc-value" style="color:var(--red)">'+num(detail.votesNegative)+'</div></div>';
  h+='<div class="stat-card"><div class="sc-icon">&#x2696;</div><div class="sc-label">Vote Ratio</div><div class="sc-value">'+ratioPill(detail.votesPositive,detail.votesNegative)+'</div></div>';
  h+='</div>';

  h+='<div style="display:grid;grid-template-columns:1fr 1fr;gap:16px">';
  h+='<div class="card"><h2>Ranked Posts</h2><div id="socprof-posts-tbl" class="empty">Loading...</div><div id="socprof-posts-pager" class="pager"></div></div>';
  h+='<div class="card"><h2>Vote History</h2><div id="socprof-votes-tbl" class="empty">Loading...</div><div id="socprof-votes-pager" class="pager"></div></div>';
  h+='</div>';
  h+='<style>@media(max-width:900px){#socialprofile-content > div:last-child{grid-template-columns:1fr !important}}</style>';

  el.innerHTML=h;
  loadProfilePosts();
  loadProfileVotes();
}

async function loadProfilePosts(){
  const d=await api('social/'+encodeURIComponent(curProfPlatform)+'/'+encodeURIComponent(curProfId)+'/posts?page='+profPostsPage+'&pageSize=10');
  const posts=(d&&d.posts)||[];
  const numPages=d?d.numPages:1;
  if(!posts.length){$('#socprof-posts-tbl').innerHTML='<p class="empty">No ranked posts</p>';return}
  let h='<div class="table-scroll"><table class="fill"><thead><tr><th>Post</th><th class="text-right">Ranking</th><th class="text-right">Ratio</th></tr></thead><tbody>';
  posts.forEach(p=>{
    h+='<tr><td class="mono" style="font-size:11px">'+postLink(curProfPlatform,curProfId,p.id)+'</td>'+
      '<td class="text-right">'+rankCell(p.ranking)+'</td>'+
      '<td class="text-right">'+ratioPill(p.votesPositive,p.votesNegative)+'</td></tr>';
  });
  h+='</tbody></table></div>';$('#socprof-posts-tbl').innerHTML=h;
  renderPager('socprof-posts-pager',profPostsPage,numPages,n=>{profPostsPage=n;loadProfilePosts()});
}

async function loadProfileVotes(){
  const d=await api('social/'+encodeURIComponent(curProfPlatform)+'/'+encodeURIComponent(curProfId)+'/votes?page='+profVotesPage+'&pageSize=10');
  const votes=(d&&d.votes)||[];
  const numPages=d?d.numPages:1;
  if(!votes.length){$('#socprof-votes-tbl').innerHTML='<p class="empty">No votes yet</p>';return}
  let h='<div class="table-scroll"><table><thead><tr><th>TX</th><th>Time</th><th>Vote</th><th>Post</th></tr></thead><tbody>';
  votes.forEach(v=>{
    const post=v.post&&v.post.id||v.postId||'';
    h+='<tr><td class="mono" style="font-size:11px"><a onclick="navigate(\'tx/'+esc(v.txid)+'\')">'+mid(esc(v.txid),16)+'</a></td>'+
      '<td>'+fmtDate(v.timestamp||v.firstSeen)+'</td>'+
      '<td>'+sentimentHtml(v.sentiment,v.sats)+'</td>'+
      '<td class="mono" style="font-size:11px">'+postLink(curProfPlatform,curProfId,post)+'</td></tr>';
  });
  h+='</tbody></table></div>';$('#socprof-votes-tbl').innerHTML=h;
  renderPager('socprof-votes-pager',profVotesPage,numPages,n=>{profVotesPage=n;loadProfileVotes()});
}

/* --- SOCIAL POST DETAIL --- */
let postVotesPage=1,curPostPlatform='',curPostProfId='',curPostId='';
async function loadSocialPost(platform,profileId,postId){
  curPostPlatform=platform;curPostProfId=profileId;curPostId=postId;postVotesPage=1;
  const el=$('#socialpost-content');
  el.innerHTML='<div class="empty">Loading post...</div>';
  const d=await api('social/'+encodeURIComponent(platform)+'/'+encodeURIComponent(profileId)+'/post/'+encodeURIComponent(postId)+'?page=1&pageSize=25');
  if(!d||d.error){el.innerHTML='<div class="empty">Post not found</div>';return}
  const summary=d.summary||{};

  let h='<a class="back-btn" onclick="navigate(\'social/'+esc(platform)+'/'+encodeURIComponent(profileId)+'\')">&#x2190; Back to profile</a>';
  h+='<div class="detail-header"><div class="dh-title">'+platBadge(platform)+'POST</div><div class="dh-value">'+esc(postId)+'</div>';
  h+='<div style="margin-top:8px;font-size:13px">By '+profileLink(platform,profileId)+' &middot; '+externalPostLink(platform,profileId,postId)+'</div></div>';

  h+='<div class="stat-cards">';
  h+='<div class="stat-card"><div class="sc-icon">&#x1F3C6;</div><div class="sc-label">Ranking</div><div class="sc-value">'+rankCell(summary.ranking)+'</div></div>';
  h+='<div class="stat-card"><div class="sc-icon">&#x1F44D;</div><div class="sc-label">Positive</div><div class="sc-value" style="color:var(--green)">'+num(summary.votesPositive)+'</div></div>';
  h+='<div class="stat-card"><div class="sc-icon">&#x1F44E;</div><div class="sc-label">Negative</div><div class="sc-value" style="color:var(--red)">'+num(summary.votesNegative)+'</div></div>';
  h+='<div class="stat-card"><div class="sc-icon">&#x2696;</div><div class="sc-label">Vote Ratio</div><div class="sc-value">'+ratioPill(summary.votesPositive,summary.votesNegative)+'</div></div>';
  h+='<div class="stat-card"><div class="sc-icon">&#x1F4DD;</div><div class="sc-label">Total Votes</div><div class="sc-value">'+num(d.totalVotes||0)+'</div></div>';
  h+='</div>';

  h+='<div class="card"><h2>Vote History</h2><div id="socpost-votes-tbl" class="empty">Loading...</div><div id="socpost-votes-pager" class="pager"></div></div>';
  el.innerHTML=h;
  renderPostVotes(d);
}

function renderPostVotes(d){
  const votes=(d&&d.votes)||[];
  if(!votes.length){$('#socpost-votes-tbl').innerHTML='<p class="empty">No votes recorded for this post</p>';return}
  let h='<div class="table-scroll"><table class="fill"><thead><tr><th>TX</th><th>Time</th><th>Vote</th><th class="text-right">Sats</th></tr></thead><tbody>';
  votes.forEach(v=>{
    h+='<tr><td class="mono" style="font-size:11px"><a onclick="navigate(\'tx/'+esc(v.txid)+'\')">'+mid(esc(v.txid),18)+'</a></td>'+
      '<td>'+fmtDate(v.timestamp)+'</td>'+
      '<td>'+sentimentHtml(v.sentiment,v.sats)+'</td>'+
      '<td class="text-right">'+num(v.sats||0)+'</td></tr>';
  });
  h+='</tbody></table></div>';$('#socpost-votes-tbl').innerHTML=h;
  renderPager('socpost-votes-pager',postVotesPage,d.numPages||1,async n=>{
    postVotesPage=n;
    const r=await api('social/'+encodeURIComponent(curPostPlatform)+'/'+encodeURIComponent(curPostProfId)+'/post/'+encodeURIComponent(curPostId)+'?page='+n+'&pageSize=25');
    if(r)renderPostVotes(r);
  });
}

/* --- TAB LOADER --- */
function loadTab(tab){
  switch(tab){
    case 'explorer':loadExplorer();break;
    case 'network':loadNetwork();break;
    case 'top100':loadTop100();break;
    case 'stats':loadStats();break;
    case 'apidocs':loadAPIDocs();break;
    case 'social':loadSocial();break;
  }
}

function tick(){
  const active=document.querySelector('.nav-link.active');
  const tab=active?active.dataset.tab:null;
  if(!tab||tab==='stats'||tab==='apidocs')return;
  if(tab==='social'){
    const sub=document.querySelector('#social-sub-tabs .sub-tab.active');
    const subId=sub?sub.dataset.sub:'hub';
    loadSocialSub(subId==='hub'?null:subId);
    if(subId==='hub')loadSocialHub();
    return;
  }
  loadTab(tab);
}

const initRoute=parseRoute();
navigate(initRoute,false);
history.replaceState({route:initRoute},'',location.pathname);
setInterval(tick,10000);
</script>
</body>
</html>)HTML";

// ── SEO meta injection ────────────────────────────────────────────────────
//
// We render the same HTML template but inject route-specific <title>,
// <meta description>, canonical URL, OpenGraph/Twitter Card tags, and
// JSON-LD structured data. This lets crawlers index every dynamic page
// (block, tx, address, profile, post) without full SSR of the body.

static std::string SeoSiteUrl() {
    const char *env = std::getenv("LOTUS_SITE_URL");
    if (env && *env) return std::string(env);
    return std::string();
}

static std::string SeoEsc(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;
        }
    }
    return out;
}

// Split a URI path "/dashboard/social/twitter/foo" into ["dashboard","social",
// "twitter","foo"] (after stripping query string).
static std::vector<std::string> SplitUriParts(const std::string &uri) {
    std::vector<std::string> parts;
    std::string clean = uri;
    auto qpos = clean.find('?');
    if (qpos != std::string::npos) clean = clean.substr(0, qpos);
    auto fpos = clean.find('#');
    if (fpos != std::string::npos) clean = clean.substr(0, fpos);
    std::string seg;
    for (char c : clean) {
        if (c == '/') {
            if (!seg.empty()) { parts.push_back(seg); seg.clear(); }
        } else {
            seg += c;
        }
    }
    if (!seg.empty()) parts.push_back(seg);
    return parts;
}

struct SeoMeta {
    std::string title;
    std::string description;
    std::string canonical; // path only, e.g. "/dashboard/social/twitter/alice"
    std::string ogType;    // "website" or "profile" or "article"
    std::string ogImage;   // optional og:image / twitter:image URL
    // Extra @graph entries to append to the JSON-LD payload (each item is a
    // complete JSON object string starting with '{' and ending with '}').
    std::vector<std::string> extraGraph;
};

// Map social platform name to numeric ID used in rank.sqlite (must match
// the enum in modules/rank/rank_module.cpp PLATFORM_NAMES).
static int SeoPlatformId(const std::string &name) {
    if (name == "twitter") return 1;
    if (name == "nostr") return 2;
    if (name == "telegram") return 3;
    return 0;
}

// JSON string escaper for inline JSON-LD payloads. RFC 8259 minimum.
static std::string SeoJsonEsc(const std::string &s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (unsigned char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

struct RankProfileStats {
    int64_t ranking = 0;
    int votesPositive = 0;
    int votesNegative = 0;
    bool found = false;
};

// Open rank.sqlite read-only and look up profile stats. Returns
// found=false on any error or missing row. Cheap (~prepared+step).
static RankProfileStats LookupRankProfile(const std::string &platform,
                                           const std::string &profileId) {
    RankProfileStats r;
    int platId = SeoPlatformId(platform);
    if (platId == 0) return r;
    fs::path dbpath = gArgs.GetDataDirPath() / "modules" / "rank.sqlite";
    sqlite3 *db = nullptr;
    int rc = sqlite3_open_v2(fs::PathToString(dbpath).c_str(), &db,
                             SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX,
                             nullptr);
    if (rc != SQLITE_OK || !db) {
        if (db) sqlite3_close(db);
        return r;
    }
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT ranking, votes_positive, votes_negative "
                      "FROM rank_profiles "
                      "WHERE platform = ?1 AND profile_id = ?2 LIMIT 1";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, platId);
        sqlite3_bind_text(stmt, 2, profileId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            r.ranking = sqlite3_column_int64(stmt, 0);
            r.votesPositive = sqlite3_column_int(stmt, 1);
            r.votesNegative = sqlite3_column_int(stmt, 2);
            r.found = true;
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    sqlite3_close(db);
    return r;
}

// Same, but for a specific post under a profile (rank_posts table).
static RankProfileStats LookupRankPost(const std::string &platform,
                                        const std::string &profileId,
                                        const std::string &postId) {
    RankProfileStats r;
    int platId = SeoPlatformId(platform);
    if (platId == 0) return r;
    fs::path dbpath = gArgs.GetDataDirPath() / "modules" / "rank.sqlite";
    sqlite3 *db = nullptr;
    int rc = sqlite3_open_v2(fs::PathToString(dbpath).c_str(), &db,
                             SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX,
                             nullptr);
    if (rc != SQLITE_OK || !db) {
        if (db) sqlite3_close(db);
        return r;
    }
    sqlite3_stmt *stmt = nullptr;
    const char *sql = "SELECT ranking, votes_positive, votes_negative "
                      "FROM rank_posts "
                      "WHERE platform = ?1 AND profile_id = ?2 "
                      "AND post_id = ?3 LIMIT 1";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, platId);
        sqlite3_bind_text(stmt, 2, profileId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, postId.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            r.ranking = sqlite3_column_int64(stmt, 0);
            r.votesPositive = sqlite3_column_int(stmt, 1);
            r.votesNegative = sqlite3_column_int(stmt, 2);
            r.found = true;
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    sqlite3_close(db);
    return r;
}

// Build the schema.org InteractionCounter / aggregateRating fragment used
// by both Person (profile) and SocialMediaPosting (post) graphs. The
// "ranking" value is the burn-weighted sats score; we expose it as a 0-5
// rating by mapping (positive-negative)/(positive+negative) to a 1-5
// range, while still publishing the raw counts for crawlers that prefer
// InteractionCounter / LikeAction semantics (matches the marketing
// worker output 1:1).
static std::string SeoRatingFragment(const RankProfileStats &s) {
    int total = s.votesPositive + s.votesNegative;
    if (total <= 0) return std::string();
    double ratio = double(s.votesPositive) / double(total);
    // Map [0..1] ratio onto [1..5] schema.org rating range.
    double rating = 1.0 + 4.0 * ratio;
    char rbuf[32];
    std::snprintf(rbuf, sizeof(rbuf), "%.2f", rating);
    std::ostringstream f;
    f << ",\"aggregateRating\":{"
      << "\"@type\":\"AggregateRating\","
      << "\"ratingValue\":\"" << rbuf << "\","
      << "\"bestRating\":\"5\","
      << "\"worstRating\":\"1\","
      << "\"ratingCount\":" << total << ","
      << "\"reviewCount\":" << total
      << "}";
    return f.str();
}

static std::string SeoInteractionFragment(const RankProfileStats &s) {
    std::ostringstream f;
    f << ",\"interactionStatistic\":["
      << "{\"@type\":\"InteractionCounter\","
      << "\"interactionType\":\"https://schema.org/LikeAction\","
      << "\"userInteractionCount\":" << s.votesPositive << "},"
      << "{\"@type\":\"InteractionCounter\","
      << "\"interactionType\":\"https://schema.org/DislikeAction\","
      << "\"userInteractionCount\":" << s.votesNegative << "}"
      << "]";
    return f.str();
}

// Build a schema.org Person graph entry for a social profile.
// Mirrors website/marketing/scripts/build/worker/social-render.js output.
static std::string BuildPersonGraph(const std::string &platform,
                                     const std::string &profileId,
                                     const std::string &profileUrl,
                                     const std::string &avatarUrl,
                                     const RankProfileStats &s) {
    std::ostringstream g;
    g << "{\"@type\":\"Person\","
      << "\"name\":\"" << SeoJsonEsc(profileId) << "\","
      << "\"identifier\":\"" << SeoJsonEsc(platform) << ":"
      << SeoJsonEsc(profileId) << "\","
      << "\"url\":\"" << SeoJsonEsc(profileUrl) << "\"";
    if (!avatarUrl.empty()) {
        g << ",\"image\":\"" << SeoJsonEsc(avatarUrl) << "\"";
    }
    if (platform == "twitter") {
        g << ",\"sameAs\":[\"https://x.com/" << SeoJsonEsc(profileId)
          << "\"]";
    }
    if (s.found) {
        g << SeoInteractionFragment(s);
        std::string rating = SeoRatingFragment(s);
        if (!rating.empty()) g << rating;
    }
    g << "}";
    return g.str();
}

// Build a schema.org SocialMediaPosting graph entry for a post under a
// profile. Includes interactionStatistic + aggregateRating like Person.
static std::string BuildPostGraph(const std::string &platform,
                                   const std::string &profileId,
                                   const std::string &postId,
                                   const std::string &postUrl,
                                   const std::string &externalUrl,
                                   const RankProfileStats &s) {
    std::ostringstream g;
    g << "{\"@type\":\"SocialMediaPosting\","
      << "\"identifier\":\"" << SeoJsonEsc(platform) << ":"
      << SeoJsonEsc(profileId) << ":" << SeoJsonEsc(postId) << "\","
      << "\"headline\":\"Post " << SeoJsonEsc(postId) << " by "
      << SeoJsonEsc(profileId) << "\","
      << "\"url\":\"" << SeoJsonEsc(postUrl) << "\","
      << "\"author\":{\"@type\":\"Person\","
      << "\"name\":\"" << SeoJsonEsc(profileId) << "\"}";
    if (!externalUrl.empty()) {
        g << ",\"sameAs\":[\"" << SeoJsonEsc(externalUrl) << "\"]";
    }
    if (s.found) {
        g << SeoInteractionFragment(s);
        std::string rating = SeoRatingFragment(s);
        if (!rating.empty()) g << rating;
    }
    g << "}";
    return g.str();
}

// Build SeoMeta for the current request URI.
static SeoMeta BuildSeoMeta(const std::string &uri) {
    SeoMeta m;
    // Strip query string and fragment from canonical URL to avoid
    // duplicate-content penalties for ?page=2 etc.
    std::string cleanUri = uri;
    auto qpos = cleanUri.find('?');
    if (qpos != std::string::npos) cleanUri = cleanUri.substr(0, qpos);
    auto fpos = cleanUri.find('#');
    if (fpos != std::string::npos) cleanUri = cleanUri.substr(0, fpos);
    m.canonical = cleanUri;
    m.ogType = "website";

    auto parts = SplitUriParts(uri);
    // parts[0] should be "dashboard"

    if (parts.size() < 2) {
        m.title = "Lotus Explorer | Blocks, Transactions, RANK & Social";
        m.description = "Open-source blockchain explorer for the Lotus "
                        "(XPI) network. Browse blocks, transactions, "
                        "addresses, charts, on-chain RANK social activity, "
                        "and live network stats.";
        return m;
    }

    const std::string &page = parts[1];

    if (page == "explorer" || page == "blocks") {
        m.title = "Recent Blocks | Lotus Explorer";
        m.description = "Latest mined blocks on the Lotus network with "
                        "size, transaction count, difficulty, and timestamp.";
    } else if (page == "block" && parts.size() >= 3) {
        m.title = "Block " + parts[2] + " | Lotus Explorer";
        m.description = "Details for Lotus block " + parts[2] +
                        ": hash, size, difficulty, transactions, "
                        "previous block, and confirmations.";
        m.ogType = "article";
    } else if (page == "tx" && parts.size() >= 3) {
        m.title = "Transaction " + parts[2].substr(0, 16) +
                  "... | Lotus Explorer";
        m.description = "Lotus transaction details: inputs, outputs, "
                        "fee, block height, and confirmations for txid " +
                        parts[2] + ".";
        m.ogType = "article";
    } else if (page == "address" && parts.size() >= 3) {
        m.title = "Address " + parts[2] + " | Lotus Explorer";
        m.description = "Lotus address " + parts[2] + " — balance, "
                        "total received, total sent, UTXOs, and "
                        "transaction history.";
        m.ogType = "profile";
    } else if (page == "network") {
        m.title = "Network Peers | Lotus Explorer";
        m.description = "Live Lotus node peer connections, addnode "
                        "commands, protocol versions, and ping latency.";
    } else if (page == "top100") {
        m.title = "Top 100 Addresses (Rich List) | Lotus Explorer";
        m.description = "Top 100 Lotus addresses by current balance and "
                        "total received, with wealth distribution chart.";
    } else if (page == "stats") {
        m.title = "Network Statistics & Charts | Lotus Explorer";
        m.description = "Lotus network statistics: difficulty, hashrate, "
                        "mempool, total supply, burned supply, with "
                        "historical charts.";
    } else if (page == "apidocs") {
        m.title = "REST API Reference | Lotus Explorer";
        m.description = "Lotus node REST API v1 reference — every "
                        "endpoint, parameter, and response shape.";
    } else if (page == "social") {
        if (parts.size() == 2) {
            m.title = "Social — RANK Hub | Lotus Explorer";
            m.description = "On-chain RANK social activity hub: latest "
                            "votes, trending profiles, top posts, and "
                            "the full profile leaderboard.";
        } else if (parts[2] == "activity") {
            m.title = "Vote Activity | Lotus RANK";
            m.description = "Latest on-chain RANK votes across all "
                            "social platforms: txid, sentiment, "
                            "amount, profile, and post.";
        } else if (parts[2] == "trending") {
            m.title = "Trending Profiles & Posts | Lotus RANK";
            m.description = "Today's top and lowest ranked profiles "
                            "and posts across the on-chain RANK "
                            "social graph.";
        } else if (parts[2] == "profiles") {
            m.title = "All Profiles Leaderboard | Lotus RANK";
            m.description = "Full leaderboard of every social profile "
                            "ranked on-chain via Lotus RANK votes.";
        } else if (parts.size() >= 4 && parts.size() < 5) {
            // /dashboard/social/{platform}/{profileId}
            const std::string &platform = parts[2];
            const std::string &profileId = parts[3];
            m.title = profileId + " on " + platform + " | Lotus RANK";
            m.description = "Profile " + profileId + " on " + platform +
                            " — RANK score, vote ratio, recent votes, "
                            "and ranked posts.";
            m.ogType = "profile";
            // Schema.org Person enrichment with on-chain RANK scoring
            // (parity with website/marketing social-render.js worker).
            std::string base = SeoSiteUrl();
            std::string profileUrl = base + m.canonical;
            std::string avatarUrl;
            if (platform == "twitter") {
                avatarUrl = "https://unavatar.io/x/" + profileId;
                m.ogImage = avatarUrl;
            }
            RankProfileStats st = LookupRankProfile(platform, profileId);
            m.extraGraph.push_back(BuildPersonGraph(
                platform, profileId, profileUrl, avatarUrl, st));
        } else if (parts.size() >= 6 && parts[4] == "post") {
            // /dashboard/social/{platform}/{profileId}/post/{postId}
            const std::string &platform = parts[2];
            const std::string &profileId = parts[3];
            const std::string &postId = parts[5];
            m.title = "Post " + postId + " by " + profileId +
                      " | Lotus RANK";
            m.description = "On-chain RANK score and vote history for "
                            "post " + postId + " by " + profileId +
                            " on " + platform + ".";
            m.ogType = "article";
            std::string base = SeoSiteUrl();
            std::string postUrl = base + m.canonical;
            std::string externalUrl;
            if (platform == "twitter") {
                externalUrl = "https://x.com/" + profileId +
                              "/status/" + postId;
            }
            RankProfileStats st =
                LookupRankPost(platform, profileId, postId);
            // Always include the author Person graph too — gives crawlers
            // a clear (Post, author=Person) relationship and a second
            // entity to surface rich results for.
            std::string profileUrl = base + "/dashboard/social/" +
                                     platform + "/" + profileId;
            std::string avatarUrl;
            if (platform == "twitter") {
                avatarUrl = "https://unavatar.io/x/" + profileId;
                m.ogImage = avatarUrl;
            }
            RankProfileStats authorStats =
                LookupRankProfile(platform, profileId);
            m.extraGraph.push_back(BuildPersonGraph(
                platform, profileId, profileUrl, avatarUrl, authorStats));
            m.extraGraph.push_back(BuildPostGraph(
                platform, profileId, postId, postUrl, externalUrl, st));
        } else {
            m.title = "Social — RANK Hub | Lotus Explorer";
            m.description = "On-chain RANK social activity.";
        }
    } else {
        m.title = "Lotus Explorer";
        m.description = "Lotus blockchain explorer.";
    }

    return m;
}

static std::string RenderSeoHead(const SeoMeta &m) {
    std::string base = SeoSiteUrl();
    std::string canonical = base + m.canonical;

    std::ostringstream s;
    s << "<title>" << SeoEsc(m.title) << "</title>\n"
      << "<meta name=\"description\" content=\"" << SeoEsc(m.description)
      << "\">\n"
      << "<meta name=\"robots\" content=\"index, follow, "
         "max-image-preview:large, max-snippet:-1, max-video-preview:-1\">\n"
      << "<meta name=\"author\" content=\"Lotusia\">\n"
      << "<meta name=\"theme-color\" content=\"#0c0e14\">\n"
      << "<meta name=\"color-scheme\" content=\"dark\">\n"
      << "<link rel=\"canonical\" href=\"" << SeoEsc(canonical) << "\">\n"
      << "<meta property=\"og:type\" content=\"" << SeoEsc(m.ogType)
      << "\">\n"
      << "<meta property=\"og:title\" content=\"" << SeoEsc(m.title)
      << "\">\n"
      << "<meta property=\"og:description\" content=\""
      << SeoEsc(m.description) << "\">\n"
      << "<meta property=\"og:url\" content=\"" << SeoEsc(canonical)
      << "\">\n"
      << "<meta property=\"og:site_name\" content=\"Lotus Explorer\">\n"
      << "<meta property=\"og:locale\" content=\"en_US\">\n"
      << "<meta name=\"twitter:card\" content=\"summary_large_image\">\n"
      << "<meta name=\"twitter:title\" content=\"" << SeoEsc(m.title)
      << "\">\n"
      << "<meta name=\"twitter:description\" content=\""
      << SeoEsc(m.description) << "\">\n";
    if (!m.ogImage.empty()) {
        s << "<meta property=\"og:image\" content=\"" << SeoEsc(m.ogImage)
          << "\">\n"
          << "<meta name=\"twitter:image\" content=\"" << SeoEsc(m.ogImage)
          << "\">\n";
    }

    // JSON-LD: WebSite + WebPage + page-specific graph entries (Person /
    // SocialMediaPosting with interactionStatistic + aggregateRating for
    // RANK profile and post pages — parity with the marketing worker).
    s << "<script type=\"application/ld+json\">{"
      << "\"@context\":\"https://schema.org\","
      << "\"@graph\":["
      << "{\"@type\":\"WebSite\",\"@id\":\"" << SeoJsonEsc(base)
      << "/#website\",\"url\":\"" << SeoJsonEsc(base)
      << "/\",\"name\":\"Lotus Explorer\","
      << "\"potentialAction\":{\"@type\":\"SearchAction\","
      << "\"target\":{\"@type\":\"EntryPoint\",\"urlTemplate\":\""
      << SeoJsonEsc(base)
      << "/dashboard/search?q={search_term_string}\"},"
      << "\"query-input\":\"required name=search_term_string\"}},"
      << "{\"@type\":\"WebPage\",\"@id\":\"" << SeoJsonEsc(canonical)
      << "#webpage\",\"url\":\"" << SeoJsonEsc(canonical) << "\","
      << "\"name\":\"" << SeoJsonEsc(m.title) << "\","
      << "\"description\":\"" << SeoJsonEsc(m.description) << "\","
      << "\"isPartOf\":{\"@id\":\"" << SeoJsonEsc(base) << "/#website\"}}";
    for (const std::string &entry : m.extraGraph) {
        s << "," << entry;
    }
    s << "]}</script>\n";

    return s.str();
}

static std::string ReplaceFirst(std::string str, const std::string &needle,
                                 const std::string &replacement) {
    auto pos = str.find(needle);
    if (pos == std::string::npos) return str;
    return str.replace(pos, needle.size(), replacement);
}

bool HandleGetDashboard(const util::Ref &, HTTPRequest *req,
                        const std::vector<std::string> &,
                        const QueryParams &) {
    std::string uri = req->GetURI();
    SeoMeta meta = BuildSeoMeta(uri);
    std::string head = RenderSeoHead(meta);
    std::string html = ReplaceFirst(DASHBOARD_HTML, "<!--SEO_HEAD-->", head);

    req->WriteHeader("Content-Type", "text/html; charset=utf-8");
    req->WriteHeader("Cache-Control", "public, max-age=60, "
                                       "stale-while-revalidate=600");
    req->WriteHeader("X-Robots-Tag",
                     "index, follow, max-image-preview:large");
    req->WriteReply(HTTP_OK, html);
    return true;
}

// ── Favicon ───────────────────────────────────────────────────────────────
//
// 5-petal lotus blossom (matches the 🌸 logo emoji used in the topnav),
// drawn in the dashboard accent blue on the dark theme background with a
// golden center disc. Served as SVG so it scales crisply at every size.
// /favicon.ico requests are answered with the same SVG payload — every
// modern browser and crawler accepts an SVG body for the .ico URL when
// the Content-Type header says so, and it lets us avoid shipping a
// separate binary asset.
static const char *FAVICON_SVG =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 64 64\">"
    "<rect width=\"64\" height=\"64\" rx=\"12\" fill=\"#0c0e14\"/>"
    "<g transform=\"translate(32 32)\" fill=\"#60a5fa\">"
    "<ellipse cx=\"0\" cy=\"-14\" rx=\"7\" ry=\"14\"/>"
    "<ellipse cx=\"0\" cy=\"-14\" rx=\"7\" ry=\"14\" transform=\"rotate(72)\"/>"
    "<ellipse cx=\"0\" cy=\"-14\" rx=\"7\" ry=\"14\" transform=\"rotate(144)\"/>"
    "<ellipse cx=\"0\" cy=\"-14\" rx=\"7\" ry=\"14\" transform=\"rotate(216)\"/>"
    "<ellipse cx=\"0\" cy=\"-14\" rx=\"7\" ry=\"14\" transform=\"rotate(288)\"/>"
    "</g>"
    "<circle cx=\"32\" cy=\"32\" r=\"6\" fill=\"#fbbf24\"/>"
    "</svg>";

bool HandleFavicon(HTTPRequest *req) {
    req->WriteHeader("Content-Type", "image/svg+xml");
    req->WriteHeader("Cache-Control", "public, max-age=86400, immutable");
    req->WriteReply(HTTP_OK, FAVICON_SVG);
    return true;
}

} // namespace api
