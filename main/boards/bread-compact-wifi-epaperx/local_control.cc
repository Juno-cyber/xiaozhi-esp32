#include "local_control.h"
#include "mcp_server.h"
#include "application.h"
#include "board.h"
#include "Fridge/fridge_mcp.h"
#include "system_info.h"
#include "settings.h"
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_app_desc.h>
#include <mdns.h>
#include <wifi_station.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <cstring>
#include <string>
#include <cJSON.h>
#include <esp_littlefs.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

static const char* TAG = "LocalCtrl";

static std::string GetMdnsHostname() {
    std::string mac = SystemInfo::GetMacAddress();
    std::string suffix;
    suffix.reserve(6);
    for (char ch : mac) {
        if (ch != ':') {
            suffix.push_back(ch);
        }
    }
    if (suffix.size() > 6) {
        suffix = suffix.substr(suffix.size() - 6);
    }
    if (suffix.empty()) {
        suffix = "device";
    }
    return "xiaozhi-" + suffix;
}

static std::string GetMdnsUrl() {
    return "http://" + GetMdnsHostname() + ".local:8080/";
}

// ===== 设备显示名称（NVS 持久化）=====
// 名称仅用于展示（health /api/device_name 返回），不参与 mDNS hostname 与连接地址。
static const char* kDeviceNameNs = "device";
static const char* kDeviceNameKey = "name";
static const size_t kDeviceNameMaxBytes = 64;  // 约 20 个 UTF-8 中文字符

static std::string GetDeviceName() {
    Settings settings(kDeviceNameNs);
    std::string name = settings.GetString(kDeviceNameKey);
    return name.empty() ? BOARD_NAME : name;
}

static bool IsValidDeviceName(const std::string& name) {
    if (name.empty() || name.size() > kDeviceNameMaxBytes) {
        return false;
    }
    // 拒绝控制字符，避免破坏 JSON 输出
    for (unsigned char ch : name) {
        if (ch < 0x20 || ch == 0x7f) {
            return false;
        }
    }
    return true;
}


static const char* kScannerHtml = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>小智设备扫描</title>
<style>
:root{color-scheme:light;--bg:#f6f7f9;--panel:#fff;--text:#17202a;--muted:#667085;--line:#d9dee7;--accent:#0f766e;--accent2:#2563eb}
*{box-sizing:border-box}body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;background:var(--bg);color:var(--text)}
main{max-width:980px;margin:0 auto;padding:20px}header{display:flex;align-items:center;justify-content:space-between;gap:12px;margin-bottom:16px}
h1{font-size:24px;margin:0;font-weight:700}.bar{display:grid;grid-template-columns:1fr auto auto;gap:8px;margin-bottom:12px}
input,button{height:40px;border-radius:6px;border:1px solid var(--line);font-size:15px}input{padding:0 12px;background:#fff}
button{padding:0 14px;background:#fff;color:var(--text);cursor:pointer}button.primary{background:var(--accent);border-color:var(--accent);color:#fff}
button:disabled{opacity:.55;cursor:default}.status{color:var(--muted);font-size:14px;margin:8px 0 14px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(260px,1fr));gap:10px}.device{background:var(--panel);border:1px solid var(--line);border-radius:8px;padding:12px;display:grid;gap:8px}
.device.selected{border-color:var(--accent2);box-shadow:0 0 0 2px rgba(37,99,235,.12)}.name{font-weight:700}.meta{color:var(--muted);font-size:13px;line-height:1.45;word-break:break-all}
.actions{display:flex;gap:8px}.actions a,.actions button{height:34px;border-radius:6px;font-size:14px;text-decoration:none;display:inline-flex;align-items:center;justify-content:center;padding:0 10px;border:1px solid var(--line);background:#fff;color:var(--text)}
.actions button.select{background:var(--accent2);border-color:var(--accent2);color:#fff}.empty{border:1px dashed var(--line);border-radius:8px;padding:22px;color:var(--muted);text-align:center;background:#fff}
@media(max-width:640px){main{padding:14px}.bar{grid-template-columns:1fr}.actions{flex-wrap:wrap}}
</style>
</head>
<body>
<main>
<header><h1>小智设备</h1><button id="refresh">刷新</button></header>
<section class="bar">
<input id="subnet" placeholder="192.168.1" autocomplete="off">
<button id="scan" class="primary">扫描</button>
<button id="stop">停止</button>
</section>
<div id="status" class="status">就绪</div>
<section id="devices" class="grid"><div class="empty">暂无设备</div></section>
</main>
<script>
const PORT=8080;
const TIMEOUT=900;
const CONCURRENCY=32;
let aborters=[];
const $=id=>document.getElementById(id);
const devices=new Map();
function ipv4Host(){const h=location.hostname;return /^\d+\.\d+\.\d+\.\d+$/.test(h)?h:"";}
function defaultSubnet(){const h=ipv4Host();return h?h.split(".").slice(0,3).join("."):(localStorage.getItem("xiaozhi_subnet")||"192.168.1");}
function normalizeSubnet(v){const m=v.trim().match(/^(\d{1,3})\.(\d{1,3})\.(\d{1,3})$/);if(!m)return "";return m.slice(1).map(Number).every(n=>n>=0&&n<=255)?m.slice(1).join("."):"";}
function setStatus(t){$("status").textContent=t;}
function render(){
 const box=$("devices");const selected=localStorage.getItem("xiaozhi_selected_url")||"";
 const rows=[...devices.values()].sort((a,b)=>a.ip.localeCompare(b.ip,undefined,{numeric:true}));
 if(!rows.length){box.innerHTML='<div class="empty">暂无设备</div>';return;}
 box.innerHTML=rows.map(d=>`<article class="device ${d.http_url===selected?'selected':''}">
  <div class="name">${escapeHtml(d.hostname||d.mdns||d.board||'xiaozhi')}</div>
  <div class="meta">IP: ${escapeHtml(d.ip||'')}<br>MAC: ${escapeHtml(d.mac||'')}<br>URL: ${escapeHtml(d.http_url||'')}</div>
  <div class="actions"><button class="select" data-url="${escapeAttr(d.http_url)}">选择</button><a href="${escapeAttr(d.http_url)}" target="_blank">打开</a></div>
 </article>`).join("");
 box.querySelectorAll("button.select").forEach(btn=>btn.onclick=()=>{localStorage.setItem("xiaozhi_selected_url",btn.dataset.url);render();});
}
function escapeHtml(s){return String(s||"").replace(/[&<>"']/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"}[c]));}
function escapeAttr(s){return escapeHtml(s);}
async function probe(ip){
 const ctrl=new AbortController();aborters.push(ctrl);
 const timer=setTimeout(()=>ctrl.abort(),TIMEOUT);
 try{
  const r=await fetch(`http://${ip}:${PORT}/`,{signal:ctrl.signal,cache:"no-store"});
  if(!r.ok)return null;
  const d=await r.json();
  if(d.status!=="ok"||!d.board)return null;
  d.ip=d.ip||ip;d.http_url=d.http_url||`http://${ip}:${PORT}/`;
  return d;
 }catch(e){return null;}finally{clearTimeout(timer);}
}
async function scan(){
 const subnet=normalizeSubnet($("subnet").value);
 if(!subnet){setStatus("网段格式无效");return;}
 localStorage.setItem("xiaozhi_subnet",subnet);
 aborters.forEach(a=>a.abort());aborters=[];devices.clear();render();
 $("scan").disabled=true;setStatus(`扫描 ${subnet}.1-254`);
 let next=1,done=0,found=0;
 async function worker(){
  while(next<=254){
   const ip=`${subnet}.${next++}`;
   const d=await probe(ip);done++;
   if(d){devices.set(d.ip,d);found++;render();}
   if(done%8===0||done===254)setStatus(`已扫描 ${done}/254，发现 ${found} 台`);
  }
 }
 await Promise.all(Array.from({length:CONCURRENCY},worker));
 $("scan").disabled=false;setStatus(`完成，发现 ${found} 台`);
}
$("subnet").value=defaultSubnet();
$("scan").onclick=scan;$("refresh").onclick=scan;$("stop").onclick=()=>{aborters.forEach(a=>a.abort());$("scan").disabled=false;setStatus("已停止");};
if(ipv4Host())scan();
</script>
</body>
</html>
)HTML";

// CORS 支持 — 允许浏览器跨域访问
static void SetCorsHeaders(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers",
                       "Content-Type, Authorization, X-Requested-With");
    // 允许公网/HTTPS 控制台访问局域网设备（Chrome Private Network Access 预检）。
    httpd_resp_set_hdr(req, "Access-Control-Allow-Private-Network", "true");
    httpd_resp_set_hdr(req, "Vary", "Origin, Access-Control-Request-Method, Access-Control-Request-Headers, Access-Control-Request-Private-Network");
}

// CORS 预检请求处理
static esp_err_t HandleOptions(httpd_req_t* req) {
    SetCorsHeaders(req);
    httpd_resp_send(req, nullptr, 0);
    return ESP_OK;
}

// canvas_data 挂载点
static const char* CANVAS_MOUNT_POINT = "/canvas";
static bool canvas_mounted = false;

LocalControl& LocalControl::GetInstance() {
    static LocalControl instance;
    return instance;
}

LocalControl::LocalControl() {
    response_sem_ = xSemaphoreCreateBinary();
}

void LocalControl::Start() {
    MountCanvasStorage();
    StartMdns();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    config.max_uri_handlers = 16;
    config.stack_size = 8192;
    config.task_priority = 5;
    config.lru_purge_enable = true;

    if (httpd_start(&server_, &config) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server");
        return;
    }

    // GET / — 健康检查
    httpd_uri_t health_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = HandleHealth,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &health_uri);

    // OPTIONS / — CORS 预检（通配，需注册到每个 URI）
    httpd_uri_t health_options = {
        .uri = "/",
        .method = HTTP_OPTIONS,
        .handler = HandleOptions,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &health_options);

    // GET /ui — 设备扫描与选择页面
    httpd_uri_t ui_uri = {
        .uri = "/ui",
        .method = HTTP_GET,
        .handler = HandleUi,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &ui_uri);

    // OPTIONS /ui — CORS 预检
    httpd_uri_t ui_options = {
        .uri = "/ui",
        .method = HTTP_OPTIONS,
        .handler = HandleOptions,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &ui_options);

    // POST /mcp — 原始 JSON-RPC 2.0
    httpd_uri_t mcp_uri = {
        .uri = "/mcp",
        .method = HTTP_POST,
        .handler = HandleMcpPost,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &mcp_uri);

    // OPTIONS /mcp — CORS 预检
    httpd_uri_t mcp_options = {
        .uri = "/mcp",
        .method = HTTP_OPTIONS,
        .handler = HandleOptions,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &mcp_options);

    // POST /api/call — 简化调用
    httpd_uri_t call_uri = {
        .uri = "/api/call",
        .method = HTTP_POST,
        .handler = HandleApiCall,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &call_uri);

    // OPTIONS /api/call — CORS 预检
    httpd_uri_t call_options = {
        .uri = "/api/call",
        .method = HTTP_OPTIONS,
        .handler = HandleOptions,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &call_options);

    // POST /api/canvas_image — 上传图片
    httpd_uri_t upload_uri = {
        .uri = "/api/canvas_image",
        .method = HTTP_POST,
        .handler = HandleCanvasImageUpload,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &upload_uri);

    // GET /api/canvas_image — 列出已存图片
    httpd_uri_t list_uri = {
        .uri = "/api/canvas_image",
        .method = HTTP_GET,
        .handler = HandleCanvasImageList,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &list_uri);

    // OPTIONS /api/canvas_image — CORS 预检
    httpd_uri_t canvas_img_options = {
        .uri = "/api/canvas_image",
        .method = HTTP_OPTIONS,
        .handler = HandleOptions,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &canvas_img_options);

    // GET /api/device_name — 查询设备显示名称
    httpd_uri_t name_get_uri = {
        .uri = "/api/device_name",
        .method = HTTP_GET,
        .handler = HandleDeviceNameGet,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &name_get_uri);

    // POST /api/device_name — 设置设备显示名称
    httpd_uri_t name_set_uri = {
        .uri = "/api/device_name",
        .method = HTTP_POST,
        .handler = HandleDeviceNameSet,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &name_set_uri);

    // OPTIONS /api/device_name — CORS 预检
    httpd_uri_t name_options = {
        .uri = "/api/device_name",
        .method = HTTP_OPTIONS,
        .handler = HandleOptions,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &name_options);

    // 获取 IP 地址并打印
    esp_netif_ip_info_t ip_info;
    auto netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        std::string mdns_url = GetMdnsUrl();
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "Local Control HTTP server ready!");
        ESP_LOGI(TAG, "  http://" IPSTR ":8080/", IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "  %s", mdns_url.c_str());
        ESP_LOGI(TAG, "Endpoints:");
        ESP_LOGI(TAG, "  GET  /                       Health check");
        ESP_LOGI(TAG, "  GET  /ui                     Device scanner");
        ESP_LOGI(TAG, "  POST /mcp                    Raw JSON-RPC 2.0");
        ESP_LOGI(TAG, "  POST /api/call               Simple call");
        ESP_LOGI(TAG, "  POST /api/canvas_image       Upload image");
        ESP_LOGI(TAG, "  GET|POST /api/device_name     Device name (NVS)");
        ESP_LOGI(TAG, "  GET  /api/canvas_image       List images");
        ESP_LOGI(TAG, "========================================");
    } else {
        ESP_LOGW(TAG, "HTTP server started but IP info unavailable");
    }
}

void LocalControl::StartMdns() {
    if (mdns_init() != ESP_OK) {
        ESP_LOGW(TAG, "mDNS init failed");
        return;
    }

    std::string hostname = GetMdnsHostname();
    std::string service_name = "Xiaozhi Fridge " + hostname.substr(8);
    mdns_hostname_set(hostname.c_str());
    mdns_instance_name_set(service_name.c_str());
    mdns_service_add(service_name.c_str(), "_http", "_tcp", 8080, nullptr, 0);
    ESP_LOGI(TAG, "mDNS registered: %s.local", hostname.c_str());
}

// ===== 响应捕获机制 =====

bool LocalControl::CaptureResponse(const std::string& payload) {
    if (!capturing_) {
        return false;
    }
    captured_response_ = payload;
    capturing_ = false;
    xSemaphoreGive(response_sem_);
    return true;
}

// ===== HTTP Handlers =====

esp_err_t LocalControl::HandleHealth(httpd_req_t* req) {
    auto app_desc = esp_app_get_description();
    auto& wifi_station = WifiStation::GetInstance();
    std::string mdns = GetMdnsHostname() + ".local";
    std::string mdns_url = GetMdnsUrl();
    std::string mac = SystemInfo::GetMacAddress();

    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "ok");
    cJSON_AddStringToObject(json, "board", BOARD_NAME);
    cJSON_AddStringToObject(json, "name", GetDeviceName().c_str());
    cJSON_AddStringToObject(json, "version", app_desc->version);
    cJSON_AddStringToObject(json, "idf_version", esp_get_idf_version());
    cJSON_AddBoolToObject(json, "wifi_connected", wifi_station.IsConnected());
    cJSON_AddStringToObject(json, "mac", mac.c_str());
    cJSON_AddStringToObject(json, "hostname", GetMdnsHostname().c_str());
    cJSON_AddStringToObject(json, "mdns", mdns.c_str());
    cJSON_AddStringToObject(json, "mdns_url", mdns_url.c_str());
    cJSON_AddNumberToObject(json, "http_port", 8080);

    // IP 地址
    std::string ip_str = wifi_station.GetIpAddress();
    if (!ip_str.empty()) {
        std::string http_url = "http://" + ip_str + ":8080/";
        cJSON_AddStringToObject(json, "ip", ip_str.c_str());
        cJSON_AddStringToObject(json, "http_url", http_url.c_str());
    }

    char* json_str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    SetCorsHeaders(req);
    httpd_resp_sendstr(req, json_str);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t LocalControl::HandleDeviceNameGet(httpd_req_t* req) {
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "ok");
    cJSON_AddStringToObject(json, "name", GetDeviceName().c_str());
    char* json_str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    SetCorsHeaders(req);
    httpd_resp_sendstr(req, json_str);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t LocalControl::HandleDeviceNameSet(httpd_req_t* req) {
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON* body = cJSON_Parse(buf);
    cJSON* name_item = body ? cJSON_GetObjectItem(body, "name") : nullptr;
    bool ok = false;
    if (cJSON_IsString(name_item)) {
        std::string name(name_item->valuestring);
        if (name.empty()) {
            // 空名称 = 恢复默认（删除自定义名）
            Settings settings(kDeviceNameNs, true);
            settings.EraseKey(kDeviceNameKey);
            ok = true;
        } else if (IsValidDeviceName(name)) {
            Settings settings(kDeviceNameNs, true);
            settings.SetString(kDeviceNameKey, name);
            ok = true;
        }
    }
    if (body) {
        cJSON_Delete(body);
    }

    if (!ok) {
        httpd_resp_set_type(req, "application/json");
        SetCorsHeaders(req);
        httpd_resp_sendstr(req,
            "{\"status\":\"error\",\"error\":\"name 需为 1-64 字节且不含控制字符\"}");
        return ESP_OK;
    }

    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "ok");
    cJSON_AddStringToObject(json, "name", GetDeviceName().c_str());
    char* json_str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    SetCorsHeaders(req);
    httpd_resp_sendstr(req, json_str);
    free(json_str);
    cJSON_Delete(json);
    return ESP_OK;
}

esp_err_t LocalControl::HandleUi(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    SetCorsHeaders(req);
    httpd_resp_sendstr(req, kScannerHtml);
    return ESP_OK;
}

esp_err_t LocalControl::HandleMcpPost(httpd_req_t* req) {
    char buf[4096];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    auto* self = static_cast<LocalControl*>(req->user_ctx);

    // 注入原始 JSON-RPC 到 MCP 框架，等待响应
    self->InjectToolCall("", std::string(buf));
    std::string response = self->WaitForResponse(5000);

    httpd_resp_set_type(req, "application/json");
    SetCorsHeaders(req);
    httpd_resp_sendstr(req, response.c_str());
    return ESP_OK;
}

static bool MapLegacyToolCall(std::string& tool_name, cJSON* args) {
    struct ToolMap {
        const char* old_name;
        const char* new_name;
        const char* action;
    };
    static const ToolMap maps[] = {
        {"fridge.canvas.add_text", "fridge.canvas.control", "add_text"},
        {"fridge.canvas.add_rect", "fridge.canvas.control", "add_rect"},
        {"fridge.canvas.add_line", "fridge.canvas.control", "add_line"},
        {"fridge.canvas.add_image", "fridge.canvas.control", "add_image"},
        {"fridge.canvas.list", "fridge.canvas.control", "list"},
        {"fridge.canvas.remove", "fridge.canvas.control", "remove"},
        {"fridge.canvas.clear", "fridge.canvas.control", "clear"},
        {"fridge.canvas.refresh", "fridge.canvas.control", "refresh"},
        {"fridge.page.create", "fridge.page.control", "create"},
        {"fridge.page.delete", "fridge.page.control", "delete"},
        {"fridge.page.list", "fridge.page.control", "list"},
        {"fridge.page.rename", "fridge.page.control", "rename"},
        {"fridge.page.clear", "fridge.page.control", "clear"},
        {"fridge.page.element.add", "fridge.page.element.control", "add"},
        {"fridge.page.element.update", "fridge.page.element.control", "update"},
        {"fridge.page.element.remove", "fridge.page.element.control", "remove"},
        {"fridge.page.element.list", "fridge.page.element.control", "list"},
    };

    for (const auto& map : maps) {
        if (tool_name == map.old_name) {
            tool_name = map.new_name;
            cJSON_DeleteItemFromObject(args, "action");
            cJSON_AddStringToObject(args, "action", map.action);
            return true;
        }
    }
    return false;
}

esp_err_t LocalControl::HandleApiCall(httpd_req_t* req) {
    char buf[4096];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    // 解析简化格式 {"tool":"fridge.pagemanager","args":{"target_page":3}}
    cJSON* body = cJSON_Parse(buf);
    if (!body) {
        httpd_resp_set_type(req, "application/json");
        SetCorsHeaders(req);
        httpd_resp_sendstr(req, "{\"error\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    cJSON* tool = cJSON_GetObjectItem(body, "tool");
    cJSON* args = cJSON_GetObjectItem(body, "args");

    if (!cJSON_IsString(tool)) {
        cJSON_Delete(body);
        httpd_resp_set_type(req, "application/json");
        SetCorsHeaders(req);
        httpd_resp_sendstr(req, "{\"error\":\"Missing 'tool' field\"}");
        return ESP_OK;
    }

    std::string tool_name = tool->valuestring;
    cJSON* effective_args = cJSON_IsObject(args) ? cJSON_Duplicate(args, true) : cJSON_CreateObject();
    MapLegacyToolCall(tool_name, effective_args);
    char* s = cJSON_PrintUnformatted(effective_args);
    std::string args_str = s ? s : "{}";
    free(s);
    cJSON_Delete(effective_args);
    cJSON_Delete(body);

    auto* self = static_cast<LocalControl*>(req->user_ctx);
    self->InjectToolCall(tool_name, args_str);
    std::string response = self->WaitForResponse(5000);

    httpd_resp_set_type(req, "application/json");
    SetCorsHeaders(req);
    httpd_resp_sendstr(req, response.c_str());
    return ESP_OK;
}

void LocalControl::InjectToolCall(const std::string& tool_name, const std::string& args_json) {
    // 标记正在等待响应
    capturing_ = true;
    captured_response_.clear();
    xSemaphoreTake(response_sem_, 0);  // 清空信号量

    if (tool_name.empty()) {
        // 原始 JSON-RPC 消息直接注入
        ESP_LOGI(TAG, "Injecting raw JSON-RPC");
        McpServer::GetInstance().ParseMessage(args_json);
    } else {
        // 构造 tools/call JSON-RPC
        static int next_id = 10000;
        int id = next_id++;
        std::string msg = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id)
                        + ",\"method\":\"tools/call\",\"params\":{\"name\":\""
                        + tool_name + "\",\"arguments\":" + args_json + "}}";
        ESP_LOGI(TAG, "Injecting tool call: %s", tool_name.c_str());
        McpServer::GetInstance().ParseMessage(msg);
    }
}

std::string LocalControl::WaitForResponse(uint32_t timeout_ms) {
    if (xSemaphoreTake(response_sem_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return captured_response_;
    }
    return "{\"error\":\"timeout\"}";
}

// ==================== Canvas 存储 ====================

void LocalControl::MountCanvasStorage() {
    esp_vfs_littlefs_conf_t conf = {};
    conf.base_path = CANVAS_MOUNT_POINT;
    conf.partition_label = "canvas_data";
    conf.format_if_mount_failed = true;

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount canvas_data LittleFS: %s", esp_err_to_name(ret));
        canvas_mounted = false;
        return;
    }
    canvas_mounted = true;

    // 打印使用情况
    size_t total = 0, used = 0;
    esp_littlefs_info("canvas_data", &total, &used);
    ESP_LOGI(TAG, "Canvas storage mounted at %s (total: %d KB, used: %d KB)",
             CANVAS_MOUNT_POINT, (int)(total / 1024), (int)(used / 1024));

    // LittleFS 挂载成功后，恢复 canvas 布局
    // 放到独立任务中执行，避免 pthread 栈溢出（LoadPageLayout 有大量 std::string 局部变量）
    xTaskCreate([](void* arg) {
        FridgeMcpTools::RestoreCanvasLayout();
        vTaskDelete(nullptr);
    }, "restore_layout", 16384, nullptr, 5, nullptr);
}

// 生成安全的文件路径：/canvas/<name>.bin
static std::string MakeCanvasImagePath(const std::string& name) {
    std::string path = std::string(CANVAS_MOUNT_POINT) + "/" + name;
    // 简单安全检查：只允许字母数字下划线
    for (size_t i = CANVAS_MOUNT_POINT ? strlen(CANVAS_MOUNT_POINT) + 1 : 1; i < path.size(); i++) {
        char c = path[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '.')) {
            path[i] = '_';
        }
    }
    return path;
}

esp_err_t LocalControl::HandleCanvasImageUpload(httpd_req_t* req) {
    if (!canvas_mounted) {
        httpd_resp_set_type(req, "application/json");
        SetCorsHeaders(req);
        httpd_resp_sendstr(req, "{\"error\":\"Canvas storage not mounted\"}");
        return ESP_OK;
    }

    // 从查询参数 name=xxx 获取文件名
    char name_buf[64] = {0};
    if (httpd_req_get_url_query_str(req, name_buf, sizeof(name_buf)) == ESP_OK) {
        char name_val[56] = {0};
        if (httpd_query_key_value(name_buf, "name", name_val, sizeof(name_val)) == ESP_OK) {
            std::string path = MakeCanvasImagePath(name_val);
            ESP_LOGI(TAG, "Uploading canvas image to: %s", path.c_str());

            // 分段接收 body 并写入文件
            FILE* f = fopen(path.c_str(), "wb");
            if (!f) {
                httpd_resp_set_type(req, "application/json");
                SetCorsHeaders(req);
                httpd_resp_sendstr(req, "{\"error\":\"Cannot create file\"}");
                return ESP_OK;
            }

            char buf[512];
            int total_received = 0;
            int remaining = req->content_len;
            while (remaining > 0) {
                int to_read = (remaining > (int)sizeof(buf)) ? (int)sizeof(buf) : remaining;
                int ret = httpd_req_recv(req, buf, to_read);
                if (ret <= 0) {
                    if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
                        continue;
                    }
                    break;
                }
                fwrite(buf, 1, ret, f);
                total_received += ret;
                remaining -= ret;
            }
            fclose(f);

            ESP_LOGI(TAG, "Canvas image saved: %s (%d bytes)", path.c_str(), total_received);

            // 返回 JSON
            std::string resp = "{\"status\":\"success\",\"name\":\"" + std::string(name_val) +
                               "\",\"size\":" + std::to_string(total_received) +
                               ",\"path\":\"" + path + "\"}";
            httpd_resp_set_type(req, "application/json");
            SetCorsHeaders(req);
            httpd_resp_sendstr(req, resp.c_str());
            return ESP_OK;
        }
    }

    httpd_resp_set_type(req, "application/json");
    SetCorsHeaders(req);
    httpd_resp_sendstr(req, "{\"error\":\"Missing 'name' query parameter. Use POST /api/canvas_image?name=myimage\"}");
    return ESP_OK;
}

esp_err_t LocalControl::HandleCanvasImageList(httpd_req_t* req) {
    if (!canvas_mounted) {
        httpd_resp_set_type(req, "application/json");
        SetCorsHeaders(req);
        httpd_resp_sendstr(req, "{\"error\":\"Canvas storage not mounted\"}");
        return ESP_OK;
    }

    char query[96] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char name_val[56] = {0};
        if (httpd_query_key_value(query, "name", name_val, sizeof(name_val)) == ESP_OK &&
            name_val[0] != '\0') {
            std::string path = MakeCanvasImagePath(name_val);
            FILE* f = fopen(path.c_str(), "rb");
            if (!f) {
                httpd_resp_set_type(req, "application/json");
                SetCorsHeaders(req);
                httpd_resp_sendstr(req, "{\"error\":\"Image file not found\"}");
                return ESP_OK;
            }

            httpd_resp_set_type(req, "application/octet-stream");
            SetCorsHeaders(req);

            char buf[512];
            while (true) {
                size_t rd = fread(buf, 1, sizeof(buf), f);
                if (rd > 0) {
                    esp_err_t err = httpd_resp_send_chunk(req, buf, rd);
                    if (err != ESP_OK) {
                        fclose(f);
                        return err;
                    }
                }
                if (rd < sizeof(buf)) {
                    break;
                }
            }
            fclose(f);
            httpd_resp_send_chunk(req, nullptr, 0);
            return ESP_OK;
        }
    }

    DIR* dir = opendir(CANVAS_MOUNT_POINT);
    if (!dir) {
        httpd_resp_set_type(req, "application/json");
        SetCorsHeaders(req);
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }

    std::string json = "[";
    bool first = true;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string path = std::string(CANVAS_MOUNT_POINT) + "/" + ent->d_name;
        struct stat st;
        if (stat(path.c_str(), &st) == 0) {
            if (!first) json += ",";
            first = false;
            json += "{\"name\":\"";
            json += ent->d_name;
            json += "\",\"size\":";
            json += std::to_string(st.st_size);
            json += "}";
        }
    }
    closedir(dir);
    json += "]";

    httpd_resp_set_type(req, "application/json");
    SetCorsHeaders(req);
    httpd_resp_sendstr(req, json.c_str());
    return ESP_OK;
}
