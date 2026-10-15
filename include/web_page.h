#pragma once
#include <pgmspace.h>

// Dashboard (liegt im Flash). Holt alle 3 s /api/stats.
static const char WEB_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Miner</title>
<script>try{if((localStorage.getItem('cmTheme')||'dark')==='light')document.documentElement.classList.add('light')}catch(e){}</script>
<style>
:root{--bg:#161821;--card:#222530;--line:#333644;--txt:#e8eaf0;--dim:#9aa0b0;
--accent:#3d8bfd;--accent2:#2f6fd6;--green:#3fb950;--red:#e5534b;--orange:#f0883e;
--modal:#232733;--field:#191c26;--ibd:#3a3f50;--shadow:rgba(0,0,0,.5)}
:root.light{--bg:#eef1f6;--card:#ffffff;--line:#e2e5ee;--txt:#1a1d26;--dim:#5c6472;
--modal:#ffffff;--field:#f3f5fa;--ibd:#cdd3e0;--shadow:rgba(30,40,80,.15)}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--txt);
font:15px/1.45 system-ui,Segoe UI,Roboto,sans-serif;padding:18px;transition:background .2s,color .2s}
.top{max-width:1200px;margin:0 auto 20px;display:flex;align-items:center;justify-content:center;position:relative}
h1{font-weight:600;text-align:center;margin:6px 0;font-size:26px;letter-spacing:.02em}
.gear{position:absolute;right:0;top:50%;transform:translateY(-50%);background:var(--card);color:var(--txt);
border:1px solid var(--line);border-radius:9px;padding:8px 13px;font-size:14px;cursor:pointer;transition:.15s}
.gear:hover{border-color:var(--accent);color:var(--accent)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,1fr));gap:16px;max-width:1200px;margin:0 auto}
.card{background:var(--card);border-radius:12px;padding:18px;text-align:center;border-top:4px solid var(--accent);
box-shadow:0 1px 3px var(--shadow)}
.lbl{color:var(--dim);letter-spacing:.09em;font-size:11.5px;text-transform:uppercase}
.val{font-size:30px;font-weight:700;margin:7px 0 3px;font-variant-numeric:tabular-nums}
.sub{color:var(--dim);font-size:12px}
.wide{max-width:1200px;margin:16px auto 0;background:var(--card);border-radius:12px;padding:16px;box-shadow:0 1px 3px var(--shadow)}
canvas{width:100%;height:160px;display:block}
table{width:100%;border-collapse:collapse}
td{padding:8px 4px;border-bottom:1px solid var(--line)}
tr:last-child td{border-bottom:0}
td:first-child{color:var(--dim)}
td:last-child{text-align:right;font-variant-numeric:tabular-nums;word-break:break-all}
.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:7px;background:var(--red)}
.dot.on{background:var(--green)}
.swbtn{background:var(--card);color:var(--accent);border:1px solid var(--line);border-radius:7px;
padding:3px 9px;font-size:12px;font-weight:600;cursor:pointer;margin-left:8px;transition:.12s}
.swbtn:hover{border-color:var(--accent)}.swbtn:disabled{opacity:.6;cursor:default}
/* --- Einstellungsfenster --- */
.ov{position:fixed;inset:0;background:rgba(10,12,20,.55);backdrop-filter:blur(2px);display:none;
align-items:flex-start;justify-content:center;overflow:auto;padding:26px 16px;z-index:9}
.ov.open{display:flex}
.modal{background:var(--modal);border-radius:16px;width:100%;max-width:520px;padding:0;overflow:hidden;
box-shadow:0 20px 60px var(--shadow);animation:pop .18s ease-out}
@keyframes pop{from{transform:translateY(10px);opacity:0}to{transform:none;opacity:1}}
.mhead{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:18px 22px;border-bottom:1px solid var(--line)}
.mhead h2{margin:0;font-weight:600;font-size:19px}
.mbody{padding:18px 22px;max-height:70vh;overflow:auto}
.seg{display:inline-flex;background:var(--field);border:1px solid var(--ibd);border-radius:9px;padding:3px}
.seg button{border:0;background:none;color:var(--dim);border-radius:7px;padding:6px 12px;font-size:13px;cursor:pointer;font-weight:600}
.seg button.act{background:var(--accent);color:#fff}
.intro{color:var(--dim);font-size:13.5px;margin:0 0 14px;line-height:1.5}
.sec{border:1px solid var(--line);border-radius:12px;padding:6px 14px 14px;margin:0 0 16px}
.sec-h{color:var(--accent);font-weight:700;font-size:13px;letter-spacing:.04em;text-transform:uppercase;
padding:12px 2px 4px}
.fld{margin:10px 0}
.fld label{display:block;color:var(--dim);font-size:12.5px;margin:0 0 5px;font-weight:500}
.fld input{width:100%;background:var(--field);color:var(--txt);border:1px solid var(--ibd);border-radius:8px;
padding:10px 12px;font-size:14px;transition:.12s}
.fld input:focus{outline:none;border-color:var(--accent);box-shadow:0 0 0 3px rgba(61,139,253,.18)}
.scanrow{display:flex;gap:8px}.scanrow input{flex:1}
.scanbtn{background:var(--field);color:var(--accent);border:1px solid var(--ibd);border-radius:8px;
padding:0 14px;cursor:pointer;font-size:13px;font-weight:600;white-space:nowrap;transition:.12s}
.scanbtn:hover{border-color:var(--accent)}.scanbtn:disabled{opacity:.6;cursor:default}
.ssidsel{width:100%;margin-top:8px;background:var(--field);color:var(--txt);border:1px solid var(--ibd);
border-radius:8px;padding:9px 10px;font-size:14px}
.mfoot{display:flex;gap:10px;justify-content:flex-end;align-items:center;padding:16px 22px;border-top:1px solid var(--line)}
.btn{border:0;border-radius:9px;padding:10px 18px;font-size:14px;font-weight:600;cursor:pointer;transition:.12s}
.btn.save{background:var(--accent);color:#fff}.btn.save:hover{background:var(--accent2)}
.btn.save:disabled{opacity:.6;cursor:default}
.btn.cancel{background:var(--field);color:var(--txt);border:1px solid var(--ibd)}
.msg{flex:1;font-size:13.5px;min-height:1.2em}.msg.err{color:var(--red)}.msg.ok{color:var(--green)}
</style></head><body>
<div class="top">
 <h1 id="mn">ESP32 Miner</h1>
 <button class="gear" onclick="openCfg()">&#9881; Settings</button>
</div>
<div class="grid">
 <div class="card"><div class="lbl">Hashrate</div><div class="val" id="hr">-</div><div class="sub">kH/s &middot; &Oslash; <span id="avg">-</span></div></div>
 <div class="card"><div class="lbl">Shares (R/A)</div><div class="val" id="sh">-</div><div class="sub" id="shsub">-</div><div class="sub" id="shsub2" style="margin-top:3px;opacity:.85"></div></div>
 <div class="card"><div class="lbl">Best difficulty</div><div class="val" id="best">-</div><div class="sub">since start</div></div>
 <div class="card"><div class="lbl">Uptime</div><div class="val" id="up">-</div><div class="sub" id="tot">-</div></div>
</div>
<div class="wide"><div class="lbl">Hashrate history (10 min)</div><canvas id="cv"></canvas></div>
<div class="wide"><table>
 <tr><td>Pool (<span id="pname">-</span>)</td><td><span class="dot" id="pd"></span><span id="pool">-</span> <button id="swbtn" class="swbtn" style="display:none" onclick="switchPool()">&#8646; Switch</button></td></tr>
 <tr><td>Worker</td><td id="user">-</td></tr>
 <tr><td>Pool difficulty</td><td id="pdiff">-</td></tr>
 <tr><td>Network difficulty</td><td id="ndiff">-</td></tr>
 <tr><td>Blocks found</td><td id="blk">-</td></tr>
 <tr><td>Wi-Fi</td><td id="wifi">-</td></tr>
 <tr><td>Chip temperature (rough)</td><td id="ctemp">-</td></tr>
 <tr><td>Free memory</td><td id="heap">-</td></tr>
 <tr><td>Firmware</td><td id="ver">-</td></tr>
 <tr><td>Project</td><td><a href="https://github.com/brenner23/ESPressMiner32" target="_blank" rel="noopener" style="color:inherit">GitHub</a></td></tr>
</table></div>

<div class="ov" id="ov" onclick="if(event.target===this)closeCfg()"><div class="modal">
 <div class="mhead">
  <h2 id="ctitle">Settings</h2>
  <div class="seg"><button type="button" id="thDark" onclick="setTheme('dark')">Dark</button><button type="button" id="thLight" onclick="setTheme('light')">Light</button></div>
 </div>
 <div class="mbody">
  <p class="intro" id="cintro" style="display:none">Welcome! Enter a name, your Wi-Fi and the pool. The miner then restarts and connects to your Wi-Fi.</p>
  <div class="sec">
   <div class="sec-h">General</div>
   <div class="fld"><label for="c_name">Display name</label><input id="c_name" maxlength="32" placeholder="ESP32 Miner"></div>
   <div class="fld"><label for="c_host">Network name (hostname)</label><input id="c_host" maxlength="32"></div>
   <div class="fld"><label for="c_ssid">Wi-Fi name (SSID)</label>
    <div class="scanrow"><input id="c_ssid" maxlength="32"><button type="button" class="scanbtn" id="scanbtn" onclick="scanWifi()">&#128246; Scan</button></div>
    <select id="ssidsel" class="ssidsel" style="display:none" onchange="if(this.value)$('c_ssid').value=this.value"></select>
   </div>
   <div class="fld"><label for="c_wpass">Wi-Fi password</label><input id="c_wpass" type="password" maxlength="64"></div>
  </div>
  <div class="sec">
   <div class="sec-h">Primary Pool</div>
   <div class="fld"><label for="p0url">Pool address</label><input id="p0url" placeholder="stratum+tcp://host:port"></div>
   <div class="fld"><label for="p0user">Wallet address</label><input id="p0user"></div>
   <div class="fld"><label for="p0pass">Pool password</label><input id="p0pass"></div>
  </div>
  <div class="sec">
   <div class="sec-h">Secondary Pool (optional)</div>
   <div class="fld"><label for="p1url">Pool address</label><input id="p1url" placeholder="empty = no secondary pool"></div>
   <div class="fld"><label for="p1user">Wallet address</label><input id="p1user"></div>
   <div class="fld"><label for="p1pass">Pool password</label><input id="p1pass"></div>
  </div>
  <div class="sec">
   <div class="sec-h">Pool switching</div>
   <div class="fld"><label for="c_altmin">Alternate automatically every N minutes &nbsp;(0 = off, switch on failure only)</label><input id="c_altmin" type="number" min="0" max="1440" value="0"></div>
  </div>
 </div>
 <div class="mfoot">
  <span class="msg" id="cmsg"></span>
  <button class="btn cancel" id="ccancel" onclick="closeCfg()">Cancel</button>
  <button class="btn save" id="csave" onclick="saveCfg()">Save &amp; restart</button>
 </div>
</div></div>

<script>
const $=id=>document.getElementById(id);
let portal=false, lastHist=[];

function setTheme(t){
 document.documentElement.classList.toggle('light',t==='light');
 try{localStorage.setItem('cmTheme',t)}catch(e){}
 $('thDark').className=t==='light'?'':'act';
 $('thLight').className=t==='light'?'act':'';
 if(lastHist.length)draw(lastHist);
}
(function(){let t='dark';try{t=localStorage.getItem('cmTheme')||'dark'}catch(e){}setTheme(t)})();

async function openCfg(){const m=$('cmsg');m.textContent='';m.className='msg';
 $('ccancel').style.display=portal?'none':'';$('cintro').style.display=portal?'':'none';
 $('ctitle').textContent=portal?'Set up miner':'Settings';
 try{const c=await(await fetch('/api/config')).json();
 $('c_name').value=c.minerName||'';$('c_host').value=c.hostname||'';$('c_ssid').value=c.wifiSsid||'';$('c_wpass').value='';
 $('c_wpass').placeholder=c.wifiPassSet?'•••••• (unchanged)':'';
 for(let i=0;i<2;i++){const p=c.pools[i]||{};$('p'+i+'url').value=p.url||'';$('p'+i+'user').value=p.user||'';$('p'+i+'pass').value=p.pass||'x'}
 $('c_altmin').value=c.poolAltMin||0;
 $('ov').classList.add('open')}catch(e){alert('Could not load settings')}}
function closeCfg(){if(!portal)$('ov').classList.remove('open')}
async function saveCfg(){const m=$('cmsg'),b=$('csave');
 const body={minerName:$('c_name').value,hostname:$('c_host').value,wifiSsid:$('c_ssid').value,wifiPass:$('c_wpass').value,
 poolAltMin:parseInt($('c_altmin').value)||0,
 pools:[0,1].map(i=>({url:$('p'+i+'url').value,user:$('p'+i+'user').value,pass:$('p'+i+'pass').value}))};
 b.disabled=true;m.className='msg';m.textContent='Saving ...';
 try{const r=await fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});
 const t=await r.text();if(!r.ok){m.className='msg err';m.textContent=t;b.disabled=false;return}
 m.className='msg ok';
 if(portal){m.textContent='Saved! Restarting and connecting to "'+body.wifiSsid+'". Then open: http://'+body.hostname+'.local/';return}
 m.textContent='Saved. Miner is restarting (about 15 s) ...';waitBack()}
 catch(e){m.className='msg err';m.textContent='Saving failed';b.disabled=false}}
function waitBack(){setTimeout(async function p(){try{await fetch('/api/stats',{cache:'no-store'});location.reload()}catch(e){setTimeout(p,2000)}},8000)}
async function switchPool(){const b=$('swbtn');b.disabled=true;const old=b.innerHTML;b.textContent='switching ...';
 try{const r=await fetch('/api/switchpool',{method:'POST'});await r.text()}catch(e){}
 setTimeout(()=>{b.disabled=false;b.innerHTML=old},4000)}
async function scanWifi(){const b=$('scanbtn'),sel=$('ssidsel');b.disabled=true;const old=b.innerHTML;b.textContent='Scanning ...';
 try{const j=await(await fetch('/api/scan')).json();const ns=j.nets||[];
 ns.sort((a,c)=>c.rssi-a.rssi);
 sel.innerHTML='<option value="">'+(ns.length?('-- '+ns.length+' networks found --'):'-- no networks found --')+'</option>';
 ns.forEach(n=>{const o=document.createElement('option');o.value=n.ssid;
  const q=n.rssi>-60?'++++':n.rssi>-70?'+++':n.rssi>-80?'++':'+';
  o.textContent=n.ssid+'   ['+q+']'+(n.lock?' 🔒':'');sel.appendChild(o)});
 sel.style.display=''}catch(e){alert('Wi-Fi scan failed')}
 b.disabled=false;b.innerHTML=old}

function fd(d){if(d<1)return d.toFixed(6);const u=['','K','M','G','T','P'];let i=0;while(d>=1000&&i<5){d/=1000;i++}return d.toFixed(2)+u[i]}
function fu(s){const d=Math.floor(s/86400);s%=86400;const p=n=>String(n).padStart(2,'0');
 return(d?d+'d ':'')+p(Math.floor(s/3600))+':'+p(Math.floor(s/60)%60)+':'+p(s%60)}
function fh(h){const u=['','K','M','G','T'];let i=0;while(h>=1000&&i<4){h/=1000;i++}return h.toFixed(2)+' '+u[i]+'Hashes'}
function draw(h){lastHist=h;const c=$('cv'),r=devicePixelRatio||1,w=c.clientWidth,H=c.clientHeight;
 c.width=w*r;c.height=H*r;const x=c.getContext('2d');x.scale(r,r);x.clearRect(0,0,w,H);
 if(h.length<2)return;const mx=Math.max(...h)*1.15||1;
 const cs=getComputedStyle(document.documentElement);
 const grid=cs.getPropertyValue('--line').trim(),lab=cs.getPropertyValue('--dim').trim(),acc=cs.getPropertyValue('--accent').trim();
 x.strokeStyle=grid;x.fillStyle=lab;x.font='11px sans-serif';x.lineWidth=1;
 for(let g=0;g<=4;g++){const y=H-18-(H-26)*g/4;x.beginPath();x.moveTo(40,y);x.lineTo(w,y);x.stroke();x.fillText(Math.round(mx*g/4),2,y+4)}
 x.beginPath();h.forEach((v,i)=>{const px=40+(w-40)*i/(h.length-1),py=H-18-(H-26)*v/mx;i?x.lineTo(px,py):x.moveTo(px,py)});
 x.strokeStyle=acc;x.lineWidth=2;x.stroke();x.lineTo(w,H-18);x.lineTo(40,H-18);x.closePath();
 x.fillStyle=acc+'22';x.fill()}
async function tick(){try{const s=await(await fetch('/api/stats')).json();
 if(s.minerName){$('mn').textContent=s.minerName;document.title=s.minerName}
 $('hr').textContent=s.hashrate.toFixed(1);$('avg').textContent=s.average.toFixed(1);
 const tot=s.accepted+s.rejected;$('sh').textContent=s.rejected+'/'+s.accepted+'/'+(tot?s.accepted*100/tot:100).toFixed(1)+'%';
 $('shsub').textContent=s.submitted+' sent / '+s.accepted+' accepted · '+s.rejected+' rejected';
 {const we=s.waveErrors||0, le=(s.hwErrors||0)-we;
  $('shsub2').textContent=le+' discarded (calculation errors)'+(we?' (+'+we+' from '+(s.guardBursts||0)+' Wi-Fi wave'+((s.guardBursts||0)==1?'':'s')+')':'');}
 $('best').textContent=fd(s.bestDiff);$('up').textContent=fu(s.uptime);$('tot').textContent=fh(s.totalHashes);
 $('pool').textContent=s.pool+(s.authorized?'':s.connected?' (connecting...)':' (disconnected)');
 $('pd').className='dot'+(s.authorized?' on':'');$('pname').textContent=s.poolName+(s.poolAltMin?' ⇆ every '+s.poolAltMin+' min':'');$('user').textContent=s.user;
 $('swbtn').style.display=s.hasSecondary?'':'none';
 $('pdiff').textContent=fd(s.poolDiff);$('ndiff').textContent=fd(s.netDiff);$('blk').textContent=s.blocks;
 $('wifi').textContent=s.ip+' · '+s.rssi+' dBm';
 $('ctemp').textContent=(s.chipTemp?s.chipTemp.toFixed(1)+' °C':'-')+(s.thermalLevel?' · '+s.thermal:'');$('heap').textContent=(s.heap/1024).toFixed(1)+' KB';
 $('ver').textContent=s.version;draw(s.history);if(s.portal&&!portal){portal=true;openCfg()}}catch(e){$('pd').className='dot'}}
tick();setInterval(tick,3000);addEventListener('resize',()=>{if(lastHist.length)draw(lastHist)});
</script></body></html>)rawliteral";
