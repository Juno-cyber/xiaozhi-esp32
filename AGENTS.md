# AGENTS.md — xiaozhi-esp32 定制分支（Fridge on bread-compact-wifi-epaperx）

> 通用 AI 编码助手指引（Cursor / Gemini / Codex / Qoder / Claude 等）。本文件与 [CLAUDE.md](CLAUDE.md) 内容同步，是同一份 Tier-0 入口的跨工具镜像——关键信息双写，避免发散。**不要通读整个仓库**：70+ 板型是上游通用代码，本分支定制点只有「冰箱管理」。

## 项目身份
- 上游：[78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)，ESP32 语音 AI 聊天机器人（MCP 协议控物）。
- 本分支定制点：在 `bread-compact-wifi-epaperx` 板型上加完整「冰箱管理」（Fridge 模块：NVS 持久化 + 11 个 MCP 工具 + 墨水屏多页面）。
- **当前 active board**：`bread-compact-wifi-epaperx`（`sdkconfig` 的 `CONFIG_BOARD_TYPE_BREAD_COMPACT_WIFI_EPAPERX=y`）。

## 一条构建命令
```bash
idf.py build            # 编译（需 ESP-IDF 5.4+）
idf.py -p <PORT> flash  # 烧录
```
切板型 / assets / 分区细节见 [docs/agent/build-and-boards.md](docs/agent/build-and-boards.md)。

## main/ 目录速览（只列与本分支相关）
| 路径 | 职责 |
|---|---|
| `main/boards/bread-compact-wifi-epaperx/` | **本分支主战场**：板型实现 + `Fridge/` 子模块 |
| `main/boards/bread-compact-wifi-epaperx/Fridge/` | 冰箱管理（manager/item/mcp/enum_utils） |
| `main/mcp_server.{h,cc}` | MCP 框架（上游，**一般不改**）：`AddTool`/`Property`/分页 |
| `main/display/epaperdisplay/` | 墨水屏显示（`EpaperDisplay`/`EpaperPage`/`EpaperLabel`） |
| `main/application.{h,cc}` | 应用主循环；MCP 初始化入口 |
| `main/boards/common/board.h` | `Board` 基类（含 `GetEpaperDisplay()`） |

## 代码风格
- Google C++ 风格（上游约定）；注释用中文；日志用 `ESP_LOGI/W/E/D/V` + `TAG`。

## 按需阅读（渐进式披露）
- **改 Fridge 相关** → [docs/agent/fridge-subsystem.md](docs/agent/fridge-subsystem.md)
- **新增/改 MCP 工具** → [docs/agent/mcp-tools.md](docs/agent/mcp-tools.md)
- **构建/烧录/切板型/assets** → [docs/agent/build-and-boards.md](docs/agent/build-and-boards.md)
- 进入 Fridge 目录时 → [main/boards/bread-compact-wifi-epaperx/Fridge/AGENTS.md](main/boards/bread-compact-wifi-epaperx/Fridge/AGENTS.md)

## 不要碰
- `managed_components/`（上游组件，构建时拉取）
- `components/*`（除 `components/GxEPD2/`，后者含墨水屏局部刷新 LUT 定制）
- 70+ 其他板型目录（非本分支目标）
