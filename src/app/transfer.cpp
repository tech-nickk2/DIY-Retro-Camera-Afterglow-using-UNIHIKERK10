#include "transfer.h"
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "gallery.h"
#include "../film/film.h"
#include "../hal/clock.h"
#include "../ui/cine.h"

namespace transfer {

static WebServer* s_server = nullptr;
static Status s_status = {};
static bool s_running = false;
static uint8_t* s_buf = nullptr;
static constexpr size_t BUF = 8192;
static gallery::Item* s_items = nullptr;
static constexpr int MAX_ITEMS = 999;

// The gallery page. Dark, film-strip feel; works offline (no external assets).
static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Afterglow</title>
<style>
:root{--bg:#141110;--card:#1f1a17;--ink:#f3ead8;--dim:#9b9184;--amber:#f5b41c}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.4 system-ui,-apple-system,sans-serif}
header{padding:18px 16px 10px}h1{margin:0;font-size:24px;letter-spacing:.2em}
.stripes{display:flex;height:5px;width:90px;margin:6px 0 10px}.stripes i{flex:1}
#status{color:var(--dim);font-size:13px}
main{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:12px;padding:8px 16px 32px}
.card{background:var(--card);border-radius:6px;overflow:hidden;border:1px solid #2b2420}
.card img{display:block;width:100%;aspect-ratio:4/3;object-fit:cover;background:#000;image-rendering:pixelated}
.meta{padding:8px 10px;font-size:12px;color:var(--dim)}.meta b{color:var(--ink);font-weight:600}
.btns{display:flex;gap:6px;padding:0 10px 10px}
.btns a{flex:1;text-align:center;padding:7px 0;border-radius:4px;text-decoration:none;font-size:13px;font-weight:600}
.view{background:#2b2420;color:var(--ink)}.save{background:var(--amber);color:#1a1410}
.tag{color:var(--amber)}
</style></head><body>
<header><div class="stripes"><i style="background:#d8352a"></i><i style="background:#ee7a1c"></i><i style="background:#f5b41c"></i><i style="background:#5aa36a"></i><i style="background:#2f77b8"></i></div>
<h1>AFTERGLOW</h1><div id="status">Setting the camera's clock…</div></header>
<main id="grid"></main>
<script>
const d=new Date();
fetch('/time?t='+Math.floor(d.getTime()/1000)+'&o='+(-d.getTimezoneOffset()))
 .then(r=>r.ok?'Clock set from this phone: '+d.toLocaleString():'Clock not set')
 .catch(()=>'Clock not set').then(t=>document.getElementById('status').textContent=t);
fetch('/list').then(r=>r.json()).then(items=>{
 const g=document.getElementById('grid');
 if(!items.length){g.innerHTML='<p class="meta">No photos yet. Go shoot something.</p>';return}
 for(const it of items){
  const c=document.createElement('div');c.className='card';
  const when=new Date(it.t*1000).toLocaleDateString(undefined,{timeZone:'UTC',month:'short',day:'numeric'});
  const what=it.k==='clip'?'<span class="tag">&#9654; '+it.len+' s</span> '+it.f:it.f+' &middot; #'+it.fr;
  c.innerHTML='<img loading="lazy" src="/thumb?n='+it.num+'" alt=""><div class="meta"><b>'+it.name+'</b><br>'+what+' &middot; '+when+
   '</div><div class="btns"><a class="view" href="/file?n='+it.name+'">View</a><a class="save" href="/file?n='+it.name+'&dl=1" download="'+it.name+'">Save</a></div>';
  g.appendChild(c);
 }
});
</script></body></html>)HTML";

static void sendFile() {
  String n = s_server->arg("n");
  // Only plain AFT_NNNN names from DCIM: no paths.
  if (n.length() < 5 || n.indexOf('/') >= 0 || n.indexOf("..") >= 0) { s_server->send(400, "text/plain", "bad name"); return; }
  char path[64];
  snprintf(path, sizeof(path), "/sd/DCIM/%s", n.c_str());
  FILE* f = fopen(path, "rb");
  if (!f) { s_server->send(404, "text/plain", "not found"); return; }
  struct stat st;
  stat(path, &st);
  const bool avi = n.endsWith(".AVI");
  if (s_server->hasArg("dl")) s_server->sendHeader("Content-Disposition", String("attachment; filename=\"") + n + "\"");
  s_server->setContentLength(st.st_size);
  s_server->send(200, avi ? "video/x-msvideo" : "image/jpeg", "");
  size_t r;
  while ((r = fread(s_buf, 1, BUF, f)) > 0) {
    s_server->sendContent((const char*)s_buf, r);
    s_status.bytes += r;
  }
  fclose(f);
  s_status.files++;
}

// Thumbnails as small 16-bit BMPs straight from the album's raw copies.
static void sendThumb() {
  const int num = s_server->arg("n").toInt();
  static uint16_t px[gallery::THUMB_W * gallery::THUMB_H];
  if (!gallery::loadThumb((uint16_t)num, px)) { s_server->send(404, "text/plain", "no thumb"); return; }
  const int w = gallery::THUMB_W, h = gallery::THUMB_H, rowBytes = w * 2;  // 200, already a multiple of 4
  const uint32_t dataSize = rowBytes * h, fileSize = 66 + dataSize;
  uint8_t hdr[66] = {'B', 'M'};
  auto u32 = [&](int o, uint32_t v) { memcpy(hdr + o, &v, 4); };
  auto u16 = [&](int o, uint16_t v) { memcpy(hdr + o, &v, 2); };
  u32(2, fileSize); u32(10, 66); u32(14, 40); u32(18, w); u32(22, h); u16(26, 1); u16(28, 16);
  u32(30, 3);  // BI_BITFIELDS
  u32(34, dataSize); u32(54, 0xF800); u32(58, 0x07E0); u32(62, 0x001F);
  s_server->setContentLength(fileSize);
  s_server->sendHeader("Cache-Control", "max-age=86400");
  s_server->send(200, "image/bmp", "");
  s_server->sendContent((const char*)hdr, 66);
  uint8_t row[gallery::THUMB_W * 2];
  for (int y = h - 1; y >= 0; y--) {  // BMP rows go bottom-up
    for (int x = 0; x < w; x++) {
      const uint16_t v = px[y * w + x];  // panel order (big-endian): BMP wants little-endian
      row[x * 2] = v >> 8;
      row[x * 2 + 1] = v & 0xFF;
    }
    s_server->sendContent((const char*)row, rowBytes);
  }
}

static void sendList() {
  const int n = gallery::list(s_items, MAX_ITEMS);
  String out = "[";
  for (int i = 0; i < n; i++) {
    const gallery::Item& it = s_items[i];
    char name[24], line[200];
    snprintf(name, sizeof(name), "AFT_%04u.%s", it.num, it.kind == gallery::CLIP ? "AVI" : "JPG");
    const char* film = it.kind == gallery::CLIP ? ui::cineLookName(it.stock)
                                                : (it.stock < film::STOCK_COUNT ? film::STOCKS[it.stock].name : "FILM");
    snprintf(line, sizeof(line), "%s{\"name\":\"%s\",\"num\":%u,\"k\":\"%s\",\"f\":\"%s\",\"fr\":%u,\"len\":\"%u.%u\",\"t\":%lu}",
             i ? "," : "", name, it.num, it.kind == gallery::CLIP ? "clip" : it.kind == gallery::INSTANT ? "instant" : "photo",
             film, it.frame, it.tenths / 10, it.tenths % 10, (unsigned long)it.time);
    out += line;
  }
  out += "]";
  s_server->send(200, "application/json", out);
}

static void setTime() {
  const long t = s_server->arg("t").toInt();
  const long offsetMin = s_server->arg("o").toInt();
  if (t < 1700000000L || offsetMin < -14 * 60 || offsetMin > 14 * 60) { s_server->send(400, "text/plain", "bad time"); return; }
  // The camera keeps local time with no time zone, so store the phone's local time.
  clock_::set((time_t)(t + offsetMin * 60));
  s_status.timeSynced = true;
  s_server->send(200, "text/plain", "ok");
}

bool start(const char* password) {
  if (s_running) return true;
  if (!s_buf) s_buf = (uint8_t*)heap_caps_malloc(BUF, MALLOC_CAP_SPIRAM);
  if (!s_items) s_items = (gallery::Item*)heap_caps_malloc(MAX_ITEMS * sizeof(gallery::Item), MALLOC_CAP_SPIRAM);
  if (!s_buf || !s_items) return false;
  WiFi.mode(WIFI_AP);
  uint8_t mac[6];
  WiFi.softAPmacAddress(mac);
  snprintf(s_status.ssid, sizeof(s_status.ssid), "AFTERGLOW-%02X%02X", mac[4], mac[5]);
  snprintf(s_status.pass, sizeof(s_status.pass), "%s", password);
  if (!WiFi.softAP(s_status.ssid, s_status.pass)) return false;
  snprintf(s_status.url, sizeof(s_status.url), "%s", WiFi.softAPIP().toString().c_str());
  s_status.timeSynced = false;
  s_status.files = s_status.bytes = 0;
  s_server = new WebServer(80);
  s_server->on("/", [] { s_server->send_P(200, "text/html", PAGE); });
  s_server->on("/list", sendList);
  s_server->on("/thumb", sendThumb);
  s_server->on("/file", sendFile);
  s_server->on("/time", setTime);
  s_server->onNotFound([] { s_server->send(404, "text/plain", "not found"); });
  s_server->begin();
  s_running = true;
  return true;
}

void stop() {
  if (!s_running) return;
  s_server->stop();
  delete s_server;
  s_server = nullptr;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  s_running = false;
}

bool running() { return s_running; }

void loop() {
  if (!s_running) return;
  s_server->handleClient();
  s_status.stations = WiFi.softAPgetStationNum();
}

const Status& status() { return s_status; }

}  // namespace transfer
