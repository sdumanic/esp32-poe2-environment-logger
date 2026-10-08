#ifndef WEB_ASSETS_H
#define WEB_ASSETS_H

// The embedded web assets live in this header so that the Arduino sketch
// preprocessor (which generates the forward declarations) only has to look at
// the code in the .ino file. Keep this file next to the sketch.

static const char PAGE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>ESP32-POE2 &middot; PMS5003</title>
<style>
:root{
  --bg:#f5f7fa; --card:#ffffff; --text:#111827; --muted:#6b7280; --line:#e5e7eb;
  --line-soft:#f1f2f4; --hover:#fafbfc;
  --accent:#2563eb; --accent-dark:#1d4ed8; --accent-soft:#eef4ff;
  --ok:#15803d; --warn:#b45309; --err:#b91c1c;
  --pm1:#16a34a; --pm25:#ea580c; --pm10:#dc2626; --temp:#7c3aed; --hum:#0891b2; --dew:#0284c7;
  --r:14px; --sh:0 1px 2px rgba(16,24,40,.05),0 10px 28px rgba(16,24,40,.06);
}
body.theme-dark{
  --bg:#0f1319; --card:#171d26; --text:#e7ebf2; --muted:#94a3b8; --line:#252d3a;
  --line-soft:#1f2733; --hover:#1d2430;
  --accent:#4f8cff; --accent-dark:#3a74e6; --accent-soft:#1b2534;
  --pm1:#22c55e; --pm25:#f97316; --pm10:#ef4444; --temp:#a78bfa; --hum:#22d3ee; --dew:#38bdf8;
  --sh:0 1px 2px rgba(0,0,0,.5),0 12px 30px rgba(0,0,0,.45);
}
body.theme-contrast{
  --bg:#000000; --card:#000000; --text:#ffffff; --muted:#ffd400; --line:#ffffff;
  --line-soft:#3a3a3a; --hover:#141414;
  --accent:#00e5ff; --accent-dark:#00b8cc; --accent-soft:#00323a;
  --pm1:#00ff66; --pm25:#ffcc00; --pm10:#ff3b30; --temp:#c084fc; --hum:#22d3ee; --dew:#38bdf8;
  --sh:none;
}
body.theme-contrast .card,body.theme-contrast .val{border-width:2px}
*{box-sizing:border-box}
body{margin:0;padding:18px;background:var(--bg);color:var(--text);
  font:14px/1.55 system-ui,-apple-system,"Segoe UI",Roboto,Arial,sans-serif;-webkit-font-smoothing:antialiased}
h1{display:flex;align-items:center;flex-wrap:wrap;gap:8px 14px;font-size:20px;font-weight:650;margin:0}
.card{background:var(--card);border:1px solid var(--line);border-radius:var(--r);box-shadow:var(--sh);padding:16px;margin-bottom:14px}
.row{display:flex;flex-wrap:wrap;gap:8px 24px;align-items:baseline}
.kv{font-size:13px;color:var(--muted)}
.kv b{color:var(--text);font-weight:600}
.tag{display:inline-block;padding:2px 10px;border-radius:999px;background:var(--accent-soft);color:var(--accent-dark);font-size:12px;font-weight:600}
.ctrl{display:flex;flex-wrap:wrap;gap:10px 14px;align-items:center;margin:4px 0 10px}
select,input[type=datetime-local]{font:inherit;font-size:13px;padding:7px 10px;border:1px solid var(--line);border-radius:10px;background:var(--card);color:var(--text)}
select:focus,input:focus{outline:2px solid var(--accent-soft);border-color:var(--accent)}
button{font:inherit;font-size:13px;font-weight:600;padding:8px 14px;border:1px solid var(--line);border-radius:10px;background:var(--card);color:var(--text);cursor:pointer;transition:.15s}
button:hover{background:var(--hover);border-color:var(--accent)}
button:active{transform:translateY(1px)}
#apply{background:var(--accent);border-color:var(--accent);color:#fff}
button.on{background:var(--accent);border-color:var(--accent);color:#fff}
#apply:hover{background:var(--accent-dark);border-color:var(--accent-dark)}
a{color:var(--accent);text-decoration:none;font-weight:500}
a:hover{text-decoration:underline}
.del{color:var(--err)}
.help{font-size:13px;font-weight:500;color:var(--muted)}
.help:hover{color:var(--accent)}
.muted{color:var(--muted);font-size:12px}
.stale{opacity:.45}
.banner{display:none;gap:10px;align-items:baseline;padding:11px 14px;margin-bottom:14px;border-radius:var(--r);
  background:#fee2e2;border:1px solid #fca5a5;color:#7f1d1d;font-size:13.5px;font-weight:650}
.banner span{font-weight:400}
.banner.warn{background:#fef3c7;border-color:#fcd34d;color:#78350f}
body.theme-dark .banner.warn{background:#3a2d10;border-color:#78350f;color:#fde68a}
body.theme-contrast .banner.warn{background:#000;border-color:#ffcc00;color:#ffcc00}
body.theme-dark .banner{background:#3f1d1d;border-color:#7f1d1d;color:#fecaca}
body.theme-contrast .banner{background:#000;border-color:#ff3b30;color:#ff3b30;border-width:2px}
canvas{width:100%;height:320px;display:block;border:1px solid var(--line);border-radius:12px;background:var(--card)}
#chartEnv{height:220px}
.vals{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:12px;margin-top:8px}
.val{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:10px 12px;border-top:3px solid var(--line)}
.val .muted{font-size:11.5px;text-transform:uppercase;letter-spacing:.05em}
.big{font-size:30px;font-weight:700;line-height:1.15;font-variant-numeric:tabular-nums}
#cvals .val:nth-child(1){border-top-color:var(--pm1)}
#cvals .val:nth-child(2){border-top-color:var(--pm25)}
#cvals .val:nth-child(3){border-top-color:var(--pm10)}
#cvals .val:nth-child(4){border-top-color:var(--temp)}
#cvals .val:nth-child(5){border-top-color:var(--hum)}
#cvals .val:nth-child(6){border-top-color:var(--dew)}
table{border-collapse:separate;border-spacing:0;width:100%;font-size:13.5px}
th{text-align:left;font-size:11.5px;text-transform:uppercase;letter-spacing:.05em;color:var(--muted);font-weight:600;padding:8px 10px;border-bottom:1px solid var(--line)}
td{padding:9px 10px;border-bottom:1px solid var(--line-soft);vertical-align:middle}
tbody tr:hover td{background:var(--hover)}
tbody tr:last-child td{border-bottom:none}
.modal{display:none;position:fixed;inset:0;background:rgba(15,23,42,.5);z-index:10;padding:18px;overflow:auto;-webkit-backdrop-filter:blur(2px);backdrop-filter:blur(2px)}
.modal.open{display:block}
.modalbox{background:var(--card);border:1px solid var(--line);border-radius:16px;max-width:820px;margin:0 auto;overflow:hidden;box-shadow:0 24px 60px rgba(0,0,0,.35)}
.modalhead{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:14px 18px;border-bottom:1px solid var(--line);background:var(--card)}
.modalbody{padding:8px 18px 20px;font-size:13.5px;line-height:1.55}
.modalbody h3{font-size:14px;margin:18px 0 6px}
.modalbody table{margin-bottom:10px;font-size:13px}
.modalbody td,.modalbody th{padding:5px 8px}
.modalbody ul{margin:6px 0 10px 18px;padding:0}
.modalbody li{margin-bottom:4px}
@media(max-width:640px){body{padding:10px}.card{padding:13px;border-radius:12px}h1{font-size:17px}.big{font-size:24px}canvas{height:250px}.ctrl{gap:8px}}
</style>
</head>
<body>

<div class="card">
  <h1>ESP32-POE2 &middot; PMS5003
    <a href="#" id="help" class="help">[ help / reference values ]</a>
    <a href="/admin" class="help">[ administration ]</a>
  </h1>
  <div class="row">
    <div class="kv">Board time: <b id="clock">-</b> <span class="tag" id="tsrc">-</span></div>
    <div class="kv">Started: <b id="boot">-</b> <span class="muted" id="reset"></span></div>
    <div class="kv">File: <b id="file">-</b></div>
    <div class="kv">Records: <b id="recs">0</b></div>
    <div class="kv">OTA: <b id="ota">-</b></div>
  </div>
  <div class="row" style="margin-top:6px">
    <div class="kv">Wired network: <b id="ethinfo">-</b></div>
    <div class="kv">Wireless (AP): <b id="apinfo">-</b></div>
  </div>
</div>

<div class="banner" id="sdwarn"><b id="sdwarnhead"></b><span id="sdwarnbody"></span></div>
<div class="banner warn" id="pwwarn"><b>Factory credentials:</b><span id="pwwarnbody"></span></div>

<div class="card">
  <div class="kv" style="margin-bottom:8px">Graph of PM1.0 / PM2.5 / PM10 changes (refreshes on every new record)</div>
  <div class="ctrl">
    <label class="kv">Range:
      <select id="range">
        <option value="0">all</option>
        <option value="300" selected>last 5 min</option>
        <option value="1800">last 30 min</option>
        <option value="3600">last 1 h</option>
        <option value="21600">last 6 h</option>
        <option value="86400">last 24 h</option>
      </select>
    </label>
    <label class="kv">From: <input type="datetime-local" id="from" step="1"></label>
    <label class="kv">To: <input type="datetime-local" id="till" step="1"></label>
    <button id="apply">Apply range</button>
    <button id="live">Live</button>
    <button id="csv">Download CSV</button>
    <button id="png">Download PNG</button>
  </div>
  <div class="muted" id="rangeinfo" style="margin-bottom:6px">-</div>
  <canvas id="chart"></canvas>
  <div class="muted" id="chartinfo" style="margin-bottom:10px">-</div>
  <canvas id="chartEnv"></canvas>
  <div class="muted" id="chartinfoEnv">-</div>
</div>

<div class="card">
  <div class="muted">Current values (updated every 5 s)</div>
  <div class="kv" id="cage" style="margin:6px 0">sensor: -</div>
  <div class="kv" id="dhtstate" style="margin:0 0 6px">AM2302: -</div>
  <div class="vals" id="cvals">
    <div class="val"><div class="muted">PM1.0</div><div class="big" id="c1">-</div></div>
    <div class="val"><div class="muted">PM2.5</div><div class="big" id="c25">-</div></div>
    <div class="val"><div class="muted">PM10</div><div class="big" id="c10">-</div></div>
    <div class="val"><div class="muted">Temperature &deg;C</div><div class="big" id="ctemp">-</div></div>
    <div class="val"><div class="muted">Humidity %</div><div class="big" id="chum">-</div></div>
    <div class="val"><div class="muted">Dew point &deg;C</div><div class="big" id="cdew">-</div></div>
  </div>
  <div class="muted" id="cupd">-</div>
</div>

<div class="card">
  <div class="kv" style="margin-bottom:6px">Files on the card</div>
  <table>
    <thead><tr><th>File</th><th>Size</th><th>Covers</th><th>Actions</th></tr></thead>
    <tbody id="fbody"></tbody>
  </table>
  <div class="ctrl" style="margin-top:10px">
    <button id="prevpage">Previous</button>
    <span class="muted" id="filepage">-</span>
    <button id="nextpage">Next</button>
  </div>
</div>

<div id="modal" class="modal">
  <div class="modalbox">
    <div class="modalhead">
      <b>Reference PM2.5 / PM10 values</b>
      <a href="#" id="close">&times; close</a>
    </div>
    <div class="modalbody">

      <h3>Baseline (reference)</h3>
      <table>
        <tr><th>Situation</th><th>PM2.5 (&micro;g/m&sup3;)</th></tr>
        <tr><td>Clean indoor air</td><td>5 - 15</td></tr>
        <tr><td>Urban outdoor background</td><td>10 - 30</td></tr>
        <tr><td>Polluted city / smog</td><td>50 - 150</td></tr>
        <tr><td>Cooking without an extractor hood</td><td>50 - 300</td></tr>
      </table>

      <h3>Cigarette smoke</h3>
      <table>
        <tr><th>Position</th><th>PM2.5 (&micro;g/m&sup3;)</th></tr>
        <tr><td>Right next to the smoke (within 1 m)</td><td>300 - 1500+</td></tr>
        <tr><td>Room average, one cigarette, closed room</td><td>20 - 100</td></tr>
        <tr><td>Room average, several cigarettes, smoky</td><td>150 - 500</td></tr>
        <tr><td>Small closed space, continuous smoking</td><td>300 - 1000</td></tr>
      </table>
      <p style="margin:4px 0 0">Rises within seconds, falls over 5 - 30 min depending on ventilation.</p>

      <h3>Fire smoke</h3>
      <table>
        <tr><th>Situation</th><th>PM2.5 (&micro;g/m&sup3;)</th></tr>
        <tr><td>Match, blown-out candle, incense</td><td>100 - 1000 (short peak)</td></tr>
        <tr><td>Fireplace / wood stove in the room</td><td>50 - 500</td></tr>
        <tr><td>Wildfire smoke outdoors, visible smoke</td><td>200 - 1000+</td></tr>
        <tr><td>Dense smoke, close range</td><td>1000 - 10000+</td></tr>
        <tr><td>Developed fire in a room</td><td>tens of thousands (sensor saturated)</td></tr>
      </table>

      <h3>Temperature / humidity reference (AM2302)</h3>
      <table>
        <tr><th>Condition</th><th>Value</th></tr>
        <tr><td>Comfortable living space</td><td>20 - 24 &deg;C, 40 - 60 % RH</td></tr>
        <tr><td>Too dry (dry eyes and throat, static)</td><td>below 30 % RH</td></tr>
        <tr><td>Too humid (mould risk)</td><td>above 60 % RH, high above 70 %</td></tr>
        <tr><td>Condensation on cold surfaces</td><td>dew point within ~3 &deg;C of the surface</td></tr>
      </table>
      <ul>
        <li><b>Dew point:</b> below 10 &deg;C comfortable, 16 - 20 &deg;C muggy,
            above 20 &deg;C oppressive.</li>
        <li><b>Sensor accuracy:</b> AM2302 is specified as &plusmn;0.5 &deg;C and
            &plusmn;2 - 5 % RH, with a resolution of 0.1.</li>
        <li><b>Polling:</b> it is read every 3 s here; do not poll it faster than
            once every 2 s.</li>
        <li><b>Mould:</b> grows when relative humidity next to a surface stays above
            ~80 % for days - watch the dew point, not only the RH.</li>
        <li><b>Note:</b> the AM2302 is not suitable for condensing environments.</li>
      </ul>

      <h3>PMS5003 sensor limits</h3>
      <ul>
        <li>Effective range: <b>0 - 500 &micro;g/m&sup3;</b>, maximum around
            <b>1000 &micro;g/m&sup3;</b>.</li>
        <li>Above that readings are unreliable and saturate - typically the value
            &quot;sticks&quot; to a constant (e.g. ~1000) while the smoke lasts.</li>
        <li>Cigarette smoke and moderate fire smoke are measured well; dense
            smoke indoors is seen only as a maximum.</li>
      </ul>

      <h3>Telling smoke types apart from the log</h3>
      <ul>
        <li><b>PM2.5 / PM10 ratio:</b> ~0.9 - 1.0 = fine aerosol (cigarette, fresh
            smoke, exhaust); ~0.5 - 0.8 = coarse fraction present (dust, ash,
            smoke close to the source).</li>
        <li><b>Rise time:</b> cigarette and match jump within seconds, dust rises
            gradually.</li>
        <li><b>Decay time:</b> cigarette smoke in a ventilated room halves in
            1 - 5 min; dust settles more slowly.</li>
      </ul>

      <h3>Humidity note</h3>
      <p style="margin:4px 0">The PMS5003 has no heated inlet, so above roughly 80 %
        relative humidity (fog, rain, early morning outdoors) it counts droplets as
        particles and shows falsely high values. For outdoor installations this is
        the most common source of &quot;phantom smoke&quot; in the data.</p>

      <h3>Practical chain test</h3>
      <p style="margin:4px 0">Light and blow out a match ~30 cm from the sensor - the
        log should show a peak within a few seconds and a gradual fall over
        1 - 3 minutes.</p>

    </div>
  </div>
</div>

<script>
var selFile = "";
var lastCount = -1;
var rows = [];
var srcLabel = "NTP";
var rangeSec = 300;                 // default range: last 5 minutes
var fromEpoch = 0;
var tillEpoch = 0;
var mode = "file";                  // "file" = single file, "range" = history range
var boardEpoch = 0;                 // current board time
var currentFile = "";               // active file on the board
var rangeFrom = 0;
var rangeTill = 0;
var lastRangeLoad = 0;
var rangeInitialised = false;
var currentTheme = "light";
var allFiles = [];
var filePage = 1;
var filesPerPage = 15;
var lastFilesVer = -1;              // /api/status "filesVer": file list changed?
var lastStatus = null;              // last /api/status payload (reused below)
var statusTimer = 0;                // poll timers (stopped while the tab is hidden)
var currentTimer = 0;

function q(id){ return document.getElementById(id); }
function getJSON(u){ return fetch(u, {cache:"no-store"}).then(function(r){ return r.json(); }); }

function fmtShort(ts){
  if (hasEpochTime() && ts > 1000000000) {
    var d = new Date(ts*1000);
    return ("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2)+":"+("0"+d.getSeconds()).slice(-2);
  }
  return (ts/1000).toFixed(0)+"s";
}

function hasEpochTime(){ return (srcLabel === "NTP" || srcLabel === "MANUAL"); }

function fmtFull(ts){
  if (!ts || ts < 1000000000) { return "-"; }
  if (hasEpochTime() && ts > 1000000000) {
    var d = new Date(ts*1000);
    return ("0"+d.getDate()).slice(-2)+"."+("0"+(d.getMonth()+1)).slice(-2)+"."+d.getFullYear()+
           " "+("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2)+":"+("0"+d.getSeconds()).slice(-2);
  }
  return (ts/1000).toFixed(0)+" s";
}

/* Always a date, whatever the time source is: used for the file table. */
function fmtStamp(ts){
  if (!ts || ts < 1000000000) { return "-"; }
  var d = new Date(ts * 1000);
  return ("0"+d.getDate()).slice(-2)+"."+("0"+(d.getMonth()+1)).slice(-2)+"."+d.getFullYear()+
         " "+("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2);
}

function fmtAxis(ts, span){
  if (hasEpochTime() && ts > 1000000000) {
    var d = new Date(ts*1000);
    if (span > 6*3600) {
      return ("0"+d.getDate()).slice(-2)+"."+("0"+(d.getMonth()+1)).slice(-2)+" "+("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2);
    }
    return ("0"+d.getHours()).slice(-2)+":"+("0"+d.getMinutes()).slice(-2)+":"+("0"+d.getSeconds()).slice(-2);
  }
  return (ts/1000).toFixed(0)+"s";
}

function rangeLabel(){
  if (rows.length > 1 && hasEpochTime()) {
    return fmtFull(rows[0][0]) + "  -  " + fmtFull(rows[rows.length-1][0]);
  }
  if (mode === "range" && rangeFrom) {
    return fmtFull(rangeFrom) + "  -  " + (rangeTill ? fmtFull(rangeTill) : "now");
  }
  return "single file";
}

/* Filter: the custom from-to range first (epoch only), then the quick range */
function applyRange(){
  return rows.slice();   // the rows already are exactly the requested range
}

function pollStatus(){
  getJSON("/api/status").then(function(s){
    lastStatus = s;
    if (s.filesVer !== undefined && s.filesVer !== lastFilesVer) {
      lastFilesVer = s.filesVer;      // the card changed: refresh the file table
      loadFiles();
    }
    q("clock").textContent = s.time;
    q("tsrc").textContent  = s.source;
    q("recs").textContent  = s.records;
    q("ota").textContent   = s.ota ? s.ota : "-";
    q("boot").textContent  = s.boot;
    q("reset").textContent = s.resetReason ? "(" + s.resetReason + ")" : "";
    if (s.theme && s.theme !== currentTheme) {
      currentTheme = s.theme;
      document.body.className = "theme-" + currentTheme;
      draw();
    }
    q("ethinfo").textContent = s.ethActive
        ? (s.ethIp + " / " + s.ethMask + "  gw " + s.ethGw + (s.ethDhcp ? "  (DHCP)" : "  (static)"))
        : "not active";
    q("apinfo").textContent = s.apEnabled
        ? (s.apSsid + "  " + s.apIp + "  channel " + s.apChannel + "  clients: " + s.apClients)
        : "disabled";
    renderSensorState(s);
    renderDhtState(s);
    renderSdHealth(s);
    renderPwWarning(s);
    if (s.epoch > 1000000000) { boardEpoch = s.epoch; }
    currentFile = s.file;
    if (selFile === "") { selFile = s.file; q("file").textContent = selFile; }

    if (!rangeInitialised && boardEpoch > 0) {
      rangeInitialised = true;
      fillRangeInputs(rangeSec > 0 ? rangeSec : 300);
      applyCustomRange();
    }

    if (s.records !== lastCount) {
      lastCount = s.records;
      if (mode === "file" && selFile === s.file) { loadData(true); }
    }
  }).catch(function(){});
}

/* The serial console is disabled, so a card that stops taking records (missing,
   full, failing) can only be reported here. The banner stays hidden while
   everything is fine. */
function fmtAge(sec){
  if (sec < 60)   { return sec + " s"; }
  if (sec < 3600) { return Math.floor(sec / 60) + " min " + (sec % 60) + " s"; }
  return Math.floor(sec / 3600) + " h " + Math.floor((sec % 3600) / 60) + " min";
}

function renderSdHealth(s){
  var el    = q("sdwarn");
  var head  = q("sdwarnhead");
  var body  = q("sdwarnbody");
  var msgs  = [];
  var age     = (typeof s.lastWriteAgeSec === "number") ? s.lastWriteAgeSec : -1;
  var freeMb  = (typeof s.sdFreeMb === "number") ? s.sdFreeMb : -1;
  var usedPct = (typeof s.sdUsedPct === "number") ? s.sdUsedPct : 0;

  if (s.sdOk === false) { msgs.push("the card is not ready"); }
  if (age >= 60) { msgs.push("no record written for " + fmtAge(age)); }
  if (s.sdOk !== false && freeMb >= 0 && (freeMb < 50 || usedPct >= 90)) {
    msgs.push("only " + freeMb + " MB free (" + usedPct + " % used)");
  }
  if (!msgs.length) {
    el.style.display = "none";
    head.textContent = "";
    body.textContent = "";
    return;
  }

  if (s.writeFails > 0) { msgs.push(s.writeFails + " failed writes since start"); }
  head.textContent = "SD card problem:";
  body.textContent = " " + msgs.join("; ") + ".";
  el.style.display = "flex";
}

/* Yellow banner while the documented factory passwords are still in use. */
function renderPwWarning(s){
  var el   = q("pwwarn");
  var body = q("pwwarnbody");
  var what = [];
  if (s.defaultAdminPwd) { what.push("the administration password"); }
  if (s.defaultApPwd)    { what.push("the Wi-Fi AP password"); }
  if (!what.length) {
    el.style.display = "none";
    body.textContent = "";
    return;
  }
  body.innerHTML = " " + what.join(" and ") + " " + (what.length > 1 ? "are" : "is") +
                   " still the documented default. Change it on the " +
                   "<a href=\"/admin\">administration page</a>.";
  el.style.display = "flex";
}

function renderSensorState(s){
  var el = q("cage");
  var vals = q("cvals");
  var age = (typeof s.ageSec === "number") ? s.ageSec : -1;
  var frames = "  |  frames: " + s.framesOk + " (rejected " + s.framesBad + ")";

  if (!s.current || s.current.ts === 0) {
    el.textContent = "sensor: no data yet" + frames;
    el.style.color = "#b91c1c";
    vals.className = "vals stale";
  } else if (age >= 0 && age < 5) {
    el.textContent = "sensor: OK (last sample " + age + " s ago)" + frames;
    el.style.color = "#15803d";
    vals.className = "vals";
  } else if (age >= 0 && age < 15) {
    el.textContent = "sensor: delayed (last sample " + age + " s ago)" + frames;
    el.style.color = "#b45309";
    vals.className = "vals stale";
  } else {
    el.textContent = "sensor: NO DATA for " + age + " s" + frames;
    el.style.color = "#b91c1c";
    vals.className = "vals stale";
  }
}

function renderDhtState(s){
  var el = q("dhtstate");
  if (!s.dhtOk) {
    el.textContent = "AM2302: no data yet (failed reads: " + s.dhtBad + ")";
    el.style.color = "#b91c1c";
    return;
  }
  var age = (typeof s.dhtAgeSec === "number") ? s.dhtAgeSec : -1;
  var txt = "AM2302: " + s.current.temp + " \u00b0C / " + s.current.hum + " %";
  if (age >= 0 && age < 15) { el.style.color = "#15803d"; txt += "  (read " + age + " s ago)"; }
  else { el.style.color = "#b45309"; txt += "  (last read " + age + " s ago)"; }
  el.textContent = txt;
}

/* Refreshes the value tiles every 5 s from the last /api/status payload: the
   same numbers arrive with the 2 s status poll, so a second request per tick
   would only add traffic. */
function renderCurrent(){
  var s = lastStatus;
  if (!s) { return; }
  if (s.current) {
    q("c1").textContent  = s.current.pm1;
    q("c25").textContent = s.current.pm25;
    q("c10").textContent = s.current.pm100;
    q("ctemp").textContent = (s.current.temp === null || s.current.temp === undefined) ? "-" : s.current.temp;
    q("chum").textContent  = (s.current.hum  === null || s.current.hum  === undefined) ? "-" : s.current.hum;
    q("cdew").textContent  = (s.current.temp === null || s.current.temp === undefined || s.current.hum === null || s.current.hum === undefined)
                             ? "-" : dewPoint(parseFloat(s.current.temp), parseFloat(s.current.hum)).toFixed(1);
  }
  q("cupd").textContent = "read at: " + s.time + " (every 5 s)";
}

function loadRange(){
  var qs = "/api/data?from=" + rangeFrom + "&to=" + rangeTill;
  getJSON(qs).then(function(d){
    rows = d.rows || [];
    srcLabel = d.source || "NTP";
    lastRangeLoad = Date.now()/1000;
    var nf = d.files ? d.files.split(",").length : 0;
    var info = "history: " + nf + (nf === 1 ? " file" : " files");
    if (d.files) { info += " (" + d.files + ")"; }
    info += "  |  records: " + rows.length;
    if (d.decim > 1) { info += " of " + d.total + " (decimated x" + d.decim + ")"; }
    info += "  |  time source: " + srcLabel;
    q("chartinfo").textContent = info;
    q("file").textContent = "history";
    q("live").className = "";
    draw();
  }).catch(function(){});
}

/* Live view. The first load fetches the whole RAM buffer of the active file;
   every later refresh adds "since" (the newest timestamp already on screen), so
   the board answers with the new rows only - a few hundred bytes instead of the
   whole buffer. Whenever the answer cannot be appended safely (another file,
   buffer rotated, clock re-dated) the response replaces the chart instead. */
var liveSince   = 0;      // newest timestamp the chart already holds
var liveLoading = false;  // one refresh at a time (responses must stay ordered)

function loadData(incremental){
  if (liveLoading) { return; }
  var url = "/api/data?f=" + encodeURIComponent(selFile);
  if (incremental && liveSince > 0) { url += "&since=" + liveSince; }
  liveLoading = true;
  getJSON(url).then(function(d){
    if (!d || !d.rows) { return; }      // error answer: leave the chart as it is
    var got    = d.rows || [];
    var oldest = (d.oldest === undefined) ? 0 : d.oldest;
    if (incremental && liveSince > 0 && oldest > 0 && liveSince >= oldest &&
        d.file === selFile) {
      Array.prototype.push.apply(rows, got);      // only the new rows arrived
    } else {
      rows = got;                                 // full reload
    }
    if (oldest > 0) {                             // drop what the board dropped
      var keep = [];
      for (var i = 0; i < rows.length; i++) {
        if (rows[i][0] >= oldest) { keep.push(rows[i]); }
      }
      rows = keep;
    }
    if (rows.length) { liveSince = rows[rows.length - 1][0]; }
    srcLabel = d.source || "NTP";
    q("live").className = "on";
    var win = "";
    if (rows.length > 1) {          // say how much of the file the buffer covers
      win = "  |  " + fmtFull(rows[0][0]) + "  -  " + fmtFull(rows[rows.length - 1][0]);
    }
    q("chartinfo").textContent = d.file + "  |  " + rows.length + " records" + win +
                                 "  |  time source: " + srcLabel;
    draw();
  }).catch(function(){}).then(function(){ liveLoading = false; });
}

function loadFiles(){
  getJSON("/api/files").then(function(d){
    allFiles = (d.files || []).slice().sort(function(a, b){
      if (a.name < b.name) return 1;
      if (a.name > b.name) return -1;
      return 0;
    });
    renderFiles();
  }).catch(function(){});
}

/* Shows up to filesPerPage files (newest first); the rest are on later pages */
function renderFiles(){
  var tb = q("fbody");
  tb.innerHTML = "";
  var pages = Math.max(1, Math.ceil(allFiles.length / filesPerPage));
  if (filePage > pages) filePage = pages;
  if (filePage < 1) filePage = 1;
  var start = (filePage - 1) * filesPerPage;
  allFiles.slice(start, start + filesPerPage).forEach(function(f){
    var tr = document.createElement("tr");

    var td1 = document.createElement("td");
    td1.textContent = f.name;
    tr.appendChild(td1);

    var td2 = document.createElement("td");
    td2.textContent = f.size + " B";
    tr.appendChild(td2);

    var tdSpan = document.createElement("td");
    tdSpan.className = "muted";
    tdSpan.textContent = (f.from ? fmtStamp(f.from) : "-") + "  -  " +
                         (f.to ? fmtStamp(f.to) : "now");
    tr.appendChild(tdSpan);

    var td3 = document.createElement("td");

    // No per-file "graph" link: the chart is driven by the From/To range filter
    // and by Live (the active file) instead.
    var a2 = document.createElement("a");
    a2.textContent = "download";
    a2.href = "/download?f=" + encodeURIComponent(f.name);
    td3.appendChild(a2);

    if (f.active) {
      td3.appendChild(document.createTextNode(" | (active)"));
    } else {
      td3.appendChild(document.createTextNode(" | "));
      var form = document.createElement("form");
      form.method = "POST";
      form.action = "/delete";
      form.style.display = "inline";
      form.onsubmit = function(){ return confirm("Delete " + f.name + " ?"); };
      var hidden = document.createElement("input");
      hidden.type = "hidden";
      hidden.name = "f";
      hidden.value = f.name;
      form.appendChild(hidden);
      var del = document.createElement("button");
      del.type = "submit";
      del.textContent = "delete";
      del.className = "del";
      form.appendChild(del);
      td3.appendChild(form);
    }

    tr.appendChild(td3);
    tb.appendChild(tr);
  });
  q("filepage").textContent = "Page " + filePage + " / " + pages + "   (" + allFiles.length + " files)";
  q("prevpage").disabled = (filePage <= 1);
  q("nextpage").disabled = (filePage >= pages);
}

/* Colour palette per theme (also used for the exported PNG) */
function palette(){
  if (currentTheme === "dark") {
    return {bg:"#171d26", grid:"#2a3342", axis:"#5b6a80", label:"#94a3b8", text:"#e7ebf2",
            pm1:"#22c55e", pm25:"#f97316", pm10:"#ef4444", temp:"#a78bfa", hum:"#22d3ee"};
  }
  if (currentTheme === "contrast") {
    return {bg:"#000000", grid:"#3a3a3a", axis:"#ffffff", label:"#ffd400", text:"#ffffff",
            pm1:"#00ff66", pm25:"#ffcc00", pm10:"#ff3b30", temp:"#c084fc", hum:"#22d3ee"};
  }
  return {bg:"#ffffff", grid:"#e6e8eb", axis:"#aaa", label:"#666", text:"#333",
          pm1:"#16a34a", pm25:"#ea580c", pm10:"#dc2626", temp:"#7c3aed", hum:"#0891b2"};
}

/* Draw the chart into the given context */
function drawChart(g, L, T, pw, ph, data, fs, lw){
  g.font = fs + "px Arial";
  var pal = palette();

  if (!data.length) {
    g.fillStyle = pal.label;
    g.fillText("No records in the selected range.", L, T + fs + 4);
    return;
  }

  var xmin = data[0][0], xmax = data[data.length-1][0];
  if (xmax <= xmin) xmax = xmin + 1;
  var ymax = 10, i, v, t, x, y;
  data.forEach(function(r){ ymax = Math.max(ymax, r[1], r[2], r[3]); });
  ymax = Math.ceil(ymax * 1.15);

  function X(tt){ return L + (tt - xmin) * pw / (xmax - xmin); }
  function Y(vv){ return T + ph - vv * ph / ymax; }

  g.strokeStyle = pal.grid;
  g.fillStyle = pal.label;
  g.lineWidth = 1;
  for (i = 0; i <= 4; i++) {
    v = ymax * i / 4; y = Y(v);
    g.beginPath(); g.moveTo(L, y); g.lineTo(L + pw, y); g.stroke();
    g.textAlign = "right"; g.fillText(v.toFixed(0), L - 6, y + fs*0.35);
  }
  for (i = 0; i <= 4; i++) {
    t = xmin + (xmax - xmin) * i / 4; x = X(t);
    g.beginPath(); g.moveTo(x, T); g.lineTo(x, T + ph); g.stroke();
    g.textAlign = "center"; g.fillText(fmtAxis(Math.round(t), xmax - xmin), x, T + ph + fs + 4);
  }

  g.strokeStyle = pal.axis;
  g.beginPath(); g.moveTo(L, T); g.lineTo(L, T + ph); g.lineTo(L + pw, T + ph); g.stroke();

  /* Y axis title */
  g.save();
  g.translate(fs * 0.95, T + ph / 2);
  g.rotate(-Math.PI / 2);
  g.textAlign = "center";
  g.fillStyle = pal.label;
  g.fillText("\u00b5g/m\u00b3", 0, 0);
  g.restore();

  var series = [
    {idx:1, col:pal.pm1,  name:"PM1.0"},
    {idx:2, col:pal.pm25, name:"PM2.5"},
    {idx:3, col:pal.pm10, name:"PM10"}
  ];

  series.forEach(function(s){
    g.strokeStyle = s.col;
    g.lineWidth = lw;
    g.beginPath();
    data.forEach(function(r, k){
      var px = X(r[0]), py = Y(r[s.idx]);
      if (k === 0) g.moveTo(px, py); else g.lineTo(px, py);
    });
    g.stroke();
    g.fillStyle = s.col;
    data.forEach(function(r){
      g.beginPath();
      g.arc(X(r[0]), Y(r[s.idx]), lw + 0.6, 0, 6.2832);
      g.fill();
    });
  });

  var lx = L;
  series.forEach(function(s){
    g.fillStyle = s.col; g.fillRect(lx, T - fs*1.4, fs*0.9, fs*0.9);
    g.fillStyle = pal.text; g.textAlign = "left";
    g.fillText(s.name, lx + fs*1.2, T - fs*0.5);
    lx += fs * 6.4;
  });
}

/* Magnus formula dew point in degC from temperature (degC) and RH (%) */
function dewPoint(tc, rh){
  if (!(rh > 0)) return 0;
  var a = 17.27, b = 237.7;
  var al = Math.log(rh / 100.0) + (a * tc) / (b + tc);
  return (b * al) / (a - al);
}

/* Second chart: temperature and humidity (Y axis 0..100 covers % and degC) */
function drawEnvChart(g, L, T, pw, ph, data, fs, lw){
  g.font = fs + "px Arial";
  var pal = palette();

  var d = data.filter(function(r){ return r[4] !== null && r[4] !== undefined; });
  if (!d.length) {
    g.fillStyle = pal.label;
    g.fillText("No temperature / humidity data in the selected range.", L, T + fs + 4);
    return;
  }

  var xmin = data[0][0], xmax = data[data.length-1][0];
  if (xmax <= xmin) xmax = xmin + 1;

  /* temperature has its own auto-scaled left axis, humidity a fixed 0..100 right axis */
  var tlo = d[0][4], thi = d[0][4], i, y;
  d.forEach(function(r){ tlo = Math.min(tlo, r[4]); thi = Math.max(thi, r[4]); });
  tlo = Math.floor(tlo - 1);
  thi = Math.ceil(thi + 1);
  if (thi - tlo < 4) { tlo -= 2; thi = tlo + 4; }
  var hlo = 0, hhi = 100;

  function X(tt){ return L + (tt - xmin) * pw / (xmax - xmin); }
  function YT(vv){ return T + ph - (vv - tlo) * ph / (thi - tlo); }
  function YH(vv){ return T + ph - (vv - hlo) * ph / (hhi - hlo); }

  g.strokeStyle = pal.grid;
  g.lineWidth = 1;
  for (i = 0; i <= 4; i++) {
    y = T + ph - ph * i / 4;
    g.beginPath(); g.moveTo(L, y); g.lineTo(L + pw, y); g.stroke();
    g.fillStyle = pal.temp; g.textAlign = "right";
    g.fillText((tlo + (thi - tlo) * i / 4).toFixed(1), L - 6, y + fs*0.35);
    g.fillStyle = pal.hum; g.textAlign = "left";
    g.fillText((hlo + (hhi - hlo) * i / 4).toFixed(0), L + pw + 8, y + fs*0.35);
  }
  for (i = 0; i <= 4; i++) {
    var tt = xmin + (xmax - xmin) * i / 4;
    var xx = X(tt);
    g.beginPath(); g.moveTo(xx, T); g.lineTo(xx, T + ph); g.stroke();
    g.fillStyle = pal.label; g.textAlign = "center";
    g.fillText(fmtAxis(Math.round(tt), xmax - xmin), xx, T + ph + fs + 4);
  }
  g.strokeStyle = pal.axis;
  g.beginPath(); g.moveTo(L, T); g.lineTo(L, T + ph); g.lineTo(L + pw, T + ph); g.stroke();

  g.fillStyle = pal.temp; g.textAlign = "left";
  g.fillText("\u00b0C", 6, T + 10);
  g.fillStyle = pal.hum; g.textAlign = "right";
  g.fillText("%", L + pw + 34, T + 10);

  var series = [
    {idx:4, col:pal.temp, name:"Temperature \u00b0C", Y:YT},
    {idx:5, col:pal.hum,  name:"Humidity %",      Y:YH}
  ];

  series.forEach(function(s){
    g.strokeStyle = s.col;
    g.lineWidth = lw;
    g.beginPath();
    var prvi = true;
    d.forEach(function(r){
      var vv = r[s.idx];
      if (vv === null || vv === undefined) { prvi = true; return; }
      var px = X(r[0]), py = s.Y(vv);
      if (prvi) { g.moveTo(px, py); prvi = false; } else { g.lineTo(px, py); }
    });
    g.stroke();
    g.fillStyle = s.col;
    d.forEach(function(r){
      var vv = r[s.idx];
      if (vv === null || vv === undefined) return;
      g.beginPath();
      g.arc(X(r[0]), s.Y(vv), lw + 0.6, 0, 6.2832);
      g.fill();
    });
  });

  var lx = L;
  series.forEach(function(s){
    g.fillStyle = s.col; g.fillRect(lx, T - fs*1.4, fs*0.9, fs*0.9);
    g.fillStyle = pal.text; g.textAlign = "left";
    g.fillText(s.name, lx + fs*1.2, T - fs*0.5);
    lx += fs * 12;
  });
}

function draw(){
  var cv = q("chart");
  var w = cv.clientWidth || 640;
  var h = cv.clientHeight || 320;
  var dpr = window.devicePixelRatio || 1;
  cv.width = Math.round(w*dpr);
  cv.height = Math.round(h*dpr);

  var g = cv.getContext("2d");
  g.setTransform(dpr,0,0,dpr,0,0);
  g.clearRect(0,0,w,h);

  var d = applyRange();
  var info = "showing " + d.length + " of " + rows.length + " records";
  if (d.length > 1) { info += "  |  range: " + fmtFull(d[0][0]) + "  -  " + fmtFull(d[d.length-1][0]); }
  if (mode === "range") { info += "  |  requested: " + rangeLabel(); }
  q("rangeinfo").textContent = info;

  drawChart(g, 52, 26, w-52-10, h-26-28, d, 12, 2);

  var cve = q("chartEnv");
  var we = cve.clientWidth || 640;
  var he = cve.clientHeight || 220;
  cve.width = Math.round(we*dpr);
  cve.height = Math.round(he*dpr);
  var ge = cve.getContext("2d");
  ge.setTransform(dpr,0,0,dpr,0,0);
  ge.clearRect(0,0,we,he);
  drawEnvChart(ge, 52, 26, we-52-46, he-26-28, d, 12, 2);
  var envRows = d.filter(function(r){ return r[4] !== null && r[4] !== undefined; });
  var envTxt = "temperature / humidity: " + envRows.length + " of " + d.length + " records";
  if (envRows.length) {
    var tmin = envRows[0][4], tmax = envRows[0][4], hmin = envRows[0][5], hmax = envRows[0][5], ts = 0, hs = 0;
    envRows.forEach(function(r){
      tmin = Math.min(tmin, r[4]); tmax = Math.max(tmax, r[4]);
      hmin = Math.min(hmin, r[5]); hmax = Math.max(hmax, r[5]);
      ts += r[4]; hs += r[5];
    });
    var tavg = ts / envRows.length, havg = hs / envRows.length;
    envTxt += "  |  temp min/avg/max: " + tmin.toFixed(1) + " / " + tavg.toFixed(1) + " / " + tmax.toFixed(1) + " \u00b0C";
    envTxt += "  |  humidity min/avg/max: " + hmin.toFixed(1) + " / " + havg.toFixed(1) + " / " + hmax.toFixed(1) + " %";
    envTxt += "  |  dew point (avg): " + dewPoint(tavg, havg).toFixed(1) + " \u00b0C";
  }
  q("chartinfoEnv").textContent = envTxt;
}

function exportPNG(){
  var d = applyRange();
  var w = 1200, h = 880;
  var cv = document.createElement("canvas");
  cv.width = w; cv.height = h;
  var g = cv.getContext("2d");

  var pal = palette();
  g.fillStyle = pal.bg; g.fillRect(0, 0, w, h);
  g.fillStyle = pal.text; g.font = "bold 20px Arial"; g.textAlign = "left";
  g.fillText("PMS5003 - " + (mode === "range" ? "history" : selFile), 24, 34);
  g.font = "14px Arial"; g.fillStyle = pal.label;
  g.fillText("Range: " + rangeLabel() + "  |  records: " + d.length +
             "  |  time source: " + srcLabel +
             "  |  exported: " + new Date().toLocaleString(), 24, 58);

  drawChart(g, 70, 100, w - 70 - 40, 320, d, 14, 2.4);

  g.fillStyle = pal.text; g.font = "bold 16px Arial"; g.textAlign = "left";
  g.fillText("Temperature / humidity", 24, 480);
  drawEnvChart(g, 70, 505, w - 70 - 60, 320, d, 14, 2.4);

  var a = document.createElement("a");
  var ts = new Date().toISOString().replace(/[:T]/g, "-").slice(0, 19);
  a.download = "pms_chart_" + ts + ".png";
  a.href = cv.toDataURL("image/png");
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
}

/* Live view: chart of the active file, refreshed on every new record */
function goLive(){
  mode = "file";
  if (currentFile) { selFile = currentFile; }
  rangeFrom = 0;
  rangeTill = 0;
  lastCount = -1;
  liveSince = 0;                            // back to live: reload the buffer
  q("file").textContent = selFile;
  loadData();
}

/* Downloads the records of the From/To range as CSV (the chart is untouched) */
function downloadCsv(){
  var fromV = q("from").value;
  var tillV = q("till").value;
  if (!fromV || !tillV) { q("chartinfo").textContent = "Set both From and To before downloading."; return; }
  var fromEp = Math.floor(new Date(fromV).getTime()/1000);
  var tillEp = Math.floor(new Date(tillV).getTime()/1000);
  if (!(fromEp > 1000000000) || !(tillEp > 1000000000)) { q("chartinfo").textContent = "Invalid date/time."; return; }
  if (tillEp <= fromEp) { tillEp = fromEp + 60; }
  var a = document.createElement("a");
  a.href = "/export?from=" + fromEp + "&to=" + tillEp;
  a.download = "";
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
}

function pad2(v){ return (v < 10 ? "0" : "") + v; }

function toLocalInput(d){
  return d.getFullYear() + "-" + pad2(d.getMonth()+1) + "-" + pad2(d.getDate()) +
         "T" + pad2(d.getHours()) + ":" + pad2(d.getMinutes()) + ":" + pad2(d.getSeconds());
}

/* Fills From/To for a quick range (120 = 2 min, 300 = 5 min ...) */
function fillRangeInputs(sec){
  var now = new Date();
  q("from").value = toLocalInput(new Date(now.getTime() - sec*1000));
  q("till").value = toLocalInput(now);
}

/* Loads the chart for the From/To fields (quick ranges only fill them in) */
function applyCustomRange(){
  var fromV = q("from").value;
  var tillV = q("till").value;
  if (!fromV || !tillV) {
    q("chartinfo").textContent = "Set both From and To (or pick a quick range).";
    return;
  }
  var fromEp = Math.floor(new Date(fromV).getTime()/1000);
  var tillEp = Math.floor(new Date(tillV).getTime()/1000);
  if (!(fromEp > 1000000000) || !(tillEp > 1000000000)) {
    q("chartinfo").textContent = "Invalid date/time.";
    return;
  }
  if (tillEp <= fromEp) { tillEp = fromEp + 60; }

  mode = "range";
  rangeFrom = fromEp;
  rangeTill = tillEp;
  loadRange();
}


window.addEventListener("load", function(){
  /* Remember the selected range in the browser (localStorage) */
  try {
    var sp = localStorage.getItem("pms_range");
    if (sp !== null) {
      q("range").value = sp;
      rangeSec = parseInt(sp, 10) || 0;
    } else {
      q("range").value = "300";
      rangeSec = 300;
    }
  } catch (e) {}

  q("range").onchange = function(){
    var sec = parseInt(this.value, 10) || 0;
    try { localStorage.setItem("pms_range", this.value); } catch (e) {}
    if (sec > 0) { fillRangeInputs(sec); }   // only fills the fields, press Apply to load
  };
  q("apply").onclick = applyCustomRange;
  q("live").onclick = goLive;
  q("csv").onclick = downloadCsv;
  q("png").onclick = exportPNG;
  q("prevpage").onclick = function(){ filePage--; renderFiles(); };
  q("nextpage").onclick = function(){ filePage++; renderFiles(); };

  /* Modal with reference values */
  q("help").onclick = function(e){ e.preventDefault(); q("modal").className = "modal open"; };
  q("close").onclick = function(e){ e.preventDefault(); q("modal").className = "modal"; };
  q("modal").onclick = function(e){ if (e.target === this) q("modal").className = "modal"; };
  document.addEventListener("keydown", function(e){
    if (e.key === "Escape") q("modal").className = "modal";
  });

  pollStatus();
  loadFiles();
  statusTimer  = setInterval(pollStatus, 2000);    // graph on change + networks + time
  currentTimer = setInterval(renderCurrent, 5000); // current values (from the last poll)
  window.addEventListener("resize", draw);

  /* A tab that nobody is looking at should not poll the board all night: stop
     both timers while it is hidden and refresh once when it comes back. */
  document.addEventListener("visibilitychange", function(){
    if (document.hidden) {
      clearInterval(statusTimer);
      clearInterval(currentTimer);
    } else {
      pollStatus();
      renderCurrent();
      statusTimer  = setInterval(pollStatus, 2000);
      currentTimer = setInterval(renderCurrent, 5000);
    }
  });
});
</script>
</body>
</html>
)HTML";

static const char ADMIN_STYLE[] PROGMEM = R"CSS(
:root{--bg:#f5f7fa;--card:#fff;--text:#111827;--muted:#6b7280;--line:#e5e7eb;--accent:#2563eb;--accent-dark:#1d4ed8;--r:14px}
*{box-sizing:border-box}
body{margin:0;padding:18px;background:var(--bg);color:var(--text);font:14px/1.55 system-ui,-apple-system,"Segoe UI",Roboto,Arial,sans-serif}
.card{background:var(--card);border:1px solid var(--line);border-radius:var(--r);max-width:880px;margin:0 auto 14px;box-shadow:0 1px 2px rgba(16,24,40,.05),0 10px 28px rgba(16,24,40,.06);padding:18px}
h1{font-size:19px;font-weight:650;margin:0 0 6px;display:flex;flex-wrap:wrap;gap:8px 14px;align-items:center}
h3{font-size:14.5px;font-weight:650;margin:20px 0 8px;padding-bottom:6px;border-bottom:1px solid var(--line)}
table{border-collapse:separate;border-spacing:0;font-size:13.5px;width:100%}
td{padding:6px 8px 6px 0;vertical-align:middle}
td:first-child{color:var(--muted);width:130px}
input,select{font:inherit;font-size:13px;padding:7px 10px;border:1px solid var(--line);border-radius:10px;background:#fff;color:var(--text);width:100%;max-width:360px}
input[type=radio],input[type=checkbox]{width:auto;margin-right:6px;vertical-align:middle}
input:focus{outline:2px solid #eef4ff;border-color:var(--accent)}
button{font:inherit;font-size:13px;font-weight:600;padding:9px 16px;border:1px solid var(--accent);border-radius:10px;background:var(--accent);color:#fff;cursor:pointer;transition:.15s}
button:hover{background:var(--accent-dark);border-color:var(--accent-dark)}
a{color:var(--accent);text-decoration:none;font-weight:500}
a:hover{text-decoration:underline}
hr{border:none;border-top:1px solid var(--line);margin:22px 0}
.muted{color:var(--muted);font-size:12px}
p{margin:8px 0}
@media(max-width:640px){body{padding:10px}.card{padding:13px}td:first-child{width:auto}}
)CSS";

static const char ADMIN_SCRIPT[] PROGMEM = R"JS(<script>
function genApPwd(){
  var alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789-_@#%*";
  var buf = new Uint8Array(12), out = "";
  if (window.crypto && crypto.getRandomValues) { crypto.getRandomValues(buf); }
  else { for (var i = 0; i < 12; i++) { buf[i] = Math.floor(Math.random() * 256); } }
  for (var i = 0; i < 12; i++) { out += alphabet.charAt(buf[i] % alphabet.length); }
  var f = document.getElementsByName("aploz")[0];
  if (f) { f.value = out; f.focus(); }
}
</script>
)JS";

#endif  // WEB_ASSETS_H
