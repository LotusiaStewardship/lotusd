// Copyright (c) 2023-2026 Lotusia / Alexandre Guillioud, Matthew Urgero
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <api/dashboard_handler.h>
#include <rpc/protocol.h>

namespace api {

static const char *DASHBOARD_HTML = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lotus Explorer</title>
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
.stat-cards{grid-template-columns:repeat(auto-fit,minmax(130px,1fr))}
.search-bar{padding:8px 12px}
.io-grid{grid-template-columns:1fr}
.io-grid .io-arrow{transform:rotate(90deg)}
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
<input id="search-input" type="text" placeholder="Search block height/hash, txid, or address">
<button class="search-btn" id="search-btn">SEARCH</button>
</div>
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
<div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px">
<div class="card"><h2>Recent Votes</h2><div id="social-activity" class="empty">Loading...</div></div>
<div class="card"><h2>Top Profiles</h2><div id="social-profiles" class="empty">Loading...</div></div>
</div>
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
  $$('.nav-link').forEach(x=>{
    x.classList.toggle('active',x.dataset.tab===id);
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
  showPanel('explorer');
  loadTab('explorer');
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

/* --- SEARCH --- */
function doSearch(){
  const q=$('#search-input').value.trim();
  if(!q)return;
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
    {id:'chart-mempool',title:'Mempool Transactions',periods:['day','week','month'],defaultP:'day',field:'mempool_count',type:'bar',color:'#ef5350'},
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
  const d=await api('stats/charts?period='+period);if(!d||!d.series||!d.series.length)return;
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

/* --- SOCIAL TAB --- */
async function loadSocial(){
  const [activity,profiles]=await Promise.all([api('social/activity'),api('social/profiles')]);
  if(activity&&activity.votes&&activity.votes.length){
    let h='<table><thead><tr><th>Profile</th><th>Post ID</th><th>Sentiment</th><th>Sats Burned</th></tr></thead><tbody>';
    activity.votes.slice(0,15).forEach(v=>{
      const s=v.sentiment;
      const sentHtml=s==='positive'||s>0?'<span style="color:var(--green)">&#x2191; positive</span>':s==='negative'||s<0?'<span style="color:var(--red)">&#x2193; negative</span>':'<span style="color:var(--dim)">neutral</span>';
      h+='<tr><td>'+esc(v.profileId||v.profile_id||'—')+'</td><td class="mono" style="font-size:11px">'+mid(esc(v.postId||v.post_id||'—'),18)+'</td><td>'+sentHtml+'</td><td class="text-right">'+num(v.sats||v.burned_sats||0)+'</td></tr>';
    });
    h+='</tbody></table>';$('#social-activity').innerHTML=h;
  } else { $('#social-activity').innerHTML='<p class="empty">No RANK votes recorded yet</p>'; }
  if(profiles&&profiles.profiles&&profiles.profiles.length){
    let h='<table><thead><tr><th>Platform</th><th>Profile</th><th>Ranking</th><th>+Votes</th><th>-Votes</th></tr></thead><tbody>';
    profiles.profiles.slice(0,15).forEach(p=>{
      h+='<tr><td>'+esc(p.platform||'—')+'</td><td>'+esc(p.id||p.profile_id||'—')+'</td><td class="text-right">'+num(p.ranking||0)+'</td><td class="text-right" style="color:var(--green)">'+num(p.votesPositive||0)+'</td><td class="text-right" style="color:var(--red)">'+num(p.votesNegative||0)+'</td></tr>';
    });
    h+='</tbody></table>';$('#social-profiles').innerHTML=h;
  } else { $('#social-profiles').innerHTML='<p class="empty">No profiles yet</p>'; }
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
  if(tab&&tab!=='stats'&&tab!=='apidocs')loadTab(tab);
}

const initRoute=parseRoute();
navigate(initRoute,false);
history.replaceState({route:initRoute},'',location.pathname);
setInterval(tick,10000);
</script>
</body>
</html>)HTML";

bool HandleGetDashboard(const util::Ref &, HTTPRequest *req,
                        const std::vector<std::string> &,
                        const QueryParams &) {
    req->WriteHeader("Content-Type", "text/html; charset=utf-8");
    req->WriteHeader("Cache-Control", "no-store");
    req->WriteReply(HTTP_OK, DASHBOARD_HTML);
    return true;
}

} // namespace api
