#pragma once
#include <string>
#include <esp_http_server.h>
#include <freertos/semphr.h>

// 局域网 HTTP MCP 桥
// 在 WiFi 连接后启动 HTTP 服务器，暴露 MCP 工具调用接口。
// 同一局域网内的 PC 可通过 HTTP POST 调用设备端 MCP 工具。
//
// 端点：
//   GET  /            健康检查，返回设备信息
//   POST /mcp         原始 JSON-RPC 2.0（与云端 LLM 走同一路径）
//   POST /api/call    简化调用 {"tool":"fridge.pagemanager","args":{"target_page":3}}
//
// mDNS: 设备注册为 xiaozhi.local（可在浏览器/curl 中直接使用）
class LocalControl {
public:
    static LocalControl& GetInstance();
    void Start();  // 启动 HTTP 服务器 + mDNS（在 WiFi 连接后调用）

    // 响应捕获：由 McpServer::ReplyResult/ReplyError 调用
    // 返回 true 表示响应已被本地捕获
    bool CaptureResponse(const std::string& payload);
    bool IsCapturing() const { return capturing_; }

private:
    LocalControl();
    httpd_handle_t server_ = nullptr;
    SemaphoreHandle_t response_sem_ = nullptr;
    std::string captured_response_;
    volatile bool capturing_ = false;

    // HTTP 处理函数
    static esp_err_t HandleHealth(httpd_req_t* req);
    static esp_err_t HandleMcpPost(httpd_req_t* req);
    static esp_err_t HandleApiCall(httpd_req_t* req);

    // 启动 mDNS 服务
    void StartMdns();

    // 注入工具调用并等待响应
    void InjectToolCall(const std::string& tool_name, const std::string& args_json);
    std::string WaitForResponse(uint32_t timeout_ms);
};

