# Fridge 子模块 AGENTS.md

> 进入本目录时先读这份，再去 [docs/agent/fridge-subsystem.md](../../../../docs/agent/fridge-subsystem.md) 看架构全貌。**不要逐个通读源码**——先靠下表定位文件。

## 本目录文件职责
| 文件 | 职责 |
|---|---|
| `fridge_enum_utils.h` | 枚举定义（`StorageState`/`PackageState`/`ItemCategory`/`AlertLevel`）+ 字符串互转 + `ParseTime`/`FormatTime`。**纯头文件**，被各处 include |
| `fridge_item.h` / `.cc` | `FridgeItem` 数据类：字段、`IsExpired`/`RemainingDays`/`GetAlertLevel`、`ToJson`/`FromJson`（NVS 用）、`ToMcpJson`/`FromMcpJson`（MCP 用，含计算字段） |
| `fridge_manager.h` / `.cc` | `FridgeManager` 单例：内存索引 + NVS 持久化 + 增删改查统计报警 + `DataChangedCallback` |
| `fridge_mcp.h` / `.cc` | `FridgeMcpTools`：注册 11 个 MCP 工具（`fridge.help` + 10 业务工具）+ 各 `Handle*` 回调 |
| `llm_advisor.h` / `.cc` | 占位（尚未实现） |

## 关键约定
- **NVS 命名空间** `fridge`，key：`item:<id>`（`FridgeItem.ToJson()`）、ID 从 `Fridge_ID_START=1001` 起递增，上限 `Fridge_MAX_ITEMS=200`。
- **MCP 工具描述走渐进式披露**：10 个业务工具为单行瘦描述，枚举/格式/页面码收敛在 `fridge.help`（`HandleHelp`）。改工具时遵守此约定，见 [docs/agent/mcp-tools.md](../../../../docs/agent/mcp-tools.md)。
- **枚举字符串值**（LLM 侧用）：category=`vegetable|fruit|meat|egg|dairy|cooked|seasoning|beverage|quick|other`；storage=`Fresh|Frozen`；package=`Sealed|Opened`。完整映射见 `fridge_enum_utils.h`。
- **墨水屏页面**：`EpaperPage`（1=Chat/2=Stats/3=List/4=Recipe/5=Home），定义在 `main/display/epaperdisplay/epaper_display.h`，由 `Board::GetEpaperDisplay()->SetPage()` 切换。

## 改动入口
- 新增/改工具 → `fridge_mcp.cc::Initialize()` 注册 + 加 `Handle*`；`fridge_mcp.h` 加声明。同步更新 `fridge.help` 的 `tool_guide`。
- 改数据字段 → `fridge_item.h`（字段）+ `fridge_item.cc`（`ToJson`/`ToMcpJson`/`FromJson`/`FromMcpJson`）+ `fridge_manager.cc`（索引/持久化）。
- 改枚举 → `fridge_enum_utils.h`（双向转换都要改）+ `fridge.help` blob。
