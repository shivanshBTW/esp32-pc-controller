#include "web_ui.h"
#include "configuration.h"

#include "esp_http_server.h"
#include "esp_log.h"

static const char *TAG = "webui";

/* Compact multi-page shell: /, /settings, /ota */
static const char *PAGE_SHELL_HEAD =
    "<!DOCTYPE html><html><head><meta charset=utf-8>"
    "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
    "<title>WakeType</title>"
    "<style>"
    "body{font-family:system-ui,sans-serif;margin:0;background:#0f1419;color:#e7ecf1}"
    "header{padding:1rem 1.25rem;border-bottom:1px solid #243041;display:flex;gap:1rem;flex-wrap:wrap;align-items:center}"
    "header strong{font-size:1.15rem;letter-spacing:.02em}"
    "nav a{color:#8ec7ff;margin-right:.85rem;text-decoration:none}"
    "main{padding:1.25rem;max-width:720px}"
    "section{margin:0 0 1.25rem;padding:1rem;background:#182230;border-radius:8px}"
    "h2{margin:0 0 .75rem;font-size:1rem}"
    "label{display:block;margin:.5rem 0 .2rem;font-size:.85rem;color:#a9b6c4}"
    "input,select,button,textarea{width:100%;box-sizing:border-box;padding:.55rem .65rem;border-radius:6px;border:1px solid #334155;background:#0f1419;color:#e7ecf1}"
    "button{cursor:pointer;background:#2563eb;border:none;margin-top:.5rem;font-weight:600}"
    "button.danger{background:#b91c1c}"
    "button.secondary{background:#334155}"
    ".row{display:flex;gap:.5rem;flex-wrap:wrap}"
    ".row button{width:auto;flex:1}"
    "pre{white-space:pre-wrap;font-size:.8rem;background:#0b1016;padding:.75rem;border-radius:6px;overflow:auto}"
    ".ok{color:#86efac}.warn{color:#fcd34d}.err{color:#fca5a5}"
    "small{color:#8b9aab}"
    "</style></head><body>"
    "<header><strong>WakeType</strong><nav>"
    "<a href=/>Home</a><a href=/settings>Settings</a><a href=/ota>OTA</a>"
    "</nav></header><main>";

static const char *PAGE_SHELL_FOOT =
    "</main><script>"
    "const TKEY='waketype_token';"
    "function tok(){return localStorage.getItem(TKEY)||''}"
    "function setTok(t){localStorage.setItem(TKEY,t||'');const e=document.getElementById('token');if(e)e.value=t||''}"
    "async function api(path,opt={}){"
    "  const h=Object.assign({'Content-Type':'application/json'},opt.headers||{});"
    "  if(tok()) h['Authorization']='Bearer '+tok();"
    "  const r=await fetch(path,Object.assign({},opt,{headers:h}));"
    "  const t=await r.text(); let j=null; try{j=JSON.parse(t)}catch(e){}"
    "  if(!r.ok) throw new Error((j&&j.error)||t||r.status); return j;"
    "}"
    "function msg(id,text,cls){const el=document.getElementById(id); if(!el)return; el.className=cls||''; el.textContent=text}"
    "</script></body></html>";

static esp_err_t send_html(httpd_req_t *req, const char *body)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PAGE_SHELL_HEAD);
    httpd_resp_sendstr_chunk(req, body);
    httpd_resp_sendstr_chunk(req, PAGE_SHELL_FOOT);
    return httpd_resp_sendstr_chunk(req, NULL);
}

static esp_err_t handle_home(httpd_req_t *req)
{
    return send_html(req,
        "<section><h2>Status</h2>"
        "<label>API token (saved in this browser)</label>"
        "<input id=token placeholder=\"paste token\">"
        "<div class=row>"
        "<button class=secondary onclick=\"setTok(document.getElementById('token').value);msg('s','Token saved','ok')\">Save token</button>"
        "<button onclick=\"refresh()\">Refresh</button>"
        "</div>"
        "<pre id=status>Loading…</pre><p id=s></p></section>"
        "<section><h2>PC controls (dry-test OK — no motherboard needed)</h2>"
        "<div class=row>"
        "<button onclick=\"pc('/api/v1/pc/power')\">Power</button>"
        "<button class=danger onclick=\"if(confirm('Long hold?'))pc('/api/v1/pc/power/hold',{duration_ms:5000})\">Long hold</button>"
        "<button class=danger onclick=\"if(confirm('Reset?'))pc('/api/v1/pc/reset')\">Reset</button>"
        "<button class=secondary onclick=\"pc('/api/v1/pc/release')\">Release</button>"
        "</div><p id=pcmsg></p></section>"
        "<section id=wifiSec><h2>Wi‑Fi setup</h2>"
        "<button class=secondary onclick=\"scan()\">Scan networks</button>"
        "<label>SSID</label><select id=ssid></select>"
        "<label>Or type SSID</label><input id=ssidManual>"
        "<label>Password</label><input id=pass type=password>"
        "<button onclick=\"saveWifi()\">Save &amp; connect</button>"
        "<p id=wmsg></p><small>On first boot join hotspot <b>WakeType-Setup</b>, then open this page.</small>"
        "</section>"
        "<script>"
        "document.getElementById('token').value=tok();"
        "async function refresh(){try{const j=await api('/api/v1/status');"
        "document.getElementById('status').textContent=JSON.stringify(j,null,2);"
        "if(j.api_token_hint) msg('s','Token hint shown in status JSON','warn');"
        "msg('s','OK','ok')}catch(e){msg('s',e.message,'err')}}"
        "async function pc(path,body){try{const j=await api(path,{method:'POST',body:body?JSON.stringify(body):'{}'});"
        "msg('pcmsg',JSON.stringify(j),'ok'); refresh()}catch(e){msg('pcmsg',e.message,'err')}}"
        "async function scan(){try{const j=await api('/api/v1/wifi/scan'); const sel=document.getElementById('ssid');"
        "sel.innerHTML=''; (j.aps||[]).forEach(a=>{const o=document.createElement('option'); o.value=a.ssid; o.textContent=a.ssid+' ('+a.rssi+')'; sel.appendChild(o)});"
        "msg('wmsg','Found '+(j.aps||[]).length+' networks','ok')}catch(e){msg('wmsg',e.message,'err')}}"
        "async function saveWifi(){const ssid=document.getElementById('ssidManual').value||document.getElementById('ssid').value;"
        "try{const j=await api('/api/v1/wifi/connect',{method:'POST',body:JSON.stringify({ssid,password:document.getElementById('pass').value})});"
        "msg('wmsg','Saved — device will join '+ssid,'ok'); if(j.api_token) setTok(j.api_token)}catch(e){msg('wmsg',e.message,'err')}}"
        "refresh();"
        "</script>");
}

static esp_err_t handle_settings(httpd_req_t *req)
{
    return send_html(req,
        "<section><h2>Settings</h2>"
        "<label>API token</label><input id=token>"
        "<button class=secondary onclick=\"setTok(document.getElementById('token').value)\">Save token locally</button>"
        "<label>Hostname</label><input id=hostname>"
        "<label><input type=checkbox id=static style=\"width:auto\"> Use static IP</label>"
        "<label>IP</label><input id=ip>"
        "<label>Gateway</label><input id=gw>"
        "<label>Netmask</label><input id=mask>"
        "<label>DNS1</label><input id=dns1>"
        "<label>DNS2</label><input id=dns2>"
        "<label>Power press ms</label><input id=pwr type=number>"
        "<label>Reset press ms</label><input id=rst type=number>"
        "<label>Default long hold ms</label><input id=long type=number>"
        "<label><input type=checkbox id=lock style=\"width:auto\"> Local lock (block remote control)</label>"
        "<label><input type=checkbox id=lockapi style=\"width:auto\"> Lock also blocks API (except release)</label>"
        "<button onclick=\"save()\">Save settings</button>"
        "<p id=m></p></section>"
        "<section><h2>Config backup</h2>"
        "<div class=row>"
        "<button class=secondary onclick=\"backup()\">Download JSON</button>"
        "</div>"
        "<label>Restore JSON</label><textarea id=cfg rows=8></textarea>"
        "<label><input type=checkbox id=incSecrets style=\"width:auto\"> Include secrets on restore</label>"
        "<label><input type=checkbox id=clearStatic checked style=\"width:auto\"> Clear static IP on import</label>"
        "<button onclick=\"restore()\">Restore</button>"
        "<p id=cmsg></p></section>"
        "<script>"
        "document.getElementById('token').value=tok();"
        "async function load(){try{const j=await api('/api/v1/settings');"
        "hostname.value=j.hostname||''; static.checked=!!j.wifi_use_static;"
        "ip.value=j.wifi_ip||''; gw.value=j.wifi_gateway||''; mask.value=j.wifi_netmask||'';"
        "dns1.value=j.wifi_dns1||''; dns2.value=j.wifi_dns2||'';"
        "pwr.value=j.power_press_ms; rst.value=j.reset_press_ms; long.value=j.default_long_press_ms;"
        "lock.checked=!!j.local_lock; lockapi.checked=!!j.local_lock_blocks_api;"
        "msg('m','Loaded','ok')}catch(e){msg('m',e.message,'err')}}"
        "async function save(){try{await api('/api/v1/settings',{method:'POST',body:JSON.stringify({"
        "hostname:hostname.value,wifi_use_static:static.checked,wifi_ip:ip.value,wifi_gateway:gw.value,"
        "wifi_netmask:mask.value,wifi_dns1:dns1.value,wifi_dns2:dns2.value,"
        "power_press_ms:+pwr.value,reset_press_ms:+rst.value,default_long_press_ms:+long.value,"
        "local_lock:lock.checked,local_lock_blocks_api:lockapi.checked})});"
        "msg('m','Saved (reboot if hostname/IP mode changed)','ok')}catch(e){msg('m',e.message,'err')}}"
        "async function backup(){try{const j=await api('/api/v1/config');"
        "const blob=new Blob([JSON.stringify(j,null,2)],{type:'application/json'});"
        "const a=document.createElement('a'); a.href=URL.createObjectURL(blob); a.download='waketype-config.json'; a.click()"
        "}catch(e){msg('cmsg',e.message,'err')}}"
        "async function restore(){try{const body=JSON.parse(cfg.value);"
        "body.clear_static_ip=clearStatic.checked; body.include_secrets=incSecrets.checked;"
        "await api('/api/v1/config',{method:'POST',body:JSON.stringify(body)}); msg('cmsg','Restored','ok')}catch(e){msg('cmsg',e.message,'err')}}"
        "load();"
        "</script>");
}

static esp_err_t handle_ota_page(httpd_req_t *req)
{
    return send_html(req,
        "<section><h2>Firmware OTA</h2>"
        "<label>API token</label><input id=token>"
        "<button class=secondary onclick=\"setTok(document.getElementById('token').value)\">Save token locally</button>"
        "<p class=warn>Relays are forced OFF before update. Use a WakeType <code>.bin</code> built for this board.</p>"
        "<input id=file type=file accept=.bin>"
        "<button class=danger onclick=\"upload()\">Upload &amp; reboot</button>"
        "<p id=m></p></section>"
        "<script>"
        "document.getElementById('token').value=tok();"
        "async function upload(){const f=file.files[0]; if(!f){msg('m','Choose a .bin','err');return}"
        "msg('m','Uploading…','warn');"
        "try{const r=await fetch('/api/v1/ota',{method:'POST',headers:{Authorization:'Bearer '+tok(),'Content-Type':'application/octet-stream'},body:f});"
        "const t=await r.text(); if(!r.ok) throw new Error(t); msg('m',t,'ok')}catch(e){msg('m',e.message,'err')}}"
        "</script>");
}

/* Captive portal probes */
static esp_err_t handle_captive_redirect(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t web_ui_register(httpd_handle_t server)
{
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = handle_home},
        {.uri = "/settings", .method = HTTP_GET, .handler = handle_settings},
        {.uri = "/ota", .method = HTTP_GET, .handler = handle_ota_page},
        {.uri = "/generate_204", .method = HTTP_GET, .handler = handle_captive_redirect},
        {.uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = handle_captive_redirect},
        {.uri = "/canonical.html", .method = HTTP_GET, .handler = handle_captive_redirect},
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }
    ESP_LOGI(TAG, "Web UI registered");
    return ESP_OK;
}
