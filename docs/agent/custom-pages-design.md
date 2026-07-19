# 多页面自定义界面 + 脚本化动态元素设计文档 v2

> 状态：**草案 v2** · 日期：2026-07-08
> 目标：墨水屏多页面系统，每个页面可放置静态元素和**脚本驱动动态元素**，通过 MCP 工具由 Hermes 编排，实现无限扩展。

---

## 一、核心架构：三层设计

```
┌──────────────────────────────────────────────────────────┐
│                    Hermes Agent                           │
│  「创建一个B站粉丝数页面」                                 │
│  → 调用 MCP 工具链创建页面 + 绑定 cron 数据源              │
└──────────────────────┬───────────────────────────────────┘
                       │ MCP over WebSocket/HTTP
                       ▼
┌──────────────────────────────────────────────────────────┐
│              xiaozhi-esp32 设备端                         │
│                                                          │
│  ┌─────────────┐   ┌─────────────────┐                  │
│  │ EpaperDisplay│   │ FridgeMcpTools   │                  │
│  │  - ui_labels_│◄──│  page.create     │                  │
│  │  - SetPage() │   │  page.delete     │                  │
│  │  - 定时刷新   │   │  element.add     │                  │
│  └──────┬───────┘   │  element.update  │← Hermes 直接推送  │
│         │           │  element.remove  │                  │
│         ▼           └─────────────────┘                  │
│  ┌─────────────────────────────┐                         │
│  │   LittleFS /custom/         │                         │
│  │   ├── pages.json            │                         │
│  │   ├── page_7.json           │                         │
│  │   └── page_8.json           │                         │
│  └─────────────────────────────┘                         │
└──────────────────────────────────────────────────────────┘
```

### 关键设计决策：动态元素的「脚本」不是跑在 ESP32 上

ESP32 不跑脚本引擎。动态元素的数据来源有两种：

| 模式 | 机制 | 适用场景 |
|---|---|---|
| **A. Hermes 推送** | Hermes cron 定时获取数据 → 调 MCP 工具更新元素文本 | 粉丝数、天气、股价等需联网的数据 |
| **B. 设备端内置** | `TextValue(std::function<String()>)` lambda | 时钟、日期、倒计时等设备本地可算的数据 |

**模式 A 是核心创新**：动态元素本质上就是一个「ID + 当前文本值」，Hermes 通过 `element.update` 工具定期推送新值。ESP32 只负责渲染和持久化，不关心数据从哪来。

---

## 二、页面与元素数据模型

### 2.1 pages.json — 页面注册表

```json
{
  "pages": [
    {"page": 7, "name": "B站粉丝", "created": "2026-07-08T12:00:00"},
    {"page": 8, "name": "倒计时", "created": "2026-07-08T13:00:00"}
  ]
}
```

### 2.2 page_N.json — 单页布局

```json
{
  "page": 7,
  "name": "B站粉丝",
  "elements": [
    {
      "type": "text",
      "id": "title",
      "text": "B站粉丝数",
      "x": 148, "y": 5,
      "font_size": 16, "align": "center"
    },
    {
      "type": "text",
      "id": "fan_count",
      "text": "加载中...",
      "x": 148, "y": 40,
      "font_size": 24, "align": "center",
      "dynamic": true,
      "update_interval": 300
    },
    {
      "type": "rect",
      "id": "box",
      "x": 20, "y": 30, "w": 256, "h": 40, "filled": false
    }
  ]
}
```

### 2.3 元素类型

| type | 说明 | 属性 |
|---|---|---|
| `text` | 静态/动态文本 | text, x, y, font_size, align, max_width, **dynamic**, **update_interval** |
| `rect` | 矩形 | x, y, w, h, filled |
| `line` | 直线 | x1, y1, x2, y2, width |
| `image` | 位图 | name, x, y, w, h |

> `dynamic: true` + `update_interval`（秒）标记此元素需要定期更新。设备端不主动获取数据——由 Hermes 或设备定时器触发 `element.update` 更新文本值。

---

## 三、MCP 工具设计（10 个工具）

### 3.1 页面管理（4 个）

| 工具 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `fridge.page.create` | `name`(string, 必填) | `{page, name}` | 创建页面，编号 7-15 自动分配 |
| `fridge.page.delete` | `page`(int, 必填) | `{removed}` | 删除页面 + 布局文件 |
| `fridge.page.list` | 无 | `[{page, name, elements}]` | 列出所有自定义页面 |
| `fridge.page.rename` | `page`(int), `name`(string) | `{page, name}` | 重命名 |

### 3.2 元素管理（5 个）

| 工具 | 参数 | 返回 | 说明 |
|---|---|---|---|
| `fridge.page.element.add` | `page`, `type`, `id`, `x`, `y`, `text?`, `font_size?`, `align?`, `w?`, `h?`, `filled?`, `x1?`, `y1?`, `x2?`, `y2?`, `width?`, `name?`, `dynamic?`, `update_interval?`, `refresh?` | `{page, id}` | 添加元素到页面 |
| `fridge.page.element.update` | `page`, `id`, `text`(string), `refresh?`(bool) | `{page, id}` | **核心工具**：更新动态元素文本 |
| `fridge.page.element.remove` | `page`, `id`, `refresh?` | `{removed}` | 删除元素 |
| `fridge.page.element.list` | `page` | `[元素列表]` | 列出页面所有元素 |
| `fridge.page.clear` | `page`, `refresh?` | `{removed_count}` | 清空页面 |

### 3.3 页面切换（扩展现有）

`fridge.pagemanager` 扩展支持 1-15：
- 1-5：系统页面（不变）
- 6：默认画布（不变）
- 7-15：自定义页面

### 3.4 `element.update` — Hermes 的核心接口

```json
// Hermes 调用示例：更新粉丝数
{
  "tool": "fridge.page.element.update",
  "args": {
    "page": 7,
    "id": "fan_count",
    "text": "12,345",
    "refresh": true
  }
}
```

设备端收到后：
1. 找到 page 7 中 id 为 `fan_count` 的 EpaperLabel
2. 更新其 `text` 值（创建一个新的 TextValue 包含新字符串）
3. 若 `refresh: true` 则立即刷新屏幕

---

## 四、动态元素的两条更新路径

### 路径 A：Hermes 定时推送（外部数据源）

```
Hermes cron job (每 5 分钟)
    │
    ├─ 1. 调用 API 获取 B站粉丝数
    │     curl https://api.bilibili.com/x/relation/stat?vmid=XXX
    │
    ├─ 2. 调用 MCP 工具更新墨水屏
    │     fridge.page.element.update {
    │       page: 7, id: "fan_count",
    │       text: "12,345", refresh: true
    │     }
    │
    └─ 3. 设备刷新墨水屏显示新值
```

**Hermes 可以创建任意数据源的动态元素**：
- 粉丝数 → cron 调 bilibili API → element.update
- 天气 → cron 调天气 API → element.update
- 股价 → cron 调股价 API → element.update
- 生日倒计时 → cron 计算天数 → element.update
- 任何 Hermes 能获取的数据 → cron → element.update

### 路径 B：设备端内置动态类型（本地计算）

对于时钟、日期等设备本地可算的动态元素，无需 Hermes 推送：

| 内置动态类型 | fmt 示例 | 设备端实现 |
|---|---|---|
| `clock` | `"HH:MM"`、`"HH:MM:SS"` | `TextValue(lambda)` 内部 strftime |
| `date` | `"YYYY-MM-DD"` | 同上 |
| `weekday` | `"星期一"` | 同上 |
| `countdown` | `目标时间戳` | 设备定时计算剩余天数/小时 |

**element.add 时通过 `dynamic_type` 参数指定**：
```json
{
  "type": "text",
  "id": "clock",
  "dynamic_type": "clock",
  "fmt": "HH:MM",
  "x": 148, "y": 10,
  "font_size": 24, "align": "center"
}
```

设备端创建 label 时：
```cpp
if (dynamic_type == "clock") {
    label->text = [fmt]() -> String {
        time_t now; time(&now);
        struct tm* t = localtime(&now);
        char buf[16];
        strftime(buf, sizeof(buf), fmt.c_str(), t);
        return String(buf);
    };
}
```

---

## 五、设备端实现方案

### 5.1 文件变更清单

| 文件 | 变更 | 说明 |
|---|---|---|
| `Fridge/fridge_mcp.h` | 修改 | 新增 Handle 声明 |
| `Fridge/fridge_mcp.cc` | 修改 | 新增 10 个工具；`SavePageLayout`/`LoadPageLayout` 多页面版 |
| `Fridge/custom_page_manager.h` | **新增** | 页面注册表管理（pages.json CRUD） |
| `Fridge/custom_page_manager.cc` | **新增** | 同上 |
| `epaperdisplay/epaper_display.h` | 修改 | `SetPage()` 支持 7-15；动态定时器 |
| `epaperdisplay/epaper_display.cc` | 修改 | 动态元素定时刷新；`UpdateLabelText()` 方法 |
| `local_control.cc` | 修改 | LittleFS 挂载后加载 pages.json + 所有页面 |

### 5.2 CustomPageManager 核心逻辑

```cpp
class CustomPageManager {
public:
    static CustomPageManager& GetInstance();

    // 页面管理
    int CreatePage(const std::string& name);      // 分配 7-15，写 pages.json
    bool DeletePage(int page);                    // 删布局文件 + pages.json 条目
    std::string ListPages();                      // 返回 JSON 数组
    bool RenamePage(int page, const std::string& name);

    // 元素管理
    bool AddElement(int page, const ElementDef& def);
    bool UpdateElementText(int page, const std::string& id, const std::string& text);
    bool RemoveElement(int page, const std::string& id);
    std::string ListElements(int page);
    bool ClearPage(int page);

    // 持久化
    void SavePageLayout(int page);
    void LoadPageLayout(int page);
    void LoadAllPages();   // 启动时调用

private:
    std::string GetLayoutPath(int page);  // "/custom/page_7.json"
    std::string GetRegistryPath();        // "/custom/pages.json"
};
```

### 5.3 动态元素定时刷新

```cpp
// epaper_display.h 新增
esp_timer_handle_t dynamic_timer_ = nullptr;
void StartDynamicTimer(int interval_sec);
void StopDynamicTimer();

// epaper_display.cc
void EpaperDisplay::StartDynamicTimer(int interval_sec) {
    if (dynamic_timer_) {
        esp_timer_stop(dynamic_timer_);
        esp_timer_delete(dynamic_timer_);
    }
    esp_timer_create_args_t args = {
        .callback = [](void* arg) {
            auto* self = (EpaperDisplay*)arg;
            // 遍历当前页的 label，调用 TextValue::operator()()
            // 若值变化则标记 dirty → 局部刷新
            self->UpdateDynamicLabels();
        },
        .arg = this,
        .name = "dyn_refresh"
    };
    esp_timer_create(&args, &dynamic_timer_);
    esp_timer_start_periodic(dynamic_timer_, interval_sec * 1000000ULL);
}
```

---

## 六、Hermes 侧：脚本化动态元素

### 6.1 Hermes cron job 模式

Hermes 可以创建任意动态元素，只需一个 cron job：

```
用户: "帮我做一个B站粉丝数页面，每5分钟更新"

Hermes 执行:
1. fridge.page.create { name: "B站粉丝" }
   → { page: 7 }

2. fridge.page.element.add {
     page: 7, type: "text", id: "title",
     text: "B站粉丝数", x: 148, y: 5,
     font_size: 16, align: "center"
   }

3. fridge.page.element.add {
     page: 7, type: "text", id: "fan_count",
     text: "加载中...", x: 148, y: 40,
     font_size: 24, align: "center",
     dynamic: true
   }

4. 创建 Hermes cron job (每5分钟):
   - 调 bilibili API 获取粉丝数
   - 调 fridge.page.element.update {
       page: 7, id: "fan_count",
       text: "12,345", refresh: true
     }

5. fridge.pagemanager { target_page: 7 }
```

### 6.2 倒计时 / 正向计时

```
用户: "做一个距离2026年圣诞节的倒计时页面"

Hermes 执行:
1. fridge.page.create { name: "圣诞倒计时" }
   → { page: 8 }

2. fridge.page.element.add {
     page: 8, type: "text", id: "label",
     text: "距离圣诞节还有", x: 148, y: 10,
     font_size: 16, align: "center"
   }

3. fridge.page.element.add {
     page: 8, type: "text", id: "days",
     text: "170", x: 148, y: 40,
     font_size: 32, align: "center",
     dynamic: true
   }

4. fridge.page.element.add {
     page: 8, type: "text", id: "unit",
     text: "天", x: 148, y: 75,
     font_size: 16, align: "center"
   }

5. 创建 Hermes cron job (每天更新):
   - 计算距 2026-12-25 的天数
   - 调 fridge.page.element.update { page: 8, id: "days", text: "169" }
```

### 6.3 复合仪表盘

```
用户: "做一个综合仪表盘：时钟+冰箱统计+天气"

Hermes 创建 page 9:
  - clock (内置 dynamic_type: clock, 设备自己刷新)
  - fridge_stats (cron: 每10分钟读 fridge.stats.summary → 更新文本)
  - weather (cron: 每30分钟调天气 API → 更新文本)
```

---

## 七、网页端扩展

### 7.1 新增「页面管理」面板

画布 Tab 顶部增加页面选择器：

```
[默认画布 ▼]  [+ 新建]  [删除]  [重命名]
```

- 下拉列表从 `fridge.page.list` 加载
- 切换页面 → 加载该页元素到编辑器
- 新建页面 → 调 `fridge.page.create`
- 删除 → 调 `fridge.page.delete`

### 7.2 动态元素标记

元素列表中动态元素显示特殊图标 + 最后更新时间：

```
🕐 clock (每60秒) — 值: 14:23
📡 fan_count (每300秒) — 值: 12,345 · 上次更新: 2分钟前
```

### 7.3 JS 新增调用

```javascript
// 页面管理
api.callTool('fridge.page.create', { name: 'B站粉丝' });
api.callTool('fridge.page.list', {});
api.callTool('fridge.page.delete', { page: 7 });

// 元素管理（所有 add 调用带 page 参数）
api.callTool('fridge.page.element.add', {
  page: state.canvas.currentPage,
  type: 'text', id: 'fan_count',
  text: '加载中...', x: 148, y: 40,
  font_size: 24, align: 'center',
  dynamic: true, update_interval: 300,
  refresh: true
});

// 手动更新动态元素值
api.callTool('fridge.page.element.update', {
  page: 7, id: 'fan_count',
  text: '12,345', refresh: true
});
```

---

## 八、实现优先级

| 阶段 | 内容 | 预计工作量 |
|---|---|---|
| **P1** | CustomPageManager + 多页面持久化 | 中（C++ 新文件） |
| **P2** | 10 个 MCP 工具注册 + Handler | 中（fridge_mcp.cc） |
| **P3** | 设备端内置动态类型（clock/date） | 小（TextValue lambda） |
| **P4** | pagemanager 扩展 7-15 | 小 |
| **P5** | 动态元素定时器 | 小（esp_timer） |
| **P6** | 网页端页面管理 + 动态标记 | 中（HTML/JS） |
| **P7** | Hermes 示范 cron（粉丝数/倒计时） | 小（Hermes 原生能力） |

---

## 九、与 v1 设计的主要区别

| 方面 | v1 | v2（本文档） |
|---|---|---|
| 动态元素 | 仅内置类型（clock/date/weekday） | **内置类型 + Hermes 脚本推送** |
| 数据来源 | 设备端自算 | **设备自算 + Hermes cron 任意数据源** |
| 扩展性 | 固定几种动态类型 | **无限**（Hermes 能获取什么就能显示什么） |
| ESP32 复杂度 | 高（需在设备端实现多种动态逻辑） | **低**（只做渲染 + element.update 接口） |
| 核心工具 | canvas.add_dynamic（设备端解析） | **element.update**（Hermes 推送值） |

> v2 的核心思想：**ESP32 是哑显示器，Hermes 是智能控制器**。ESP32 只负责持久化布局 + 渲染文本，所有复杂逻辑（API 调用、计算、数据格式化）在 Hermes 侧完成，通过 `element.update` 推送最终文本到屏幕。
