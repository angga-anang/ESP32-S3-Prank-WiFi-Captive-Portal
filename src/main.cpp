/*
 * PRANK-WIFI - ESP32-S3 WiFi Prank Captive Portal
 *
 * Membuat AP palsu (nama bisa diatur), ketika korban konek dan menekan
 * "Lanjutkan" di captive portal, audio prank diputar & pesan
 * "ANDA KENA PRANK!" muncul.
 *
 * Fitur:
 *  - Captive portal otomatis muncul
 *  - Audio prank embedded (base64) atau streaming (/s/audio)
 *  - Admin panel (/admin) untuk ubah SSID, sandi, upload audio, dsb.
 *  - Auto-kick (deauth semua klien tiap X detik)
 *  - Volume kontrol & reveal delay
 *  - Penyimpanan pengaturan di NVS
 *  - Audio disimpan di LittleFS
 *
 * Rekonstruksi dari firmware ESP32-S3 flash dump.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <esp_wifi.h>

// ======================== DEFAULT CONFIG ========================
static const char* DEFAULT_SSID      = "<Wifi-Gratis";
static const char* DEFAULT_AP_PASS   = "";           // kosong = open
static const char* DEFAULT_ADMIN_PASS= "admin";
static const uint16_t DEFAULT_KICK   = 0;            // 0 = mati
static const uint8_t  DEFAULT_VOL    = 100;          // persen
static const uint16_t DEFAULT_REVEAL = 4;            // detik

// ======================== GLOBALS ========================
WebServer server(80);
DNSServer dnsServer;
Preferences prefs;

String cfgSSID;
String cfgApPass;
String cfgAdminPass;
uint16_t cfgKick;       // auto-kick interval (detik), 0=off
uint8_t  cfgVol;        // volume 0-100
uint16_t cfgRevealSec;  // reveal delay in seconds

String audioName;       // nama file asli yang diupload
String audioPath;       // path di LittleFS (mis: /prank.wav)
String audioMime;       // MIME type
uint32_t audioBytes;    // ukuran file

bool adminLoggedIn = false;
unsigned long lastKickMs = 0;

// ======================== HTML PAGES ========================
// Disimpan sebagai raw string literals, placeholder __XXX__ diganti runtime

static const char PAGE_ADMIN[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="id">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Admin AP</title>
<style>
:root{--bg:#eceff1;--card:#fff;--ink:#212f38;--muted:#5c6b76;--line:#d7dee3;
--ok:#1b7f51;--ok-dark:#156a43;--warn:#9a5b07;--danger:#b3261e}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--ink);font:15px/1.5 Roboto,-apple-system,"Segoe UI",Arial,sans-serif;padding:28px 16px}
.wrap{max-width:640px;margin:0 auto}
h1{font-size:20px;font-weight:700}
.sub{color:var(--muted);font-size:13px;margin:2px 0 20px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:20px;margin-bottom:16px}
h2{font-size:14px;font-weight:700;margin-bottom:14px}
.row{display:flex;gap:10px;align-items:center;margin-bottom:10px;flex-wrap:wrap}
label{flex:0 0 160px;color:var(--muted);font-size:13px}
input{flex:1;min-width:180px;padding:9px 10px;border:1px solid var(--line);border-radius:8px;font:inherit;background:#fff}
input:focus{outline:2px solid rgba(27,127,81,.35);border-color:var(--ok)}
.hint{flex-basis:100%;color:var(--muted);font-size:12px;margin-top:-4px}
input[type=file]{padding:6px}
button{padding:10px 16px;border:0;border-radius:8px;font:inherit;font-weight:600;cursor:pointer}
.btn-ok{background:var(--ok);color:#fff}.btn-ok:hover{background:var(--ok-dark)}
.btn{background:#e3e8ec;color:var(--ink)}.btn:hover{background:#d3dbe1}
.btn-warn{background:#f2e4d0;color:var(--warn)}.btn-warn:hover{background:#e9d3ad}
.btn-danger{background:#e7d5d3;color:var(--danger)}.btn-danger:hover{background:#dcc4c1}
.stat{display:flex;justify-content:space-between;gap:10px;border-bottom:1px solid var(--line);padding:7px 0;font-size:14px}
.stat:last-child{border-bottom:0}
.stat b{font-weight:600;text-align:right}
audio{width:100%;margin-top:10px}
#toast{position:fixed;top:16px;left:50%;transform:translateX(-50%) translateY(-160%);background:var(--ink);color:#fff;padding:10px 18px;border-radius:24px;font-size:14px;font-weight:600;opacity:0;transition:transform .25s,opacity .25s;z-index:99;max-width:90%;text-align:center;pointer-events:none}
#toast.ok{background:var(--ok)}
#toast.err{background:var(--danger)}
#toast.show{transform:translateX(-50%) translateY(0);opacity:1}
</style>
</head>
<body>
<div class="wrap">
<h1>Admin AP</h1>
<p class="sub">__SSID__ - http://__IP__/admin</p>

<div class="card">
<h2>Status</h2>
<div class="stat"><span>SSID aktif</span><b id="s-ssid">__SSID__</b></div>
<div class="stat"><span>Klien terhubung</span><b id="s-clients">-</b></div>
<div class="stat"><span>File audio</span><b id="s-audio">-</b></div>
<div class="stat"><span>Tersimpan sebagai</span><b id="s-audiofile">-</b></div>
<div class="stat"><span>Penyimpanan audio</span><b id="s-disk">-</b></div>
</div>

<form class="card" id="f-settings">
<h2>Pengaturan hotspot</h2>
<div class="row"><label for="ssid">Nama Wi-Fi</label><input id="ssid" name="ssid" maxlength="32" required></div>
<div class="row"><label for="apPass">Sandi Wi-Fi</label><input id="apPass" name="apPass" maxlength="63" placeholder="kosong = tanpa sandi">
<div class="hint">Minimal 8 karakter jika diisi. Kosongkan supaya jaringan terbuka.</div></div>
<div class="row"><label for="adminPass">Sandi admin</label><input id="adminPass" name="adminPass" maxlength="32" required></div>
<div class="row"><label for="kick">Auto-kick (detik)</label><input id="kick" name="kick" type="number" min="0" max="3600" step="5">
<div class="hint">0 = mati. Jika diisi, semua klien diputus tiap X detik sehingga portal muncul berulang.</div></div>
<div class="row"><label for="vol">Volume suara prank</label>
<div style="flex:1;min-width:180px;display:flex;align-items:center;gap:10px">
<input id="vol" name="vol" type="range" min="0" max="100" step="5" value="100" style="flex:1">
<b id="volv" style="min-width:42px;text-align:right">100%</b>
</div>
<div class="hint">Kerasnya suara di perangkat target (0-100%). 100% = maksimal. Tidak mengubah volume sistem HP.</div></div>
<div class="row"><label for="reveal">Prank muncul setelah</label>
<div style="flex:1;min-width:180px;display:flex;align-items:center;gap:10px">
<input id="reveal" name="reveal" type="range" min="1" max="60" step="1" value="4" style="flex:1">
<b id="revealv" style="min-width:80px;text-align:right">4 dtk</b>
</div>
<div class="hint">Drama "Menghubungkan..." sebelum card "ANDA KENA PRANK!" tampil (1-60 dtk). Tetap sinkron ke suara yang berhenti; kalau suara tak pernah mulai, card tetap muncul beberapa detik kemudian.</div></div>
<button type="submit" class="btn-ok">Simpan</button>
</form>

<form class="card" id="f-upload" enctype="multipart/form-data" method="post" action="/upload">
<h2>Audio prank</h2>
<div class="row"><label for="file">File suara</label><input id="file" name="file" type="file" accept=".mp3,.wav,.ogg,.m4a,audio/*" required></div>
<div class="hint">MP3 paling kecil &amp; instan (&le;64 KB otomatis ditanam di halaman portal). Maks sekitar 900 KB.</div>
<button type="submit" class="btn-ok">Upload &amp; pakai</button>
</form>

<div class="card">
<h2>Uji &amp; kendali</h2>
<audio id="player" controls preload="none" src="/s/audio"></audio>
<div class="row" style="margin-top:14px">
<button type="button" class="btn" id="btest">Putar di sini</button>
<button type="button" class="btn-warn" id="bkick">Putuskan semua klien</button>
<button type="button" class="btn-danger" id="breboot">Reboot papan</button>
<button type="button" class="btn" id="blogout">Keluar</button>
</div>
</div>

<div id="toast" role="status"></div>
</div>
<script>
function $(id){return document.getElementById(id)}
function post(url,body){return fetch(url,{method:'POST',body:body||null}).then(r=>r.json()).catch(()=>({ok:false}))}
var toastTimer=0;
function toast(t,ok){
  var el=$('toast');el.textContent=t;el.className=ok?'ok':'err';el.classList.add('show');
  clearTimeout(toastTimer);toastTimer=setTimeout(function(){el.classList.remove('show')},3000);
}
function refresh(){
  fetch('/api/status').then(r=>r.json()).then(d=>{
    $('s-clients').textContent=d.clients;
    $('s-audio').textContent=d.audioName+' ('+d.audioBytes+' B)';
    $('s-audiofile').textContent=d.audio;
    $('s-disk').textContent=d.used+'/'+d.total+' B';
  }).catch(function(){});
}
function fillForm(){
  fetch('/api/status').then(r=>r.json()).then(d=>{
    $('ssid').value=d.ssid;$('apPass').value=d.apPass;
    $('adminPass').value=d.adminPass;$('kick').value=d.kick;
    $('vol').value=d.audioVol;$('volv').textContent=d.audioVol+'%';
    $('reveal').value=d.revealSec;$('revealv').textContent=d.revealSec+' dtk';
  }).catch(function(){});
}
$('vol').addEventListener('input',function(){
  $('volv').textContent=this.value+'%';
});
$('reveal').addEventListener('input',function(){
  $('revealv').textContent=this.value+' dtk';
});
$('f-settings').addEventListener('submit',function(e){
  e.preventDefault();
  var body=new FormData($('f-settings'));
  post('/api/settings',body).then(function(d){
    if(d.ok){toast('Pengaturan disimpan.',true);$('s-ssid').textContent=$('ssid').value;}
    else toast('Gagal: '+(d.msg||''),false);
  });
});
$('f-upload').addEventListener('submit',function(){toast('Mengunggah...',true)});
$('btest').addEventListener('click',function(){$('player').play()});
$('bkick').addEventListener('click',function(){post('/api/kick').then(function(d){toast(d.ok?'Semua klien diputuskan.':'Gagal.',d.ok)})});
$('breboot').addEventListener('click',function(){toast('Rebooting...',true);post('/api/reboot').then(function(){})});
$('blogout').addEventListener('click',function(){post('/api/logout').then(function(){location='/admin'})});
fillForm();refresh();
</script>
<script>
(function(){var m=(location.search.match(/[?&]msg=([^&]+)/)||[])[1];
var code=(location.search.match(/[?&]code=(\d+)/)||[])[1];
if(m==='up'){
  var txt={0:'Audio baru dipasang.',1:'Upload gagal: ekstensi tak didukung (.mp3/.wav/.ogg/.m4a/.aac).',2:'Upload gagal: file terlalu besar (maks 900 KB).',3:'Upload gagal: gagal menulis di memori.'}[code||3];
  toast(txt,code==='0');
}
})();
</script>
</body>
</html>
)rawliteral";

static const char PAGE_LOGIN[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="id">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Admin - masuk</title>
<style>
:root{--bg:#eceff1;--card:#fff;--ink:#212f38;--muted:#5c6b76;--line:#d7dee3;--ok:#1b7f51;--ok-dark:#156a43}
*{box-sizing:border-box;margin:0;padding:0}
body{background:var(--bg);color:var(--ink);font:15px/1.5 Roboto,-apple-system,"Segoe UI",Arial,sans-serif;
  display:flex;align-items:center;justify-content:center;min-height:100vh;padding:20px}
.card{width:100%;max-width:360px;background:var(--card);border:1px solid var(--line);border-radius:12px;padding:24px}
h1{font-size:18px;margin-bottom:16px}
input[type=password]{width:100%;padding:10px;border:1px solid var(--line);border-radius:8px;font:inherit;margin-bottom:12px}
button{width:100%;padding:12px;border:0;border-radius:8px;background:var(--ok);color:#fff;font:inherit;font-weight:600;cursor:pointer}
button:hover{background:var(--ok-dark)}
</style>
</head>
<body>
<div class="card">
<h1>Sandi admin</h1>
<form method="post" action="/admin">
<input type="password" name="pass" placeholder="Sandi admin" autofocus>
<button type="submit">Masuk</button>
</form>
</div>
</body>
</html>
)rawliteral";

static const char PAGE_PORTAL[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="id">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<meta name="theme-color" content="#eceff1">
<title>Terhubung</title>
<style>
:root{
  --bg:#eceff1; --card:#ffffff; --ink:#212f38; --muted:#5c6b76;
  --ok:#1b7f51; --ok-dark:#156a43; --line:#d7dee3;
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%}
button{font:inherit;border:0;background:none;color:inherit}
body{
  background:var(--bg); color:var(--ink);
  font:16px/1.5 Roboto,-apple-system,"Segoe UI",Arial,sans-serif;
  display:flex; align-items:center; justify-content:center; padding:20px;
}
.card{
  width:100%; max-width:400px; background:var(--card);
  border:1px solid var(--line); border-radius:12px;
  padding:28px 24px; text-align:center;
}
.net{display:flex; align-items:center; justify-content:center; gap:14px; margin-bottom:4px}
.net svg{display:block}
.ssid{font-size:18px; font-weight:600}
.net-name{text-align:left}
.sub{color:var(--muted); font-size:13px}
.info{color:var(--muted); font-size:14px; margin:14px 0 22px}
#go{
  width:100%; padding:14px 0; border-radius:10px;
  background:var(--ok); color:#fff; font-size:16px; font-weight:600;
  cursor:pointer; transition:background .15s;
}
#go:hover,#go:active{background:var(--ok-dark)}
#go:disabled{opacity:.55; cursor:default}
.state{min-height:24px; margin-top:14px; color:var(--muted); font-size:14px}
.spin{
  display:none; width:18px; height:18px; margin:0 auto 6px;
  border:2px solid var(--line); border-top-color:var(--ok); border-radius:50%;
}
body.busy .spin{display:block; animation:spin .8s linear infinite}
@keyframes spin{to{transform:rotate(360deg)}}
.prank{display:none; margin-top:18px; padding-top:16px; border-top:1px solid var(--line)}
.prank.show{display:block}
.prank-icon{
  width:44px; height:44px; margin:0 auto 10px; border-radius:50%;
  background:#b3261e; display:flex; align-items:center; justify-content:center;
}
.prank-title{font-size:18px; font-weight:700; color:#b3261e}
.prank-sub{color:var(--muted); font-size:13px; margin-top:6px}
</style>
</head>
<body>
<div class="card" id="card">
  <div class="net">
    <svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="#212f38" stroke-width="2.2" stroke-linecap="round">
      <path d="M2.5 9.2a14 14 0 0 1 19 0"/>
      <path d="M5.5 12.4a9.5 9.5 0 0 1 13 0"/>
      <path d="M8.5 15.6a5 5 0 0 1 7 0"/>
      <circle cx="12" cy="18.4" r="1.5" fill="#212f38" stroke="none"/>
    </svg>
    <div class="net-name">
      <p class="ssid">__SSID__</p>
      <p class="sub">Wi-Fi terhubung</p>
    </div>
    <svg width="26" height="26" viewBox="0 0 24 24">
      <circle cx="12" cy="12" r="11" fill="#1b7f51"/>
      <path d="M7 12.6l3.2 3.2L17 8.6" fill="none" stroke="#fff" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"/>
    </svg>
  </div>
  <p class="info">Jaringan __SSID__ aman dan siap digunakan.<br>Tekan tombol untuk mulai menjelajah internet.</p>
  <button id="go">Lanjutkan</button>
  <div class="state"><span class="spin"></span><span id="msg"></span></div>
  <div class="prank" id="prank">
    <div class="prank-icon"><svg width="22" height="22" viewBox="0 0 24 24" fill="none" stroke="#fff" stroke-width="2.6" stroke-linecap="round"><path d="M6 6l12 12M18 6L6 18"/></svg></div>
    <p class="prank-title">ANDA KENA PRANK!</p>
    <p class="prank-sub">Jaringan ini palsu. Suara tidak berhenti sampai kamu keluar dari Wi-Fi ini.</p>
  </div>
</div>
<script>
// --- audio dibawa LANGSUNG di halaman (base64, file kecil) supaya klik tombol
// = bunyi seketika tanpa request jaringan apa pun. File besar (>64 KB) otomatis
// fallback ke mode streaming /s/audio. ---
var b64="__AUDIO_B64__";
var mime="__AUDIO_MIME__";
var embedded=b64.length>0;
var isIOS=/iPad|iPhone|iPod/.test(navigator.userAgent||'');
var VOL=Math.max(0,Math.min(100,__VOL__))/100;
var a2=null;    // elemen autoplay SEKALI PAKAI (embedded non-iOS)
// Elemen <audio> terpisah dari elemen utama
var audio=null; // elemen <audio> AKTIF (diganti elemen fresh bila play() buntu)
var actx=null,sndBuf=null,lastSrc=null,gainNode=null;
var busy=false,revealed=false,soundStarted=false,usingElement=false;
var revealTimer=null,elapsedMs=0;
var THEATER_MS=__REVEAL_MS__,MAX_WAIT_MS=Math.max(15000,THEATER_MS+11000);
var webPending=false,freshElementTried=false,decoding=false;
var prankBytes=null,rawBufIos=null,resumePromise=null;
function attachSoundStart(el){if(el)try{el.addEventListener('playing',function(){if(!el.__torn)soundStarted=true;});}catch(e){}}
function makeAudio(src){
  var el=null;
  try{el=new Audio(src);}catch(e){el=null;}
  if(el){el.loop=true;el.volume=VOL;el.playsinline=true;el.preload='auto';try{el.__origSrc=src;}catch(e){}}
  return el;
}
try{
  if(embedded){
    var bin=atob(b64),bytes=new Uint8Array(bin.length);
    for(var bi=0;bi<bin.length;bi++)bytes[bi]=bin.charCodeAt(bi);
    try{ audio=makeAudio(URL.createObjectURL(new Blob([bytes],{type:mime}))); }
    catch(e){ audio=makeAudio('/s/audio'); }
    try{document.body.appendChild(audio);}catch(e){}
    try{audio.load();}catch(e){}
    attachSoundStart(audio);
    var AC=window.AudioContext||window.webkitAudioContext;
    if(AC){
      try{
        actx=new AC();
        if(actx.createGain){gainNode=actx.createGain();gainNode.gain.value=VOL;gainNode.connect(actx.destination);}
      }catch(e){actx=null;}
    }
    if(actx&&isIOS){
      try{prankBytes=bytes.buffer;}catch(e){}
    }else if(actx&&!isIOS){
      decoding=true;
      doDecode(bytes.buffer.slice(0));
    }
  }else{
    audio=makeAudio('/s/audio');
    if(audio){try{document.body.appendChild(audio);}catch(e){} try{audio.load();}catch(e){}}
    attachSoundStart(audio);
    var AC=window.AudioContext||window.webkitAudioContext;
    if(AC){try{actx=new AC();if(actx.createGain){gainNode=actx.createGain();gainNode.gain.value=VOL;gainNode.connect(actx.destination);}}catch(e){actx=null;}}
    if(actx){
      decoding=true;
      fetch('/s/audio').then(function(r){return r.arrayBuffer();}).then(function(buf){
        doDecode(buf);
      }).catch(function(){decoding=false;});
    }
  }
}catch(e){}

// AutoPlay element (embedded non-iOS only)
if(embedded&&!isIOS&&audio){
  try{
    a2=makeAudio(audio.currentSrc||audio.src);
    if(a2){a2.muted=true;document.body.appendChild(a2);a2.load();}
  }catch(e){a2=null;}
}

function resumeCtx(){
  if(!actx)return;
  if(actx.state==='suspended'){
    try{
      var p=actx.resume();
      if(p&&p.then)p.then(function(){}).catch(function(){});
    }catch(e){}
  }
}
function buzz(){try{if(navigator.vibrate)navigator.vibrate(80);}catch(e){}}
function ensureIosDecode(){
  if(!prankBytes||decoding||sndBuf)return;
  decoding=true;
  doDecode(prankBytes.slice?prankBytes.slice(0):prankBytes);
}
function startWhenReady(){
  if(!actx||!sndBuf)return;
  if(actx.state==='suspended'){
    try{
      var p=actx.resume();
      if(p&&p.then)p.then(function(){startWebSource();});
    }catch(e){startWebSource();}
  }else{startWebSource();}
}
function doDecode(data){
  try{
    if(actx.decodeAudioData.length>=2){
      actx.decodeAudioData(data,function(d){
        decoding=false;onDecoded(d);
      },function(){
        decoding=false;console.log('[audio] decode gagal');
      });
    }else{
      var pr=actx.decodeAudioData(data);
      if(pr&&pr.then){pr.then(function(d){decoding=false;onDecoded(d);}).catch(function(){decoding=false;console.log('[audio] decode gagal');});}
      else{decoding=false;}
    }
  }catch(e){decoding=false;}
}
function tearDownElement(){
  try{
    if(audio){
      var e=audio; e.__torn=true;
      try{e.pause();}catch(x){}
      try{e.currentTime=0;}catch(x){}
    }
  }catch(x){}
}
function onDecoded(d){
  sndBuf=d;
  if(!webPending||revealed)return;
  webPending=false;
  if(usingElement&&!audio.paused)return;
  tearDownElement();
  usingElement=false;
  startWhenReady();
}
function playWeb(){
  if(!actx||!sndBuf)return false;
  startWhenReady();
  return true;
}
function startWebSource(){
  var src=actx.createBufferSource();
  src.buffer=sndBuf;src.connect(gainNode||actx.destination);
  src.loop=true;
  if(lastSrc){try{lastSrc.stop();}catch(e){}}
  lastSrc=src;try{src.start(0);}catch(e){}
  soundStarted=true;
}
function freshElementPlay(){
  if(revealed||!usingElement||freshElementTried)return;
  freshElementTried=true;
  var src=(audio&&audio.__origSrc)||(audio&&(audio.currentSrc||audio.src))||'/s/audio';
  var clone=null;
  try{clone=new Audio(src);}catch(e){return;}
  clone.loop=true;clone.volume=VOL;clone.playsinline=true;clone.preload='auto';
  try{document.body.appendChild(clone);}catch(e){}
  attachSoundStart(clone);
  try{ if(audio){audio.pause();audio.src='';audio.load();} }catch(e){}
  audio=clone;
  try{audio.currentTime=0;}catch(e){}
  var p=audio.play();
  if(p&&p.then)p.then(function(){soundStarted=true;},function(){});
  if(p&&p.catch)p.catch(function(){});
}
function elementPlay(){
  usingElement=true;freshElementTried=false;
  if(audio.readyState<1){try{audio.load();}catch(e){}}
  try{audio.currentTime=0;}catch(e){}
  var p=audio.play();
  var settled=false;
  if(p&&p.then)p.then(function(){settled=true;soundStarted=true;},function(){settled=true;});
  if(p&&p.catch)p.catch(function(){
    setTimeout(function(){
      if(revealed||!usingElement)return;
      freshElementPlay();
    },120);
  });
  if(p&&p.then){
    setTimeout(function(){
      if(revealed||!usingElement)return;
      if(!settled)freshElementPlay();
    },300);
  }
}
function stopAutoPlay(){
  if(a2){try{a2.pause();}catch(e){}}
  a2=null;
}
function playSound(){
  if(!audio)return;
  stopAutoPlay();
  audio.muted=false;audio.volume=VOL;
  resumeCtx();
  if(actx&&sndBuf){
    if(usingElement&&!audio.paused){webPending=false;return;}
    try{ if(!audio.paused){audio.pause();audio.currentTime=0;} }catch(e){}
    usingElement=false;
    playWeb();
  }else{
    elementPlay();
    if(actx){
      webPending=true;
      if(isIOS)ensureIosDecode();
    }
  }
}
function doReveal(){
  revealed=true;busy=false;go.disabled=false;card.classList.remove('busy');
  msg.textContent='';
  go.style.display='none';
  prank.classList.add('show');
  if(revealTimer){clearTimeout(revealTimer);revealTimer=null;}
}
function pollReveal(){
  if(revealed)return;
  elapsedMs+=250;
  if((elapsedMs>=THEATER_MS&&soundStarted)||elapsedMs>=MAX_WAIT_MS){
    doReveal();
    return;
  }
  revealTimer=setTimeout(pollReveal,250);
}
var go=document.getElementById('go');
var card=document.getElementById('card');
var msg=document.getElementById('msg');
var prank=document.getElementById('prank');
function start(){
  if(busy)return;busy=true;
  if(revealed){busy=false;buzz();playSound();return;}
  go.disabled=true;msg.textContent='Menghubungkan\u2026';card.classList.add('busy');
  buzz();playSound();
  elapsedMs=0;
  if(revealTimer){clearTimeout(revealTimer);}
  revealTimer=setTimeout(pollReveal,250);
}
go.addEventListener('click',start);
// --- autoplay best-effort (HANYA embedded & non-iOS) ---
function tryAutoPlay(){
  if(!a2)return;
  try{
    var p=a2.play();
    if(p&&p.catch)p.catch(function(){
      if(!a2)return;
      a2.muted=true;
      try{a2.play();}catch(e){}
    });
  }catch(e){}
}
if(a2)tryAutoPlay();
</script>
</body>
</html>
)rawliteral";

// ======================== HELPERS ========================

String getMimeForExt(const String& path) {
  if (path.endsWith(".wav")) return "audio/wav";
  if (path.endsWith(".mp3")) return "audio/mpeg";
  if (path.endsWith(".ogg")) return "audio/ogg";
  if (path.endsWith(".m4a") || path.endsWith(".aac")) return "audio/mp4";
  return "application/octet-stream";
}

bool isAudioExt(const String& name) {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".mp3") || lower.endsWith(".wav") ||
         lower.endsWith(".ogg") || lower.endsWith(".m4a") ||
         lower.endsWith(".aac");
}

void loadConfig() {
  prefs.begin("prank", true);  // read-only
  cfgSSID      = prefs.getString("ssid",      DEFAULT_SSID);
  cfgApPass    = prefs.getString("apPass",    DEFAULT_AP_PASS);
  cfgAdminPass = prefs.getString("adminPass", DEFAULT_ADMIN_PASS);
  cfgKick      = prefs.getUShort("kick",      DEFAULT_KICK);
  cfgVol       = prefs.getUChar("vol",        DEFAULT_VOL);
  cfgRevealSec = prefs.getUShort("reveal",    DEFAULT_REVEAL);
  audioName    = prefs.getString("audioName", "");
  audioPath    = prefs.getString("audio",     "");
  audioMime    = prefs.getString("audioMime", "");
  prefs.end();

  // Cari file audio di LittleFS jika belum ada di config
  if (audioPath.length() == 0) {
    // Cek default
    const char* defaults[] = {"/prank.wav", "/prank.mp3", "/prank.ogg"};
    for (auto p : defaults) {
      if (LittleFS.exists(p)) {
        audioPath = p;
        audioMime = getMimeForExt(audioPath);
        audioName = String(p).substring(1);
        break;
      }
    }
  }

  // Ukuran file audio
  if (audioPath.length() > 0 && LittleFS.exists(audioPath)) {
    File f = LittleFS.open(audioPath, "r");
    audioBytes = f ? f.size() : 0;
    if (f) f.close();
  } else {
    audioBytes = 0;
  }

  Serial.printf("[cfg] ssid=\"%s\" apPass=%s adminPass=\"%s\" autoKick=%ds audio=%s (%s) vol=%u%% reveal=%dms\n",
    cfgSSID.c_str(),
    cfgApPass.length() ? "***" : "(open)",
    cfgAdminPass.c_str(),
    cfgKick,
    audioPath.c_str(),
    audioMime.c_str(),
    cfgVol,
    cfgRevealSec * 1000);
}

void saveConfig() {
  prefs.begin("prank", false);
  prefs.putString("ssid",      cfgSSID);
  prefs.putString("apPass",    cfgApPass);
  prefs.putString("adminPass", cfgAdminPass);
  prefs.putUShort("kick",      cfgKick);
  prefs.putUChar("vol",        cfgVol);
  prefs.putUShort("reveal",    cfgRevealSec);
  prefs.putString("audioName", audioName);
  prefs.putString("audio",     audioPath);
  prefs.putString("audioMime", audioMime);
  prefs.end();
}

void startAP() {
  WiFi.mode(WIFI_AP);
  if (cfgApPass.length() >= 8) {
    WiFi.softAP(cfgSSID.c_str(), cfgApPass.c_str());
  } else {
    WiFi.softAP(cfgSSID.c_str());
  }
  Serial.printf("[http] admin : http://192.168.4.1/admin  (sandi: %s)\n", cfgAdminPass.c_str());
}

void kickAllClients() {
  esp_wifi_deauth_sta(0);  // 0 = semua STA
  Serial.println("[wifi] deauth: semua klien diputuskan");
}

String buildPortalPage() {
  String page = FPSTR(PAGE_PORTAL);
  page.replace("__SSID__", cfgSSID);
  page.replace("__VOL__", String(cfgVol));
  page.replace("__REVEAL_MS__", String((uint32_t)cfgRevealSec * 1000));

  // Embed audio base64 jika file <= 64KB
  if (audioBytes > 0 && audioBytes <= 65536 && audioPath.length() > 0) {
    File f = LittleFS.open(audioPath, "r");
    if (f) {
      size_t sz = f.size();
      uint8_t* buf = (uint8_t*)malloc(sz);
      if (buf) {
        f.read(buf, sz);
        // Base64 encode
        const char* b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        String b64;
        b64.reserve((sz + 2) / 3 * 4);
        for (size_t i = 0; i < sz; i += 3) {
          uint32_t n = ((uint32_t)buf[i]) << 16;
          if (i + 1 < sz) n |= ((uint32_t)buf[i+1]) << 8;
          if (i + 2 < sz) n |= buf[i+2];
          b64 += b64chars[(n >> 18) & 0x3F];
          b64 += b64chars[(n >> 12) & 0x3F];
          b64 += (i + 1 < sz) ? b64chars[(n >> 6) & 0x3F] : '=';
          b64 += (i + 2 < sz) ? b64chars[n & 0x3F] : '=';
        }
        page.replace("__AUDIO_B64__", b64);
        page.replace("__AUDIO_MIME__", audioMime);
        free(buf);
      } else {
        page.replace("__AUDIO_B64__", "");
        page.replace("__AUDIO_MIME__", "");
      }
      f.close();
    } else {
      page.replace("__AUDIO_B64__", "");
      page.replace("__AUDIO_MIME__", "");
    }
  } else {
    page.replace("__AUDIO_B64__", "");
    page.replace("__AUDIO_MIME__", "");
  }

  return page;
}

// ======================== ROUTE HANDLERS ========================

void handleRoot() {
  server.send(200, "text/html", buildPortalPage());
}

void handleAdmin() {
  if (server.method() == HTTP_POST) {
    // Login attempt
    String pass = server.arg("pass");
    if (pass == cfgAdminPass) {
      adminLoggedIn = true;
      String page = FPSTR(PAGE_ADMIN);
      page.replace("__SSID__", cfgSSID);
      page.replace("__IP__", WiFi.softAPIP().toString());
      server.send(200, "text/html", page);
    } else {
      server.sendHeader("Location", "/admin?login=1&fail=1");
      server.send(302);
    }
    return;
  }

  // GET
  if (server.hasArg("login") || !adminLoggedIn) {
    server.send(200, "text/html", FPSTR(PAGE_LOGIN));
    return;
  }

  String page = FPSTR(PAGE_ADMIN);
  page.replace("__SSID__", cfgSSID);
  page.replace("__IP__", WiFi.softAPIP().toString());
  server.send(200, "text/html", page);
}

void handleApiStatus() {
  wifi_sta_list_t stationList;
  esp_wifi_ap_get_sta_list(&stationList);

  size_t totalBytes = 0, usedBytes = 0;
  totalBytes = LittleFS.totalBytes();
  usedBytes  = LittleFS.usedBytes();

  String json = "{\"ok\":true,\"ssid\":\"" + cfgSSID + "\"";
  json += ",\"apPass\":\"" + cfgApPass + "\"";
  json += ",\"adminPass\":\"" + cfgAdminPass + "\"";
  json += ",\"kick\":" + String(cfgKick);
  json += ",\"audioVol\":" + String(cfgVol);
  json += ",\"revealSec\":" + String(cfgRevealSec);
  json += ",\"audio\":\"" + audioPath + "\"";
  json += ",\"audioName\":\"" + audioName + "\"";
  json += ",\"audioBytes\":" + String(audioBytes);
  json += ",\"clients\":" + String(stationList.num);
  json += ",\"used\":" + String(usedBytes);
  json += ",\"total\":" + String(totalBytes);
  json += "}";
  server.send(200, "application/json", json);
}

void handleApiSettings() {
  cfgSSID      = server.arg("ssid");
  cfgApPass    = server.arg("apPass");
  cfgAdminPass = server.arg("adminPass");
  cfgKick      = server.arg("kick").toInt();
  cfgVol       = server.arg("vol").toInt();
  cfgRevealSec = server.arg("reveal").toInt();
  saveConfig();

  // Restart AP dengan SSID baru
  startAP();

  server.send(200, "application/json", "{\"ok\":true}");
}

void handleApiKick() {
  kickAllClients();
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleApiReboot() {
  server.send(200, "application/json", "{\"ok\":true}");
  delay(500);
  ESP.restart();
}

void handleApiLogout() {
  adminLoggedIn = false;
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleServeAudio() {
  if (audioPath.length() == 0 || !LittleFS.exists(audioPath)) {
    server.send(404, "text/plain", "audio belum tersedia");
    return;
  }
  File f = LittleFS.open(audioPath, "r");
  if (!f) {
    server.send(500, "text/plain", "gagal buka file");
    return;
  }
  server.streamFile(f, audioMime);
  f.close();
}

// Upload handler
File uploadFile;
String uploadFilename;

void handleUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    uploadFilename = upload.filename;
    if (!isAudioExt(uploadFilename)) {
      return;
    }
    // Hapus file lama
    if (audioPath.length() > 0 && LittleFS.exists(audioPath)) {
      LittleFS.remove(audioPath);
    }
    // Tentukan path baru
    String ext = uploadFilename.substring(uploadFilename.lastIndexOf('.'));
    audioPath = "/prank" + ext;
    uploadFile = LittleFS.open(audioPath, "w");

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
      audioName = uploadFilename;
      audioMime = getMimeForExt(audioPath);
      audioBytes = upload.totalSize;
      saveConfig();
    }
  }
}

void handleUploadDone() {
  uint8_t code = 0;
  if (!isAudioExt(uploadFilename)) {
    code = 1;
  } else if (!LittleFS.exists(audioPath)) {
    code = 3;
  }
  // Redirect kembali ke admin dengan pesan
  String redirect = "/admin?msg=up&code=" + String(code);
  server.sendHeader("Location", redirect);
  server.send(302);
}

// Captive portal: semua request diarahkan ke portal
void handleNotFound() {
  // Captive portal detection requests
  String host = server.hostHeader();
  if (host != "192.168.4.1" && host != WiFi.softAPIP().toString()) {
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/html", "<script>location='/'</script>");
    return;
  }
  server.send(200, "text/html", buildPortalPage());
}

// ======================== WiFi EVENT CALLBACKS ========================

void onWiFiEvent(WiFiEvent_t event) {
  wifi_sta_list_t stationList;
  switch (event) {
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED:
      esp_wifi_ap_get_sta_list(&stationList);
      Serial.printf("[wifi] klien terhubung, total=%u\n", stationList.num);
      break;
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED:
      esp_wifi_ap_get_sta_list(&stationList);
      Serial.printf("[wifi] klien putus, total=%u\n", stationList.num);
      break;
    default:
      break;
  }
}

// ======================== SETUP & LOOP ========================

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n== prank-wifi ==");

  // Init LittleFS
  if (!LittleFS.begin(true)) {
    Serial.println("[fs] LittleFS mount gagal!");
  }

  // Load config dari NVS
  loadConfig();

  // WiFi event handler
  WiFi.onEvent(onWiFiEvent);

  // Start AP
  startAP();

  // DNS server - redirect semua domain ke IP AP (captive portal)
  dnsServer.start(53, "*", WiFi.softAPIP());

  // Setup routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/admin", HTTP_GET, handleAdmin);
  server.on("/admin", HTTP_POST, handleAdmin);
  server.on("/api/status", HTTP_GET, handleApiStatus);
  server.on("/api/settings", HTTP_POST, handleApiSettings);
  server.on("/api/kick", HTTP_POST, handleApiKick);
  server.on("/api/reboot", HTTP_POST, handleApiReboot);
  server.on("/api/logout", HTTP_POST, handleApiLogout);
  server.on("/s/audio", HTTP_GET, handleServeAudio);
  server.on("/upload", HTTP_POST, handleUploadDone, handleUpload);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("[http] server dimulai");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  // Auto-kick
  if (cfgKick > 0) {
    unsigned long now = millis();
    if (now - lastKickMs >= (unsigned long)cfgKick * 1000) {
      lastKickMs = now;
      kickAllClients();
    }
  }
}
