// web.cpp - web page for mapping, settings, backup/restore and firmware update.
// Everything except the firmware upload goes through the same command
// interpreter as the serial console. All pages need the admin password
// (user name "admin").
#include <WebServer.h>
#include <Update.h>
#include "app.h"
#include "led_driver.h"

static WebServer s_server(80);

// Print sink that collects output into a String
class StringOut : public Print {
 public:
  String s;
  size_t write(uint8_t c) override { s += (char)c; return 1; }
  size_t write(const uint8_t* b, size_t n) override { for (size_t i = 0; i < n; i++) s += (char)b[i]; return n; }
};

static const char kPage[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Steamboat lights</title>
<style>
body{font-family:system-ui,sans-serif;margin:0;background:#111;color:#ddd}
header{background:#1d2a3a;padding:10px 14px;display:flex;flex-wrap:wrap;gap:12px;align-items:center}
header h1{font-size:18px;margin:0;flex:1}
main{padding:10px 14px;max-width:1100px}
section{background:#1a1a1a;border:1px solid #333;border-radius:8px;padding:10px 12px;margin:10px 0}
h2{font-size:15px;margin:0 0 8px;color:#9cf}
button{background:#2b4b6b;color:#fff;border:0;border-radius:5px;padding:6px 10px;margin:2px;cursor:pointer;font-size:13px}
button:hover{background:#3a6590} button.warn{background:#7a3030} button.ok{background:#2f6b3a}
input,select{background:#222;color:#eee;border:1px solid #444;border-radius:4px;padding:4px;font-size:13px}
input[type=number]{width:62px} input[type=color]{width:42px;height:26px;padding:0}
table{border-collapse:collapse;width:100%;font-size:13px} td,th{padding:3px 5px;border-bottom:1px solid #2a2a2a;text-align:left}
tr.edited td{background:#3a3315} .end{color:#999}
#cursor{font-family:monospace;color:#ff9;margin:6px 0}
pre{background:#000;color:#9f9;padding:8px;height:200px;overflow:auto;font-size:12px;margin:6px 0 0}
.row{display:flex;flex-wrap:wrap;gap:6px;align-items:center;margin:4px 0}
.slider{display:flex;align-items:center;gap:6px;margin:3px 0} .slider label{width:130px} .slider input[type=range]{flex:1;max-width:300px}
.dirty{color:#fc6;font-weight:bold} .muted{color:#888;font-size:12px}
.grid{display:grid;grid-template-columns:150px 260px;gap:4px 8px;align-items:center}
progress{width:300px;height:16px}
</style></head><body>
<header><h1>Steamboat lights <span id="ver" class="muted"></span></h1><span id="net" class="muted"></span><span id="dirty"></span>
<button class="ok" onclick="cmd('save')">Save</button><button onclick="cmd('on')">On</button><button class="warn" onclick="cmd('off')">Off</button></header>
<main>
<section><h2>Find LEDs</h2>
<div class="row">Channel <select id="ch"></select> LED <input id="led" type="number" min="0" value="0">
<button onclick="light()">Light</button>
<button onclick="cmd('-10')">-10</button><button onclick="cmd('-')">-1</button><button onclick="cmd('+')">+1</button><button onclick="cmd('+10')">+10</button>
<span class="muted">(arrow keys work too)</span></div>
<div class="row"><button onclick="cmd('walk '+ch())">Walk</button><button onclick="cmd('ruler '+ch())">Ruler</button>
<button onclick="cmd('fill '+ch())">Fill channel</button><button onclick="cmd('rgb '+ch())">RGB order test</button>
<button onclick="cmd('ends')">All section ends</button><button onclick="cmd('blank')">Blank</button>
<label><input type="checkbox" id="bg" onchange="cmd('bg '+(this.checked?'on':'off'))"> dim boat behind tests</label>
<button class="ok" onclick="cmd('run')">Run show</button></div>
<div id="cursor"></div>
<div class="muted">Ruler: LED 0 green, every 5th blue, every 10th red, every 100th magenta.</div>
</section>
<section><h2>Sections</h2>
<div class="muted">Runs are <b>ch:first-last</b>, separated by commas, in animation order, e.g. <code>1:90-120, 1:200-170, 2:5</code>.
first&gt;last means the run is wired backwards; the water flows along the list in order. LED numbers start at 0.<br>
Mark with the cursor: <b>Start</b> begins the section over at the cursor, <b>Add run</b> starts another run at the cursor, <b>End</b> ends the last run at the cursor.
<b>Show</b> runs a white dot through the section in animation order. Edit the text then Apply. Changes are live; Save keeps them.</div>
<table><thead><tr><th>#</th><th>Section</th><th>Anim</th><th>Runs</th><th>LEDs</th><th>Color</th><th>Rev</th><th></th></tr></thead><tbody id="secs"></tbody></table>
<pre id="problems" style="height:auto;max-height:160px;color:#fc6"></pre>
</section>
<section><h2>Channels</h2><table><thead><tr><th>Ch</th><th>GPIO</th><th>LEDs</th><th>LED type</th><th>Color order</th><th></th></tr></thead><tbody id="chans"></tbody></table></section>
<section><h2>Show</h2>
<div class="slider"><label>Brightness</label><input type="range" id="bright" min="0" max="255" onchange="cmd('bright '+this.value)"><span id="brightv"></span></div>
<div class="slider"><label>Water speed %</label><input type="range" id="waterSpeed" min="0" max="400" onchange="cmd('speed water '+this.value)"><span id="waterSpeedv"></span></div>
<div class="slider"><label>Smoke speed %</label><input type="range" id="smokeSpeed" min="0" max="400" onchange="cmd('speed smoke '+this.value)"><span id="smokeSpeedv"></span></div>
<div class="slider"><label>Wheel speed %</label><input type="range" id="wheelSpeed" min="0" max="400" onchange="cmd('speed wheel '+this.value)"><span id="wheelSpeedv"></span></div>
<div class="slider"><label>Unlit spoke level</label><input type="range" id="floor" min="0" max="255" onchange="cmd('floor '+this.value)"><span id="floorv"></span></div>
<div class="slider"><label>Smoke thinning</label><input type="range" id="fade" min="0" max="255" onchange="cmd('fade '+this.value)"><span id="fadev"></span></div>
<div class="row"><button onclick="cmd('wheeldir')">Reverse wheel direction</button><span id="wheelRev" class="muted"></span></div>
<div class="row"><button onclick="cmd('reload')">Discard unsaved changes</button><button class="warn" onclick="if(confirm('Load compiled-in show and section defaults? (Network settings are kept. Not saved until you press Save.)'))cmd('defaults')">Defaults</button>
<button class="warn" onclick="if(confirm('Reboot?'))cmd('reboot')">Reboot</button></div>
</section>
<section id="sensorBox"><h2>Daylight auto-off</h2>
<div class="row"><label><input type="checkbox" id="autoDark" onchange="cmd('sensor '+(this.checked?'on':'off'))"> Turn the display off in daylight</label></div>
<div id="lightNow" style="font-family:monospace;color:#ff9;margin:4px 0"></div>
<div class="slider"><label>On below (% light)</label><input type="range" id="darkLevel" min="0" max="100" onchange="cmd('sensor dark '+this.value)"><span id="darkLevelv"></span></div>
<div class="slider"><label>Off above (% light)</label><input type="range" id="dayLevel" min="0" max="100" onchange="cmd('sensor day '+this.value)"><span id="dayLevelv"></span></div>
<div class="row">Must stay past a threshold for <input type="number" id="sensorDelay" min="0" max="3600" onchange="cmd('sensor delay '+this.value)"> seconds</div>
<div class="muted">Watch the reading at dusk and set "on below" a little under it and "off above" a bit higher. The gap and the delay stop it flickering on and off.</div>
</section>
<section><h2>Network</h2>
<div class="grid">
<label>Home Wi-Fi name</label><input id="staSsid" maxlength="32" placeholder="(none: access point only)">
<label>Home Wi-Fi password</label><input id="staPass" type="password" maxlength="63" placeholder="(unchanged)">
<label>Setup AP name</label><input id="apSsid" maxlength="32">
<label>Setup AP password</label><input id="apPass" type="password" maxlength="63" placeholder="(unchanged) WPA2, 8-63 chars">
<label>Admin password</label><input id="adminPass" type="password" maxlength="63" placeholder="(unchanged)">
</div>
<div class="row"><button class="ok" onclick="saveNet()">Save network settings</button><button class="warn" onclick="if(confirm('Reboot now?'))cmd('reboot')">Reboot to apply</button></div>
<div class="muted">Wi-Fi and AP changes apply after a reboot. The admin password (user name <b>admin</b>) protects this page and OTA uploads. If the home network can't be joined, the setup AP starts after 20 s; holding the Function button 5 s also starts it.</div>
</section>
<section><h2>Backup &amp; restore</h2>
<div class="row"><button onclick="location.href='/backup'+($('sec').checked?'?secrets=1':'')">Download backup</button>
<label><input type="checkbox" id="sec"> include Wi-Fi / AP / admin passwords</label></div>
<div class="row"><input type="file" id="rfile" accept=".txt,text/plain"><label><input type="checkbox" id="rsave" checked> save after restoring</label>
<button class="warn" onclick="restore()">Restore</button></div>
<div class="muted">The backup is a text file of console commands; you can read and edit it. Sections are matched by name, so a backup still restores after a firmware update that adds or reorders sections.</div>
<pre id="rlog" style="height:auto;max-height:200px;display:none"></pre>
</section>
<section><h2>Firmware update</h2>
<div class="row"><input type="file" id="ffile" accept=".bin"><button class="warn" onclick="upload()">Upload firmware</button><progress id="fprog" max="100" value="0" style="display:none"></progress><span id="fstat"></span></div>
<div class="muted">Use <code>.pio/build/usb/firmware.bin</code> (PlatformIO) or Sketch &gt; Export Compiled Binary (Arduino IDE). Settings are kept. Download a backup first if the new firmware changes the section list.</div>
</section>
<section><h2>Console</h2><div class="row"><input id="cl" style="flex:1" placeholder="command (help for list, up/down for history)" onkeydown="clKey(event)"><button onclick="cmd('help')">Help</button></div><pre id="log"></pre></section>
</main>
<script>
let S=null;
const $=id=>document.getElementById(id);
const ch=()=>$('ch').value;
function hex(c){return '#'+c.toLowerCase()}
function esc(s){return String(s).replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/</g,'&lt;')}
// Console input history: up/down like a shell. Kept in this browser (last 50);
// password commands are never stored.
let H=[];try{H=JSON.parse(localStorage.getItem('hist')||'[]')}catch(e){}
let hi=H.length,draft='';
function clKey(e){
  const el=e.target;
  if(e.key=='Enter'){
    const c=el.value; if(!c.trim())return;
    if(!/^\s*(wifi|ap|admin)\s+pass/i.test(c)&&H[H.length-1]!==c){H.push(c);if(H.length>50)H.shift();try{localStorage.setItem('hist',JSON.stringify(H))}catch(e){}}
    hi=H.length;draft='';el.value='';cmd(c);
  }else if(e.key=='ArrowUp'||e.key=='ArrowDown'){
    e.preventDefault();
    if(e.key=='ArrowUp'){if(hi==0)return;if(hi==H.length)draft=el.value;hi--;}
    else{if(hi>=H.length)return;hi++;}
    el.value=hi<H.length?H[hi]:draft;
    el.setSelectionRange(el.value.length,el.value.length);
  }
}
async function cmd(c,keepTable){
  const r=await fetch('/api/cmd',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'c='+encodeURIComponent(c)});
  const t=await r.text(); const log=$('log');
  log.textContent+='> '+c.split('\n').map(l=>/^(wifi|ap|admin) pass/.test(l)?l.replace(/pass .*/,'pass ****'):l).join(' ; ')+'\n'+t; log.scrollTop=log.scrollHeight;
  const lines=t.trim().split('\n'); const cur=lines.filter(l=>/^ch\d+ LED/.test(l)).pop(); if(cur)$('cursor').textContent=cur;
  if(/^(set|add|clear|ms|ma|me|color|rev|defaults|reload|chan)\b/.test(c)||c.includes('\n'))keepTable=false; else if(keepTable===undefined)keepTable=true;
  await load(!keepTable);
  return t;
}
function light(){cmd('p '+ch()+' '+$('led').value)}
async function load(rebuild,lite){
  S=await (await fetch('/api/state')).json();
  $('ver').textContent='v'+S.ver;
  $('net').textContent=S.net; $('dirty').innerHTML=S.dirty?'<span class="dirty">unsaved changes</span>':'';
  $('sensorBox').style.display=S.sensor?'':'none';
  $('lightNow').textContent=`Light now ${S.light}% (raw ${S.raw})  sensor says ${S.day?'DAY':'NIGHT'}  display ${S.lit?'LIT':(S.on?'dark (daylight)':'OFF')}`;
  if(lite)return;
  $('autoDark').checked=!!S.autoDark; if(document.activeElement!=$('sensorDelay'))$('sensorDelay').value=S.sensorDelay;
  for(const k of ['bright','waterSpeed','smokeSpeed','wheelSpeed','floor','fade','darkLevel','dayLevel']){$(k).value=S[k];$(k+'v').textContent=S[k];}
  $('wheelRev').textContent=S.wheelRev?'reversed':'normal'; $('bg').checked=!!S.bg;
  for(const k of ['staSsid','apSsid'])if(document.activeElement!=$(k))$(k).value=S[k];
  if($('ch').options.length!=S.channels.length){$('ch').innerHTML=S.channels.map((c,i)=>`<option value="${i+1}">${i+1} (GPIO${c.pin})</option>`).join('');}
  if(document.activeElement!=$('ch'))$('ch').value=S.test.ch; if(document.activeElement!=$('led'))$('led').value=S.test.led;
  $('chans').innerHTML=S.channels.map((c,i)=>`<tr><td>${i+1}</td><td>${c.pin}</td><td><input type=number id="cc${i}" value="${c.count}" min=0 max=${S.maxLeds}></td>
   <td><select id="ct${i}" onchange="$('co${i}').value=S.types[this.selectedIndex].order">${S.types.map(t=>`<option${t.name==c.type?' selected':''}>${t.name}</option>`).join('')}</select></td>
   <td><select id="co${i}">${['RGB','RBG','GRB','GBR','BRG','BGR'].map(o=>`<option${o==c.order?' selected':''}>${o}</option>`).join('')}</select></td>
   <td><button onclick="cmd('chan ${i+1} count '+$('cc${i}').value+'\\nchan ${i+1} type '+$('ct${i}').value+'\\nchan ${i+1} order '+$('co${i}').value)">Apply</button></td></tr>`).join('');
  $('problems').textContent=S.problems;
  if(rebuild||!$('secs').children.length)buildTable();
}
function buildTable(){
  $('secs').innerHTML=S.sections.map((s,i)=>{const n=i+1;
   return `<tr id="r${n}"><td>${n}</td><td>${s.name}</td><td class=muted>${s.role}${s.grp?' '+s.grp:''}</td>
   <td><input id="l${n}" value="${s.leds}" placeholder="ch:first-last, ..." oninput="ed(${n})" style="width:230px"></td>
   <td class=end>${s.count}</td>
   <td><input type=color id="k${n}" value="${hex(s.color)}" oninput="ed(${n})"></td>
   <td><input type=checkbox id="v${n}" ${s.rev?'checked':''} onchange="ed(${n})" title="play in the opposite order"></td>
   <td><button onclick="apply(${n})">Apply</button><button onclick="cmd('s ${n}')">Show</button>
   <button onclick="cmd('ms ${n}')">Start</button><button onclick="cmd('ma ${n}')">Add run</button><button onclick="cmd('me ${n}')">End</button></td></tr>`}).join('');
}
function ed(n){$('r'+n).className='edited'}
function apply(n){const s=S.sections[n-1];const l=$('l'+n).value.trim();
  let c=(l?`set ${n} ${l}`:`clear ${n}`)+`\ncolor ${n} ${$('k'+n).value.slice(1)}`;
  c+=`\nrev ${n} ${$('v'+n).checked?'on':'off'}`; cmd(c,false)}
async function saveNet(){
  const L=[]; const ss=$('staSsid').value.trim();
  L.push(ss?'wifi ssid '+ss:'wifi off');
  if($('staPass').value)L.push('wifi pass '+$('staPass').value);
  if($('apSsid').value.trim())L.push('ap ssid '+$('apSsid').value.trim());
  if($('apPass').value){if($('apPass').value.length<8||$('apPass').value.length>63){alert('AP password must be 8-63 characters (WPA2)');return}L.push('ap pass '+$('apPass').value)}
  if($('adminPass').value){if($('adminPass').value.length<4){alert('Admin password must be at least 4 characters');return}L.push('admin pass '+$('adminPass').value)}
  L.push('save');
  const t=await cmd(L.join('\n'));
  for(const k of ['staPass','apPass','adminPass'])$(k).value='';
  if(/Saved/.test(t))alert('Network settings saved. Reboot to apply Wi-Fi/AP changes.'); else alert('Not all settings were accepted:\n'+t);
}
async function restore(){
  const f=$('rfile').files[0]; if(!f){alert('Choose a backup file first');return}
  if(!confirm('Restore settings from '+f.name+'?'))return;
  const text=await f.text();
  const r=await fetch('/api/restore',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},
    body:'text='+encodeURIComponent(text)+'&save='+($('rsave').checked?1:0)});
  const log=$('rlog'); log.style.display=''; log.textContent=await r.text();
  await load(true);
}
function upload(){
  const f=$('ffile').files[0]; if(!f){alert('Choose a .bin file first');return}
  if(!confirm('Upload '+f.name+' and restart the controller?'))return;
  const fd=new FormData(); fd.append('firmware',f,f.name);
  const x=new XMLHttpRequest(); const p=$('fprog'); p.style.display=''; p.value=0; $('fstat').textContent='Uploading...';
  x.upload.onprogress=e=>{if(e.lengthComputable)p.value=Math.round(e.loaded*100/e.total)};
  x.onload=()=>{ $('fstat').textContent=x.responseText; if(x.status==200){ $('fstat').textContent+=' Reconnecting...'; setTimeout(()=>location.reload(),15000);} };
  x.onerror=()=>{$('fstat').textContent='Upload failed (connection lost).'};
  x.open('POST','/update'); x.send(fd);
}
document.addEventListener('keydown',e=>{if(['INPUT','SELECT'].includes(document.activeElement.tagName))return;
  const m={ArrowRight:'+',ArrowLeft:'-',ArrowUp:'+10',ArrowDown:'-10'}[e.key]; if(m){e.preventDefault();cmd(m)}});
load(true);
setInterval(()=>load(false,true),3000);
</script></body></html>)HTML";

static bool auth() {
  if (s_server.authenticate("admin", g_net.adminPass)) return true;
  s_server.requestAuthentication(BASIC_AUTH, "Steamboat lights");
  return false;
}

static String jsonEscape(const String& in) {
  String o;
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if (c == '\r') {}
    else if ((uint8_t)c < 32) o += ' ';
    else o += c;
  }
  return o;
}

static void handleState() {
  if (!auth()) return;
  String j;
  j.reserve(4096 + NUM_SECTIONS * 140);
  char buf[200];
  j += "{\"ver\":\"" FW_VERSION "\",\"net\":\"" + jsonEscape(netInfo()) + "\"";
  j += ",\"staSsid\":\"" + jsonEscape(String(g_net.staSsid)) + "\",\"apSsid\":\"" + jsonEscape(String(g_net.apSsid)) + "\"";
  snprintf(buf, sizeof(buf), ",\"on\":%d,\"dirty\":%d,\"bright\":%u,\"waterSpeed\":%u,\"smokeSpeed\":%u,\"wheelSpeed\":%u,"
           "\"floor\":%u,\"fade\":%u,\"wheelRev\":%u,\"bg\":%u,\"maxLeds\":%d",
           g_on ? 1 : 0, g_dirty ? 1 : 0, g_set.brightness, g_set.waterSpeed, g_set.smokeSpeed, g_set.wheelSpeed,
           g_set.spokeFloor, g_set.smokeFade, g_set.wheelReverse, g_set.testBackground, MAX_LEDS_PER_CHANNEL);
  j += buf;
  snprintf(buf, sizeof(buf), ",\"sensor\":%d,\"light\":%d,\"raw\":%d,\"day\":%d,\"lit\":%d,\"autoDark\":%u,"
           "\"darkLevel\":%u,\"dayLevel\":%u,\"sensorDelay\":%u",
           sensorPresent() ? 1 : 0, lightLevel(), lightRaw(), isDaylight() ? 1 : 0, displayActive() ? 1 : 0,
           g_set.autoDark, g_set.darkLevel, g_set.dayLevel, g_set.sensorDelay);
  j += buf;
  snprintf(buf, sizeof(buf), ",\"test\":{\"mode\":%d,\"ch\":%d,\"led\":%u,\"sec\":%d}",
           g_test.mode, g_test.ch + 1, g_test.a, g_test.section + 1);
  j += buf;
  j += ",\"channels\":[";
  for (int c = 0; c < NUM_CHANNELS; c++) {
    snprintf(buf, sizeof(buf), "%s{\"pin\":%d,\"count\":%u,\"order\":\"%s\",\"type\":\"%s\"}", c ? "," : "",
             LED_PINS[c], g_chan[c].count, orderName(g_chan[c].order), ledTypeName(g_chan[c].type));
    j += buf;
  }
  j += "],\"types\":[";
  for (int t = 0; t < LED_TYPE_COUNT; t++) {
    snprintf(buf, sizeof(buf), "%s{\"name\":\"%s\",\"order\":\"%s\"}", t ? "," : "", ledTypeName(t), orderName(ledTypeOrder(t)));
    j += buf;
  }
  j += "],\"sections\":[";
  for (int i = 0; i < NUM_SECTIONS; i++) {
    const SectionCfg& s = g_sect[i];
    int grp = (sectionRole(i) == ROLE_SMOKE || sectionRole(i) == ROLE_SPOKE) ? sectionGroup(i) + 1 : 0;
    snprintf(buf, sizeof(buf), "%s{\"name\":\"%s\",\"role\":\"%s\",\"grp\":%d,\"count\":%u,\"color\":\"%06lX\",\"rev\":%d,\"leds\":\"",
             i ? "," : "", sectionName(i), roleName(sectionRole(i)), grp, sectionCount(i),
             (unsigned long)s.color, s.reverse ? 1 : 0);
    j += buf;
    j += formatRuns(i);
    j += "\"}";
  }
  StringOut problems;
  validate(problems, true);
  j += "],\"problems\":\"" + jsonEscape(problems.s) + "\"}";
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(200, "application/json", j);
}

static void handleCmd() {
  if (!auth()) return;
  String all = s_server.arg("c");
  StringOut out;
  int start = 0;
  while (start <= (int)all.length()) {   // several commands may be separated by newlines
    int nl = all.indexOf('\n', start);
    if (nl < 0) nl = all.length();
    String line = all.substring(start, nl);
    line.trim();
    if (line.length()) {
      String lower = line; lower.toLowerCase();
      if (lower == "reboot" || lower == "restart") {
        s_server.send(200, "text/plain", out.s + "Rebooting...\n");
        delay(300);
        ESP.restart();
      }
      runCommand(line, out);
    }
    start = nl + 1;
  }
  out.s.replace("\r", "");
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(200, "text/plain", out.s);
}

static void handleBackup() {
  if (!auth()) return;
  StringOut out;
  writeBackup(out, s_server.hasArg("secrets") && s_server.arg("secrets") != "0");
  out.s.replace("\r", "");
  s_server.sendHeader("Content-Disposition", "attachment; filename=\"steamboat-backup.txt\"");
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(200, "text/plain", out.s);
}

// Run each line of a backup file; report failures; optionally save.
static void handleRestore() {
  if (!auth()) return;
  String text = s_server.arg("text");
  bool doSave = s_server.arg("save") == "1";
  StringOut report;
  int lines = 0, failed = 0;
  int start = 0;
  while (start <= (int)text.length()) {
    int nl = text.indexOf('\n', start);
    if (nl < 0) nl = text.length();
    String line = text.substring(start, nl);
    start = nl + 1;
    line.trim();
    if (line.length() == 0 || line[0] == '#') continue;
    String lower = line; lower.toLowerCase();
    if (lower == "reboot" || lower == "restart" || lower == "save") continue;
    lines++;
    StringOut out;
    if (!runCommand(line, out)) {
      failed++;
      out.s.replace("\r", "");
      report.printf("FAILED: %s\n  %s", line.c_str(), out.s.c_str());
    }
  }
  report.printf("%d command(s) applied, %d failed.\n", lines - failed, failed);
  if (doSave) {
    if (saveConfig()) report.print("Saved to flash.\n");
    else report.print("SAVE FAILED.\n");
  } else {
    report.print("Not saved yet: press Save to keep these settings.\n");
  }
  report.print("Wi-Fi / AP changes (if any) apply after a reboot.\n");
  s_server.sendHeader("Cache-Control", "no-store");
  s_server.send(200, "text/plain", report.s);
}

// ---------------------------------------------------------------- firmware upload

static bool   s_otaAuthed = false;
static String s_otaError;

static void otaBlankLeds() {
  uint8_t orders[NUM_CHANNELS];
  for (int c = 0; c < NUM_CHANNELS; c++) { orders[c] = g_chan[c].order; memset(Leds::buffer(c), 0, g_chan[c].count * 3); }
  Leds::show(0, orders);
}

static void handleUpdateChunk() {
  HTTPUpload& up = s_server.upload();
  if (up.status == UPLOAD_FILE_START) {
    s_otaError = "";
    s_otaAuthed = s_server.authenticate("admin", g_net.adminPass);
    if (!s_otaAuthed) return;
    otaBlankLeds();
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) s_otaError = String("Could not start update: ") + Update.errorString();
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!s_otaAuthed || s_otaError.length()) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize)
      s_otaError = String("Write failed (is this an ESP32 firmware .bin?): ") + Update.errorString();
  } else if (up.status == UPLOAD_FILE_END) {
    if (!s_otaAuthed || s_otaError.length()) { if (s_otaAuthed) Update.abort(); return; }
    if (!Update.end(true)) s_otaError = String("Update failed: ") + Update.errorString();
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    if (s_otaAuthed) Update.abort();
    s_otaError = "Upload aborted.";
  }
}

static void handleUpdateDone() {
  if (!s_otaAuthed) { s_server.requestAuthentication(BASIC_AUTH, "Steamboat lights"); return; }
  if (s_otaError.length()) {
    s_server.send(500, "text/plain", s_otaError);
    return;
  }
  s_server.send(200, "text/plain", "Firmware updated. Restarting.");
  delay(500);
  ESP.restart();
}

void webBegin() {
  s_server.on("/", HTTP_GET, []() { if (auth()) s_server.send_P(200, "text/html", kPage); });
  s_server.on("/api/state", HTTP_GET, handleState);
  s_server.on("/api/cmd", HTTP_POST, handleCmd);
  s_server.on("/api/cmd", HTTP_GET, handleCmd);   // handy from a browser bar: /api/cmd?c=ends
  s_server.on("/backup", HTTP_GET, handleBackup);
  s_server.on("/api/restore", HTTP_POST, handleRestore);
  s_server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateChunk);
  s_server.onNotFound([]() { s_server.send(404, "text/plain", "Not found"); });
  s_server.begin();
}

void webLoop() { s_server.handleClient(); }
