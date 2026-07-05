# MCP 工具开发指引

> 如何在 xiaozhi-esp32 上新增/修改设备端 MCP 工具。先读 [CLAUDE.md](../../CLAUDE.md) 再读本文件。协议细节见上游 [docs/mcp-usage.md](../mcp-usage.md) 与 [docs/mcp-protocol.md](../mcp-protocol.md)。

## 框架核心（[main/mcp_server.h](../../main/mcp_server.h) / [main/mcp_server.cc](../../main/mcp_server.cc)）

注册一个工具：
```cpp
McpServer::GetInstance().AddTool(
    "module.action",            // name: 唯一，建议 "模块.功能" 层次命名
    "一行英文描述",             // description: 给 LLM 看的，见下方「渐进式披露」
    PropertyList({              // 输入参数
        Property("volume", kPropertyTypeInteger, 0, 100)   // 整数 + 范围
    }),
    [this](const PropertyList& props) -> ReturnValue {
        int v = props["volume"].value<int>();
        // ... 业务逻辑
        return true;            // 返回 bool / int / std::string / cJSON* / ImageContent*
    });
```

### 关键类型
- `ReturnValue = std::variant<bool, int, std::string, cJSON*, ImageContent*>`（[mcp_server.h:50](../../main/mcp_server.h#L50)）。
- `Property(name, type)` 必填；`Property(name, type, default)` 可选带默认值；`Property(name, type, min, max)` 整数范围（[mcp_server.h:58-95](../../main/mcp_server.h#L58-L95)）。`PropertyType` 仅 `kPropertyTypeBoolean` / `kPropertyTypeInteger` / `kPropertyTypeString`。
- `PropertyList`：`AddProperty()` / `operator["name"]`（找不到抛异常，访问可选参数用 try-catch，见 fridge_mcp.cc 现有写法）/ `GetRequired()`（无默认值=必填）。
- 错误处理：callback 里抛 `std::runtime_error` → 框架转成 MCP error 响应（见 [mcp_server.cc](../../main/mcp_server.cc) `DoToolCall`）。

### 注册时机与位置
- **common tools**（跨全部板型，AI 可见）：`McpServer::AddCommonTools()`（[mcp_server.cc:33](../../main/mcp_server.cc#L33)），由 `Application` 启动时调用（[application.cc:442](../../main/application.cc#L442)）。**上游代码，一般不改**。
- **user-only tools**（仅用户可见，`annotations.audience=["user"]`，AI 不可见）：`AddUserOnlyTool` / `AddUserOnlyTools()`。
- **板型自定义工具**：在板型的 `InitializeTools()` 里注册。本分支范例见 [compact_wifi_board_epaperx.cc:192](../../main/boards/bread-compact-wifi-epaperx/compact_wifi_board_epaperx.cc#L192)（注册 `FridgeMcpTools`）。

### tools/list 分页行为（[mcp_server.cc:457](../../main/mcp_server.cc#L457) `GetToolsList`）
- 单页负载上限 **8000 字节**（`max_payload_size`）；超限用 `nextCursor`（下一工具名）分页，客户端带 `cursor` 续拉。
- `withUserTools=true` 时才返回 user-only 工具。
- **含义**：工具描述越长 → 单页塞得越少 → 分页轮次越多 → 每会话 LLM 输入 token 越大。这正是「描述瘦身」的价值。

## 渐进式披露（progressive disclosure）约定 ⭐

本分支在 Fridge 模块首次引入该模式，**新增工具组时应遵循**：

1. **业务工具描述保持单行英文**，不内联枚举/格式长串、不双语重复。
2. **枚举/格式/页面码等参考信息收敛到一个 `<module>.help` 发现工具**（无参数，瘦描述）：
   - 描述形如 `"Usage reference. Call once to get enum values, formats, and a tool-selection guide."`
   - callback 返回一个 JSON blob，含：枚举值映射（中英）、时间/格式约定、页面/状态码、每个业务工具一句话用途。
3. 该 blob **每会话只在 LLM 主动调用时下发一次**，不随每次 `tools/list` 重发 → 净降 token。
4. 范例：[fridge_mcp.cc](../../main/boards/bread-compact-wifi-epaperx/Fridge/fridge_mcp.cc) 的 `fridge.help` 注册 + `HandleHelp` 实现，以及 10 个业务工具的瘦描述。

> 简单工具组（1-2 个工具、无共享枚举）不必套此模式，直接写清楚即可。该模式针对「多工具 + 共享枚举/格式」场景。

## 可选未来项（不在本次范围）
- `AddCommonTools` 里的 common tools 描述（`self.get_device_status` 等）同样可瘦身，但跨全部 70+ 板型属上游代码，改动需谨慎评估。

## 检查清单（新增工具时）
- [ ] name 唯一且层次化（`module.action`）
- [ ] description 单行、点明「何时用」
- [ ] 必填参数无默认值、可选参数带默认值；整数给范围
- [ ] callback 用 try-catch 包业务逻辑，错误抛 `std::runtime_error` 或返回错误字符串
- [ ] 若工具组 ≥3 且共享枚举/格式 → 按「渐进式披露」抽 `<module>.help`
- [ ] `idf.py build` 编译通过
