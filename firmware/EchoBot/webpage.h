// Echo's phone control page (served by the ESP32 itself, works with no internet)
#pragma once

const char PAGE_HTML[] PROGMEM = R"rawliteral(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Echo Bot</title>
<style>
:root{--bg:#0d1220;--card:#161e33;--line:#26314f;--txt:#e6ecfa;--dim:#8a9ac0;--acc:#4fc3ff;--ok:#5dff9a;--warn:#ffb74d;--bad:#ff5c6c}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--txt);font-family:system-ui,Segoe UI,Roboto,sans-serif}
header{padding:18px 16px 8px;display:flex;align-items:center;gap:12px}
.face{font-size:42px}h1{margin:0;font-size:22px}.sub{color:var(--dim);font-size:13px}
.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;padding:0 16px}
.stat{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:10px;text-align:center}
.stat b{display:block;font-size:18px}.stat span{color:var(--dim);font-size:12px}
.card{background:var(--card);border:1px solid var(--line);border-radius:16px;margin:12px 16px;padding:14px}
.card h2{margin:0 0 10px;font-size:15px;color:var(--acc)}
.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}.grid3{grid-template-columns:repeat(3,1fr)}
button{background:#22304f;color:var(--txt);border:1px solid var(--line);border-radius:12px;padding:11px 6px;font-size:14px;cursor:pointer}
button:active{transform:scale(.97)}button.pri{background:var(--acc);color:#04203a;font-weight:700;border:none}
button.on{outline:2px solid var(--acc)}
input,select{width:100%;background:#0f1628;color:var(--txt);border:1px solid var(--line);border-radius:10px;padding:10px;font-size:15px}
.row{display:flex;gap:8px;margin-top:8px}.row>*{flex:1}
.big{font-size:36px;font-weight:800;text-align:center;margin:4px 0}
.list div{display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid var(--line);font-size:14px}
.pill{display:inline-block;padding:2px 8px;border-radius:999px;font-size:12px;background:#22304f}
.alarm{background:var(--bad)!important;color:#fff}
small{color:var(--dim)}
</style></head><body>
<header><div class="face" id="fx">🙂</div><div><h1>Echo</h1><div class="sub" id="sub">connecting…</div></div></header>
<div class="stats">
 <div class="stat"><b id="bat">--</b><span>battery</span></div>
 <div class="stat"><b id="dst">--</b><span>distance</span></div>
 <div class="stat"><b id="clk">--</b><span>time</span></div>
</div>

<div class="card"><h2>💬 Say something on Echo's screen</h2>
 <input id="msg" maxlength="120" placeholder="Hi Reno, go eat 😤">
 <div class="row"><button class="pri" onclick="say()">Send</button><button onclick="api('beep')">🔔 Find my bot</button></div>
</div>

<div class="card"><h2>😊 Mood</h2><div class="grid" id="moods"></div></div>

<div class="card"><h2>🧭 Mode</h2><div class="grid grid3" id="modes"></div></div>

<div class="card"><h2>⏱️ Study timer</h2>
 <div class="big" id="ptime">25:00</div><div style="text-align:center"><span class="pill" id="pst">idle</span></div>
 <div class="row"><input id="pf" type="number" value="25" min="1" max="120"><input id="pb" type="number" value="5" min="1" max="60"></div>
 <small>focus minutes · break minutes</small>
 <div class="row"><button class="pri" onclick="pomo('start')">Start</button><button onclick="pomo('pause')">Pause</button><button onclick="pomo('reset')">Reset</button></div>
</div>

<div class="card"><h2>💧 Reminders</h2>
 <div class="row"><div><small>Water (min, 0=off)</small><input id="rw" type="number" min="0" max="240"></div>
 <div><small>Stretch (min, 0=off)</small><input id="rs" type="number" min="0" max="240"></div></div>
 <div class="row"><select id="rm"><option value="1">Meal reminders ON</option><option value="0">Meal reminders OFF</option></select></div>
 <div class="row"><button class="pri" onclick="remind()">Save</button></div>
</div>

<div class="card"><h2>🚨 Guard mode</h2>
 <div style="text-align:center"><span class="pill" id="gst">off</span> <small id="gcount"></small></div>
 <div class="row"><button class="pri" onclick="api('guard?act=arm')">Arm</button><button onclick="api('guard?act=disarm')">Disarm</button></div>
</div>

<div class="card"><h2>📡 Wi-Fi around Echo</h2>
 <button onclick="scan()" style="width:100%">Scan</button><div class="list" id="scan"></div>
</div>

<div class="card"><h2>⚙️ Link to hotspot (for laptop brain later)</h2>
 <input id="ss" placeholder="Hotspot name"><div class="row"><input id="sp" type="password" placeholder="Password"></div>
 <div class="row"><button onclick="wifi()">Save &amp; connect</button><button onclick="api('sleep')">😴 Sleep</button><button onclick="api('wake')">☀️ Wake</button></div>
 <small id="sta"></small>
</div>
<div style="height:24px"></div>
<script>
const M={neutral:'🙂',happy:'😄',sad:'😢',angry:'😠',sleepy:'😴',surprised:'😮',love:'😍',curious:'🤔'};
const MODES=[['face','👀 Face'],['pomo','⏱️ Timer'],['dice','🎲 Decide'],['dist','📏 Distance'],['piano','🎵 Air piano'],['guard','🚨 Guard'],['scan','📡 Wi-Fi'],['flappy','🐤 Flappy'],['batt','🔋 Battery']];
let st={};
function api(p){return fetch('/api/'+p).then(r=>r.json()).then(j=>{st=j;draw();return j}).catch(()=>{})}
function say(){const t=document.getElementById('msg');if(!t.value)return;api('say?text='+encodeURIComponent(t.value));t.value=''}
function pomo(a){api('pomo?act='+a+'&focus='+pf.value+'&brk='+pb.value)}
function remind(){api('remind?water='+rw.value+'&stretch='+rs.value+'&meals='+rm.value)}
function wifi(){api('wifi?ssid='+encodeURIComponent(ss.value)+'&pass='+encodeURIComponent(sp.value))}
function scan(){document.getElementById('scan').innerHTML='<small>scanning…</small>';
 fetch('/api/scan').then(r=>r.json()).then(l=>{document.getElementById('scan').innerHTML=l.map(n=>
 `<div><span>${n.lock?'🔒':'🔓'} ${n.ssid||'(hidden)'}</span><span>${n.rssi} dBm · ch${n.ch}</span></div>`).join('')||'<small>none found</small>'})}
const mm=s=>String(Math.floor(s/60)).padStart(2,'0')+':'+String(s%60).padStart(2,'0');
let filled=false;
function draw(){
 fx.textContent=M[st.mood]||'🙂';
 sub.textContent=(st.sleeping?'sleeping · ':'')+'mode: '+st.mode+(st.present?' · sees you':'');
 bat.textContent=st.pct+'%';dst.textContent=st.dist>0?st.dist+' cm':'--';
 clk.textContent=st.time||'--';
 ptime.textContent=mm(st.pleft||0);pst.textContent=st.pstate;
 gst.textContent=st.galarm?'ALARM!':(st.garmed?'armed':'off');gst.className='pill'+(st.galarm?' alarm':'');
 gcount.textContent=st.gcount?('triggered '+st.gcount+'×'):'';
 sta.textContent=st.staip?('Linked: '+st.stassid+' · '+st.staip):(st.stassid?('Trying: '+st.stassid):'Not linked');
 document.querySelectorAll('#modes button').forEach(b=>b.classList.toggle('on',b.dataset.m==st.mode));
 document.querySelectorAll('#moods button').forEach(b=>b.classList.toggle('on',b.dataset.m==st.mood));
 if(!filled){rw.value=st.water;rs.value=st.stretch;rm.value=st.meals?1:0;filled=true}
}
moods.innerHTML=Object.keys(M).map(k=>`<button data-m="${k}" onclick="api('mood?m=${k}')">${M[k]}</button>`).join('');
modes.innerHTML=MODES.map(m=>`<button data-m="${m[0]}" onclick="api('mode?m=${m[0]}')">${m[1]}</button>`).join('');
const d=new Date();api('time?epoch='+Math.floor(d.getTime()/1000)+'&tz='+(-d.getTimezoneOffset()));
setInterval(()=>api('status'),2000);
</script></body></html>)rawliteral";
