# 局域网 MCP 设备控制 — 设计文档

> xiaozhi-esp32 定制分支 · `bread-compact-wifi-epaperx` 板型
> 版本：2026-07-04

## 一、概述

在 ESP32 小智设备上实现局域网 HTTP 控制能力。同一 WiFi 下的 PC/手机可通过 HTTP 调用设备端全部 MCP 工具（页面切换、冰箱增删改查、菜谱推荐等），无需云端 LLM 参与。

### 设计目标

| 目标 | 实现 |
|------|------|
| 同一局域网内控制设备 | HTTP 服务器 (port 8080) + mDNS |
| 调用设备端 MCP 工具 | 注入 JSON-RPC 到 McpServer，与云端 LLM 走同一路径 |
| 获取工具返回值 | 响应捕获钩子拦截 `SendMcpMessage`，HTTP 同步返回 |
| 设备发现 | mDNS 注册 `xiaozhi-<mac后6位>.local`；网页 `/ui` 可扫描同网段设备 |

### 非目标

- 不做串口控制（已移除）
- 不做云端依赖（纯局域网，不经过 MQTT）
- 不做鉴权（局域网内部信任模型）

## 二、架构

```
PC / 手机 (同一 WiFi)
    │
    │  HTTP POST
    │  curl / Python / 浏览器
    │
    ▼
ESP32-S3 (192.168.1.10:8080  /  xiaozhi-a1b2c3.local:8080)
    │
    ├─ GET  /          → 设备健康检查
    ├─ GET  /ui        → 浏览器设备扫描与选择页面
    ├─ POST /mcp       → 原始 JSON-RPC 2.0
    ├─ POST /api/call  → 简化调用 {"tool":"...","args":{...}}
    │
    ▼
LocalControl (HTTP Server)
    │  解析请求 → 构造 JSON-RPC
    │
    ▼
McpServer::ParseMessage()        ← 与云端 LLM 走完全相同的路径
    │
    ▼
McpServer::DoToolCall()
    │  Application::Schedule() → 主线程执行
    │
    ▼
FridgeMcpTools::Handle*
    │
    ├──→ EpaperDisplay::SetPage()      墨水屏物理刷新
    ├──→ FridgeManager::GetStatistics() 冰箱数据
    └──→ FridgeManager::AddItem()      NVS 持久化
    │
    ▼
McpServer::ReplyResult()
    │
    ▼
Application::SendMcpMessage()
    │
    ├─ mcp_message_hook_ 捕获？  → 是 → LocalControl::CaptureResponse() → HTTP 响应
    │
    └─ 否 → protocol_->SendMcpMessage() → MQTT 发往云端
```

## 三、HTTP 接口

### 3.1 健康检查

```
GET /
```

**响应**：
```json
{
  "status": "ok",
  "board": "bread-compact-wifi-epaperx",
  "version": "2.0.3",
  "idf_version": "v5.4.2",
  "wifi_connected": true,
  "mac": "aa:bb:cc:a1:b2:c3",
  "hostname": "xiaozhi-a1b2c3",
  "mdns": "xiaozhi-a1b2c3.local",
  "mdns_url": "http://xiaozhi-a1b2c3.local:8080/",
  "http_port": 8080,
  "ip": "192.168.1.10",
  "http_url": "http://192.168.1.10:8080/"
}
```

### 3.1.1 设备扫描页

```
GET /ui
```

浏览器页面会并发探测当前网段 `1-254` 的 `http://<ip>:8080/`，识别返回 `status=ok` 与 `board` 字段的小智设备，并把设备按 IP、MAC、hostname 列表展示。选择后会把目标 `http_url` 存入浏览器 `localStorage.xiaozhi_selected_url`。

### 3.2 简化调用

```
POST /api/call
Content-Type: application/json
```

**请求体**：
```json
{
  "tool": "fridge.pagemanager",
  "args": { "target_page": 3 }
}
```

**响应**：
```json
{
  "jsonrpc": "2.0",
  "id": 10000,
  "result": {
    "content": [
      { "type": "text", "text": "{\"status\":\"success\",\"current_page\":3}" }
    ],
    "isError": false
  }
}
```

### 3.3 原始 JSON-RPC

```
POST /mcp
Content-Type: application/json
```

**请求体**（标准 JSON-RPC 2.0）：
```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "fridge.stats.summary",
    "arguments": {}
  }
}
```

**响应**：同 3.2

## 四、可用 MCP 工具

| 工具 | 说明 | 示例参数 |
|------|------|---------|
| `fridge.pagemanager` | 切换墨水屏页面 | `{"target_page": 3}` |
| `fridge.stats.summary` | 冰箱统计摘要 | `{}` |
| `fridge.stats.query` | 按条件查询食材 | `{"category":"meat"}` |
| `fridge.item.list` | 列出所有食材 | `{"sort_by":"name"}` |
| `fridge.item.get` | 获取单条食材详情 | `{"item_id":1001}` |
| `fridge.item.add` | 添加食材 | `{"name":"番茄","category":"vegetable","quantity":3,"unit":"个","expire_time":"2026-07-10 12:00:00"}` |
| `fridge.item.update` | 更新食材信息 | `{"item_id":1001,"quantity":2}` |
| `fridge.item.remove` | 删除食材 | `{"item_id":1001}` |
| `fridge.item.clear_all` | 清空全部 | `{}` |
| `fridge.recipe.recommend` | 推荐菜谱并显示 | `{"recommendation_mode":"fridge_only","dish_name":"番茄炒蛋","required_ingredients":"番茄,鸡蛋"}` |
| `device.network.info` | 查询当前 Wi-Fi IP 和本地 HTTP 地址 | `{}` |

### 墨水屏页面定义

| target_page | 名称 | 显示内容 |
|-------------|------|---------|
| 1 | CHAT_PAGE | 状态栏 + 表情 + 聊天消息 |
| 2 | FRIDGE_STATS_PAGE | 大时钟 + 冰箱统计 |
| 3 | FOOD_LIST_PAGE | 食材列表（最多4行） |
| 4 | RECIPE_PAGE | AI 食谱推荐 |
| 5 | HOME_PIC_DISPLAY | 纪念日图片 |

## 五、使用示例

### curl

```bash
# 健康检查
curl http://192.168.1.10:8080/

# 打开扫描页：浏览器访问 http://192.168.1.10:8080/ui

# 切换到食材列表页
curl -X POST http://192.168.1.10:8080/api/call \
  -H "Content-Type: application/json" \
  -d '{"tool":"fridge.pagemanager","args":{"target_page":3}}'

# 添加番茄
curl -X POST http://192.168.1.10:8080/api/call \
  -H "Content-Type: application/json" \
  -d '{"tool":"fridge.item.add","args":{"name":"番茄","category":"vegetable","quantity":3,"unit":"个","expire_time":"2026-07-10 12:00:00"}}'

# 查看冰箱统计
curl -X POST http://192.168.1.10:8080/api/call \
  -H "Content-Type: application/json" \
  -d '{"tool":"fridge.stats.summary","args":{}}'
```

### Python

```python
import requests

IP = "192.168.1.10"
URL = f"http://{IP}:8080/api/call"

def call_tool(tool, **args):
    r = requests.post(URL, json={"tool": tool, "args": args})
    return r.json()

# 切页
call_tool("fridge.pagemanager", target_page=2)

# 加食材
call_tool("fridge.item.add", name="牛奶", category="dairy",
          quantity=1, unit="盒", expire_time="2026-07-15 12:00:00")

# 统计
print(call_tool("fridge.stats.summary"))
```

## 六、核心实现

### 6.1 文件结构

```
main/boards/bread-compact-wifi-epaperx/
├── local_control.h          ← HTTP 服务器 + 响应捕获声明
├── local_control.cc         ← 实现
├── compact_wifi_board_epaperx.cc  ← 启动入口
└── Fridge/                  ← 冰箱 MCP 工具（已有）

main/application.h           ← 新增 mcp_message_hook_ 字段
main/application.cc          ← SendMcpMessage 插入钩子
```

### 6.2 响应捕获机制

MCP 工具的返回值通过 `McpServer::ReplyResult()` → `Application::SendMcpMessage()` 发出。正常情况下会发往云端 MQTT。为了在局域网控制时把响应返回给 HTTP 调用方，在 `Application` 中新增了钩子：

**application.h**：
```cpp
// 本地控制钩子：返回 true 表示响应已被本地捕获，不再发往云端
std::function<bool(const std::string&)> mcp_message_hook_;
```

**application.cc**：
```cpp
void Application::SendMcpMessage(const std::string& payload) {
    if (mcp_message_hook_) {
        if (mcp_message_hook_(payload)) {
            return;  // 本地捕获，不发云端
        }
    }
    // 正常发往 MQTT
    protocol_->SendMcpMessage(payload);
}
```

**local_control.cc** — 捕获 + 等待：
```cpp
bool LocalControl::CaptureResponse(const std::string& payload) {
    if (!capturing_) return false;
    captured_response_ = payload;
    capturing_ = false;
    xSemaphoreGive(response_sem_);  // 唤醒 HTTP handler
    return true;
}

std::string WaitForResponse(uint32_t timeout_ms) {
    if (xSemaphoreTake(response_sem_, pdMS_TO_TICKS(timeout_ms)) == pdTRUE) {
        return captured_response_;
    }
    return "{\"error\":\"timeout\"}";
}
```

### 6.3 启动时序

```
设备上电
  │
  ├─ InitializeTools()
  │    ├─ 注册 MCP 工具
  │    ├─ 设置 mcp_message_hook_
  │    └─ 启动后台线程等 WiFi
  │
  ├─ Application::Start()
  │    ├─ WiFi 连接
  │    ├─ MQTT 连接
  │    └─ 进入空闲状态
  │
  └─ 后台线程检测到 WiFi 连接
       ├─ mdns_init() → 注册 xiaozhi-<mac后6位>.local
       ├─ httpd_start() → 监听 8080
       └─ 注册 URI handlers
```

> **关键**：HTTP 服务器必须在 WiFi/lwip 初始化后启动。之前在 `InitializeTools()` 阶段直接启动导致 `assert failed: tcpip_send_msg_wait_sem` 崩溃。修复方案是后台线程轮询 `WifiStation::IsConnected()`，连上后才调用 `Start()`。

### 6.4 请求处理流程

以 `POST /api/call {"tool":"fridge.pagemanager","args":{"target_page":3}}` 为例：

```
1. httpd 收到 HTTP POST
2. HandleApiCall() 解析出 tool_name 和 args
3. InjectToolCall():
   a. 构造 JSON-RPC: {"jsonrpc":"2.0","id":N,"method":"tools/call",...}
   b. capturing_ = true
   c. McpServer::ParseMessage(msg) 注入框架
4. McpServer::DoToolCall() → Application::Schedule() → 主线程执行
5. FridgeMcpTools::HandlePageManager():
   - EpaperDisplay::SetPage(3) → 墨水屏刷新
   - 返回 {"status":"success","current_page":3}
6. McpServer::ReplyResult() → Application::SendMcpMessage()
7. mcp_message_hook_ → CaptureResponse() → 信号量唤醒
8. WaitForResponse() 返回响应
9. httpd 把响应发回给 PC
```

## 七、验证记录

### 7.1 崩溃修复

| 问题 | 原因 | 修复 |
|------|------|------|
| 设备无限重启 | HTTP 服务器在 lwip 初始化前启动 | 后台线程等 WiFi 连上后再启动 |

崩溃日志：
```
assert failed: tcpip_send_msg_wait_sem (Invalid mbox)
Backtrace: 0x40379f89:0x3fcb2660 ...
Rebooting...
```

### 7.2 功能验证

| 测试项 | 命令 | 结果 |
|--------|------|------|
| 健康检查 | `GET /` | ✅ 返回设备信息 |
| 切到 Page 1 | `fridge.pagemanager target_page=1` | ✅ 墨水屏刷新 |
| 切到 Page 2 | `fridge.pagemanager target_page=2` | ✅ 墨水屏刷新 |
| 切到 Page 3 | `fridge.pagemanager target_page=3` | ✅ 墨水屏刷新 |
| 切到 Page 4 | `fridge.pagemanager target_page=4` | ✅ 墨水屏刷新 |
| 切到 Page 5 | `fridge.pagemanager target_page=5` | ✅ 墨水屏刷新 |
| 冰箱统计 | `fridge.stats.summary` | ✅ 返回 JSON |
| mDNS | `xiaozhi-<mac后6位>.local:8080` | ✅ 可解析 |

## 八、限制与未来方向

### 当前限制
- 无鉴权（局域网内任意设备可调用）
- 无并发控制（同一时间只处理一个 MCP 调用）
- 5 秒响应超时（复杂工具可能不够）
- HTTP body 上限 4KB

### 未来方向
- [ ] 加入简单 token 鉴权
- [ ] WebSocket 实时推送（页面变更通知）
- [ ] 支持并发的请求队列
- [ ] 增大 body 限制，支持批量操作
- [ ] 设备发现：UDP 广播自动发现局域网内所有小智设备
