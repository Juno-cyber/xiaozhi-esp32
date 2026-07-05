# Fridge 子系统指引

> 进入本文件前应先读根目录 [CLAUDE.md](../../CLAUDE.md)。本文档描述「冰箱管理」功能模块的全貌，供改 Fridge 相关代码时按需查阅。

## 定位
Fridge 模块挂在 `bread-compact-wifi-epaperx` 板型上，提供：食材的 NVS 持久化、面向云端 LLM 的 11 个 MCP 工具、以及墨水屏多页面联动。**所有代码在** `main/boards/bread-compact-wifi-epaperx/Fridge/`。

## 文件职责
| 文件 | 职责 |
|---|---|
| `fridge_enum_utils.h` | 全部枚举（存储/包装/分类/报警）+ 整数↔字符串转换 + `ParseTime`/`FormatTime`。**纯 header，内联函数**。 |
| `fridge_item.{h,cc}` | `FridgeItem` 食材对象：字段、`ToJson/FromJson`（NVS 内部格式，数字枚举）、`ToMcpJson/FromMcpJson`（LLM 通信格式，字符串枚举 + 计算字段）、`IsExpired/RemainingDays/GetAlertLevel`。 |
| `fridge_manager.{h,cc}` | `FridgeManager` 单例：内存索引 + NVS 持久化 + 查询/统计/报警。 |
| `fridge_mcp.{h,cc}` | `FridgeMcpTools`：11 个 MCP 工具的注册 + 回调实现。 |
| `llm_advisor.{h,cc}` | 占位（当前空壳），预留 LLM 提示构造。 |
| `AGENTS.md` | 本目录迷你指引（指向本文档）。 |

## FridgeManager（单例）
- `FridgeManager::GetInstance()` 首次调用触发 `LoadFromNVS()`。
- **NVS key 设计**（namespace = `"fridge"`）：
  - `item:<id>` → `FridgeItem::ToJson()` 字符串（数字枚举，含消耗记录）。
  - 不持久化 `last_id`；启动时遍历 `[Fridge_ID_START, Fridge_ID_START+Fridge_MAX_ITEMS)` 扫描已占用 key，`GetNextItemId()` 取首个空位。
- 内存索引：`items_`（id→item）、`category_index_`（multimap，分类→id）、`id_list_`（有序 id，便于遍历/清空）。
- 常量：`Fridge_MAX_ITEMS=200`、`Fridge_ID_START=1001`、`Fridge_Alert_Days=3`（即"即将过期"窗口）。
- 数据变更走 `NotifyDataChanged()` 回调（墨水屏可订阅刷新，当前板型未挂接）。

## FridgeItem 字段
`id, name, category, quantity(float), unit, state(StorageState), package_state, add_time, expire_time, last_update_time, open_time, consume_history(最多4条)`。

- `ToMcpJson()`（发给 LLM）输出字段：`item_id, name, category(字符串), quantity, unit, storage_state(字符串), package_state(字符串), add_time(格式化), expire_time(格式化), remaining_days, alert_level(字符串), is_expired`。
- `FromMcpJson()` 接受 LLM 下发的字符串枚举（用于反向重建对象）。

## 枚举速查（详见 `fridge_enum_utils.h`）
- 分类 `ItemCategory`(int 0-9)：vegetable/fruit/meat/egg/dairy/cooked/seasoning/beverage/quick/other。
- 存储 `StorageState`：Fresh(0 冷藏)/Frozen(1 冷冻)。
- 包装 `PackageState`：Sealed(0)/Opened(1)（只读，设备维护）。
- 报警 `AlertLevel`：None/Warning(≤3天)/Critical(已过期)。
- 时间：`ParseTime` 接受 `YYYY-MM-DD HH:MM:SS` 或纯数字时间戳；`FormatTime` 输出 `YYYY-MM-DD HH:MM:SS`，0 → `"N/A"`。

## MCP 工具清单（11 个）
注册在 `FridgeMcpTools::Initialize()`，由板型 `InitializeTools()` 调用。**渐进式披露**：`fridge.help` 集中下发枚举/格式/页面/工具指南，其余 10 个工具描述保持单行瘦描述（详见 [mcp-tools.md](mcp-tools.md)）。

| 工具 | 一句话 |
|---|---|
| `fridge.help` | 渐进式披露参考 blob（枚举/格式/页面/工具选择指南） |
| `fridge.item.get` | 按 item_id 取单条详情 |
| `fridge.item.add` | 新增食材，返回新对象（含生成 item_id） |
| `fridge.item.remove` | 按 item_id 删除单条 |
| `fridge.item.clear_all` | 清空全部（不可撤销） |
| `fridge.stats.summary` | 总数/过期/即将过期/分类计数 |
| `fridge.stats.query` | 按分类或过期状态筛选/发现未知 item_id |
| `fridge.item.list` | 列出食材（可排序/限量） |
| `fridge.item.update` | 按 item_id 局部更新（仅传改的字段） |
| `fridge.pagemanager` | 切换墨水屏页面 target_page 1-5 |
| `fridge.recipe.recommend` | 生成菜谱渲染到食谱页，返回库存快照 |

## 墨水屏联动
- 页面枚举 `EpaperPage` 在 `main/display/epaperdisplay/epaper_display.h`：1=CHAT / 2=FRIDGE_STATS / 3=FOOD_LIST / 4=RECIPE / 5=HOME_PIC。
- 板型实现 `Board::GetEpaperDisplay()` 返回 `EpaperDisplay*`（见 `compact_wifi_board_epaperx.cc`）。
- Fridge MCP 通过 `Board::GetInstance().GetEpaperDisplay()` 拿到屏对象，调用 `SetPage()` / `SetRecipeContent()`。
- `EpaperLabel` 是声明式绘制对象（Text/Rect/Line/Bitmap/...），带 `page` 字段归属页面，见 `epaper_display.h`。

## 常见改动路径
- 加新工具：在 `fridge_mcp.cc::Initialize()` 注册 + 加 `Handle*` 方法（声明放 `fridge_mcp.h`）。参考 `fridge.help` 的瘦描述 + 渐进式披露约定。
- 改字段/枚举：改 `fridge_enum_utils.h`（枚举+转换）+ `fridge_item.{h,cc}`（字段+两套 JSON）+ `fridge.help` 的参考 blob 同步。
- 改持久化：改 `fridge_manager.cc`（NVS key/扫描逻辑）。
