#include "local_control.h"
#include "mcp_server.h"
#include "application.h"
#include "board.h"
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

static const char* TAG = "LocalCtrl";

LocalControl& LocalControl::GetInstance() {
    static LocalControl instance;
    return instance;
}

LocalControl::LocalControl() {
    response_sem_ = xSemaphoreCreateBinary();
}

void LocalControl::Start() {
    StartMdns();

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    config.max_uri_handlers = 8;
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

    // POST /mcp — 原始 JSON-RPC 2.0
    httpd_uri_t mcp_uri = {
        .uri = "/mcp",
        .method = HTTP_POST,
        .handler = HandleMcpPost,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &mcp_uri);

    // POST /api/call — 简化调用
    httpd_uri_t call_uri = {
        .uri = "/api/call",
        .method = HTTP_POST,
        .handler = HandleApiCall,
        .user_ctx = this
    };
    httpd_register_uri_handler(server_, &call_uri);

    // 获取 IP 地址并打印
    esp_netif_ip_info_t ip_info;
    auto netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "Local Control HTTP server ready!");
        ESP_LOGI(TAG, "  http://" IPSTR ":8080/", IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "  http://xiaozhi.local:8080/");
        ESP_LOGI(TAG, "Endpoints:");
        ESP_LOGI(TAG, "  GET  /            Health check");
        ESP_LOGI(TAG, "  POST /mcp         Raw JSON-RPC 2.0");
        ESP_LOGI(TAG, "  POST /api/call    Simple call");
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
