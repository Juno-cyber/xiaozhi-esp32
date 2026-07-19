#include "local_control.h"
#include "mcp_server.h"
#include "application.h"
#include "board.h"
#include "Fridge/fridge_mcp.h"
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_netif.h>
#include <esp_app_desc.h>
#include <mdns.h>
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

// CORS 支持 — 允许浏览器跨域访问
static void SetCorsHeaders(httpd_req_t* req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
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

    // 获取 IP 地址并打印
    esp_netif_ip_info_t ip_info;
    auto netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "Local Control HTTP server ready!");
        ESP_LOGI(TAG, "  http://" IPSTR ":8080/", IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "  http://xiaozhi.local:8080/");
        ESP_LOGI(TAG, "Endpoints:");
        ESP_LOGI(TAG, "  GET  /                       Health check");
        ESP_LOGI(TAG, "  POST /mcp                    Raw JSON-RPC 2.0");
        ESP_LOGI(TAG, "  POST /api/call               Simple call");
        ESP_LOGI(TAG, "  POST /api/canvas_image       Upload image");
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

    const char* hostname = "xiaozhi";
    mdns_hostname_set(hostname);
    mdns_service_add("Xiaozhi Fridge", "_http", "_tcp", 8080, nullptr, 0);
    ESP_LOGI(TAG, "mDNS registered: %s.local", hostname);
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

    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "status", "ok");
    cJSON_AddStringToObject(json, "board", BOARD_NAME);
    cJSON_AddStringToObject(json, "version", app_desc->version);
    cJSON_AddStringToObject(json, "idf_version", esp_get_idf_version());

    // IP 地址
    esp_netif_ip_info_t ip_info;
    auto netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
        cJSON_AddStringToObject(json, "ip", ip_str);
    }

    char* json_str = cJSON_PrintUnformatted(json);
    httpd_resp_set_type(req, "application/json");
    SetCorsHeaders(req);
    httpd_resp_sendstr(req, json_str);
    free(json_str);
    cJSON_Delete(json);
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
        httpd_resp_sendstr(req, "{\"error\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    cJSON* tool = cJSON_GetObjectItem(body, "tool");
    cJSON* args = cJSON_GetObjectItem(body, "args");

    if (!cJSON_IsString(tool)) {
        cJSON_Delete(body);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"Missing 'tool' field\"}");
        return ESP_OK;
    }

    std::string tool_name = tool->valuestring;
    std::string args_str = "{}";
    if (cJSON_IsObject(args)) {
        char* s = cJSON_PrintUnformatted(args);
        args_str = s;
        free(s);
    }
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
        httpd_resp_sendstr(req, "{\"error\":\"Canvas storage not mounted\"}");
        return ESP_OK;
    }

    DIR* dir = opendir(CANVAS_MOUNT_POINT);
    if (!dir) {
        httpd_resp_set_type(req, "application/json");
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
