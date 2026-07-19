#include "fridge_mcp.h"
#include "board.h"
#include "display/epaperdisplay/epaper_display.h"
#include "custom_page_manager.h"
#include <esp_log.h>
#include <sstream>
#include <cstring>
#include <cstdio>
#include <unistd.h>

static const char* TAG = "FridgeMCP";

// Canvas 布局持久化文件
static const char* CANVAS_LAYOUT_FILE = "/canvas/layout.json";

// 前向声明（实现在后面的 namespace 块中）
static void SaveCanvasLayout();
static void LoadCanvasLayout();

namespace {

std::string EscapeJsonString(const std::string& input) {
    std::string output;
    output.reserve(input.size() + 16);
    for (char ch : input) {
        switch (ch) {
            case '\\': output += "\\\\"; break;
            case '"': output += "\\\""; break;
            case '\n': output += "\\n"; break;
            case '\r': break;
            case '\t': output += "\\t"; break;
            default: output += ch; break;
        }
    }
    return output;
}

std::string BuildRecipeDisplayText(const std::string& mode,
                                   const std::string& dish_name,
                                   const std::string& summary,
                                   const std::string& required_ingredients,
                                   const std::string& extra_ingredients,
                                   const std::string& cooking_time) {
    (void)mode;
    std::string display_text = dish_name;
    if (!cooking_time.empty()) {
        display_text += "（" + cooking_time + "）";
    }

    if (!summary.empty()) {
        display_text += "\n推荐: " + summary;
    }

    if (!required_ingredients.empty()) {
        display_text += "\n需要: " + required_ingredients;
    }

    display_text += "\n采购: ";
    display_text += extra_ingredients.empty() ? "无" : extra_ingredients;

    return display_text;
}

// 将逗号分隔的食材字符串拆成列表（去空格、去空项）
std::vector<std::string> SplitIngredients(const std::string& input) {
    std::vector<std::string> result;
    std::string current;
    for (size_t i = 0; i < input.size(); ++i) {
        char ch = input[i];
        if (ch == ',') {
            size_t start = current.find_first_not_of(" \t");
            size_t end = current.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                result.push_back(current.substr(start, end - start + 1));
            }
            current.clear();
        } else if (ch == (char)0xE3 && i + 1 < input.size() && input[i+1] == (char)0x80 && i + 2 < input.size() && input[i+2] == (char)0x81) {
            // UTF-8 中文逗号 ，= 0xE3 0x80 0x81
            size_t start = current.find_first_not_of(" \t");
            size_t end = current.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                result.push_back(current.substr(start, end - start + 1));
            }
            current.clear();
            i += 2;  // 跳过剩余两个字节
        } else {
            current += ch;
        }
    }
    size_t start = current.find_first_not_of(" \t");
    size_t end = current.find_last_not_of(" \t");
    if (start != std::string::npos && end != std::string::npos) {
        result.push_back(current.substr(start, end - start + 1));
    }
    return result;
}

// 检查某食材名是否在冰箱库存中（子串匹配，忽略大小写）
bool IsIngredientInFridge(const std::string& ingredient, const std::vector<FridgeItem>& fridge_items) {
    // 将 ingredient 转小写
    std::string ing_lower = ingredient;
    for (auto& c : ing_lower) {
        if (c >= 'A' && c <= 'Z') c += 32;
    }
    for (const auto& item : fridge_items) {
        // 将 item.name 转小写
        std::string name_lower = item.name;
        for (auto& c : name_lower) {
            if (c >= 'A' && c <= 'Z') c += 32;
        }
        // 双向子串匹配：冰箱有"鸡蛋"，需要"鸡蛋"；或冰箱有"牛肉片"，需要"牛肉"
        if (name_lower.find(ing_lower) != std::string::npos ||
            ing_lower.find(name_lower) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// 对比 required_ingredients 和冰箱库存，返回缺失食材（逗号分隔）
std::string ComputeMissingIngredients(const std::string& required_ingredients,
                                      const std::vector<FridgeItem>& fridge_items) {
    auto required = SplitIngredients(required_ingredients);
    std::string missing;
    for (const auto& ing : required) {
        if (!IsIngredientInFridge(ing, fridge_items)) {
            if (!missing.empty()) missing += "、";
            missing += ing;
        }
    }
    return missing;
}

std::string BuildInventorySnapshotJson(const std::vector<FridgeItem>& items) {
    std::string json = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) {
            json += ",";
        }
        json += items[i].ToMcpJson();
    }
    json += "]";
    return json;
}

}  // namespace

void FridgeMcpTools::Initialize() {
    auto& mcp_server = McpServer::GetInstance();
    
    // 工具 1: 获取食材详细信息
    PropertyList get_item_props;
    get_item_props.AddProperty(Property("item_id", kPropertyTypeInteger));
    
    mcp_server.AddTool("fridge.item.get",
        "Get detailed information about a fridge item. (获取冰箱食材的详细信息)\n"
        "Returns: item_id, name, category, quantity, unit, storage_state, package_state, "
        "add_time, expire_time, remaining_days, alert_level",
        get_item_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleGetItem(properties);
        });
    
    // 工具 2: 添加食材到冰箱
    PropertyList add_item_props;
    add_item_props.AddProperty(Property("name", kPropertyTypeString));
    add_item_props.AddProperty(Property("category", kPropertyTypeString,
        std::string("vegetable|fruit|meat|egg|dairy|cooked|seasoning|beverage|quick|other")));
    add_item_props.AddProperty(Property("quantity", kPropertyTypeInteger));
    add_item_props.AddProperty(Property("unit", kPropertyTypeString));
    add_item_props.AddProperty(Property("expire_time", kPropertyTypeString,
        std::string("Format: YYYY-MM-DD HH:MM:SS (e.g., 2025-01-15 12:00:00)")));
    add_item_props.AddProperty(Property("storage_state", kPropertyTypeString, std::string("Fresh")));
    
    mcp_server.AddTool("fridge.item.add",
        "Add a new item to the fridge. (添加新食材到冰箱)\n"
        "Category options: vegetable(蔬菜), fruit(水果), meat(肉类), egg(蛋类), dairy(乳制品), "
        "cooked(熟食), seasoning(调味料), beverage(饮料), quick(速食), other(其他)\n"
        "Storage state options: Fresh(冷藏，默认), Frozen(冷冻)\n"
        "Expire time format: YYYY-MM-DD HH:MM:SS (NVS存储为Unix时间戳, 显示时转为可读格式)",
        add_item_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleAddItem(properties);
        });
    
    // 工具 3: 删除食材从冰箱
    PropertyList remove_item_props;
    remove_item_props.AddProperty(Property("item_id", kPropertyTypeInteger));
    
    mcp_server.AddTool("fridge.item.remove",
        "Remove an item from the fridge. (从冰箱删除食材)\n"
        "Requires: item_id (the ID of the item to remove)",
        remove_item_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleRemoveItem(properties);
        });
    
    // 工具 4: 清空冰箱中的所有食材
    PropertyList clear_all_props;  // 无参数
    
    mcp_server.AddTool("fridge.item.clear_all",
        "Clear all items from the fridge. (清空冰箱中的所有食材)\n"
        "WARNING: This action cannot be undone. All items will be permanently removed.",
        clear_all_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleClearAll(properties);
        });
    
    // 工具 5: 获取冰箱统计摘要
    PropertyList stats_props;  // 无参数
    
    mcp_server.AddTool("fridge.stats.summary",
        "Get a summary of fridge statistics. (获取冰箱统计摘要)\n"
        "Returns: total_items, expired_items, expiring_soon_items, and category_count breakdown",
        stats_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleStatsSummary(properties);
        });
    
    // 工具 6: 条件查询冰箱食材
    PropertyList query_props;
    query_props.AddProperty(Property("category", kPropertyTypeString,
        std::string("(optional) 筛选特定分类: vegetable|fruit|meat|egg|dairy|cooked|seasoning|beverage|quick|other. "
                    "若要查询所有分类，请忽略此参数或设为 'all'。")));
    query_props.AddProperty(Property("filter", kPropertyTypeString,
        std::string("(optional) 状态过滤: all(全部), expired(已过期), expiring_soon(即将过期)")));
    query_props.AddProperty(Property("expiring_days", kPropertyTypeInteger, 7));

    mcp_server.AddTool("fridge.stats.query",
        "Search and list fridge items by criteria. (按条件搜索并列出冰箱食材)\n"
        "Use this tool when you need to: 1. Find items by category. 2. List all expired or expiring items. "
        "3. Check stock levels for specific food groups. 4. Get item IDs when they are unknown.\n"
        "当需要执行以下操作时使用：1. 按类别查找食材。2. 列出所有过期或即将过期的物品。3. 检查特定类别的库存。4. 在不知道ID时获取食材列表。",
        query_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleStatsQuery(properties);
        });
    
    // 工具 7: 列出所有食材
    PropertyList list_props;
    list_props.AddProperty(Property("category", kPropertyTypeString,
        std::string("(optional) 筛选特定分类: vegetable|fruit|meat|egg|dairy|cooked|seasoning|beverage|quick|other")));
    list_props.AddProperty(Property("limit", kPropertyTypeInteger, 0));
    list_props.AddProperty(Property("sort_by", kPropertyTypeString,
        std::string("(optional) 排序字段: add_time(添加时间), expire_time(过期时间), name(名称)")));
    list_props.AddProperty(Property("order", kPropertyTypeString,
        std::string("(optional) 排序顺序: asc(升序), desc(降序, 默认)")));

    mcp_server.AddTool("fridge.item.list",
        "List all items in the fridge with optional filters and sorting. (列出冰箱中的所有食材，支持筛选和排序)\n"
        "Returns a list of items with their full details.",
        list_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleItemList(properties);
        });
    
    // 工具 8: 更新食材信息
    PropertyList update_item_props;
    update_item_props.AddProperty(Property("item_id", kPropertyTypeInteger));
    update_item_props.AddProperty(Property("name", kPropertyTypeString));
    update_item_props.AddProperty(Property("category", kPropertyTypeString,
        std::string("(optional) vegetable|fruit|meat|egg|dairy|cooked|seasoning|beverage|quick|other")));
    update_item_props.AddProperty(Property("quantity", kPropertyTypeInteger));
    update_item_props.AddProperty(Property("unit", kPropertyTypeString));
    update_item_props.AddProperty(Property("expire_time", kPropertyTypeString,
        std::string("(optional) Format: YYYY-MM-DD HH:MM:SS")));
    update_item_props.AddProperty(Property("storage_state", kPropertyTypeString, std::string("(optional) Fresh|Frozen")));

    mcp_server.AddTool("fridge.item.update",
        "Update information of an existing item in the fridge. (更新冰箱中现有食材的信息)\n"
        "Provide only the fields you want to change.",
        update_item_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleItemUpdate(properties);
        });
    
    // 工具 9: 冰箱显示页面管理
    PropertyList page_props;
    page_props.AddProperty(Property("target_page", kPropertyTypeInteger, 1, 15));

    mcp_server.AddTool("fridge.pagemanager",
        "Switch the e-paper display page. Pages 1-5 are built-in (Chat/Stats/List/Recipe/HomePic). "
        "Page 6 is the default canvas. Pages 7-15 are user-created custom pages. "
        "当需要查看冰箱统计、食材列表、AI菜谱或自定义页面时切换页面。",
        page_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandlePageManager(properties);
        });

    // 工具 10: AI 食谱推荐显示
    PropertyList recipe_props;
    recipe_props.AddProperty(Property("recommendation_mode", kPropertyTypeString));
    recipe_props.AddProperty(Property("dish_name", kPropertyTypeString));
    recipe_props.AddProperty(Property("summary", kPropertyTypeString, std::string("")));
    recipe_props.AddProperty(Property("required_ingredients", kPropertyTypeString));
    recipe_props.AddProperty(Property("extra_ingredients", kPropertyTypeString, std::string("")));
    recipe_props.AddProperty(Property("cooking_time", kPropertyTypeString, std::string("20分钟")));
    recipe_props.AddProperty(Property("switch_page", kPropertyTypeBoolean, true));

    mcp_server.AddTool("fridge.recipe.recommend",
        "Recommend a recipe based on the current fridge inventory and display it on the e-paper recipe page. "
        "(基于当前冰箱库存推荐食谱，并显示到墨水屏食谱页)\n"
        "You must first determine the recommendation mode from the user's conversation before calling this tool:\n"
        "1. `fridge_only`: only use ingredients already in the fridge. The device will check the fridge and "
        "REJECT the call if any required ingredient is missing, telling you what to buy.\n"
        "2. `mixed_purchase`: use some fridge ingredients and buy some extra ingredients. The device will "
        "auto-detect missing ingredients and fill them into extra_ingredients for you.\n"
        "Fill the recipe in this normalized format: recommendation mode, dish name, brief recommendation reason, "
        "required ingredients, extra ingredients to buy when needed, and cooking time. "
        "The device will return the current fridge inventory snapshot together with the rendered recipe result.",
        recipe_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleRecipeRecommend(properties);
        });
    
    // ==================== Canvas 工具 (page 6) ====================

    // 工具 11: canvas.add_text — 在画布页放置文本
    PropertyList canvas_text_props;
    canvas_text_props.AddProperty(Property("id", kPropertyTypeString));
    canvas_text_props.AddProperty(Property("text", kPropertyTypeString));
    canvas_text_props.AddProperty(Property("x", kPropertyTypeInteger));
    canvas_text_props.AddProperty(Property("y", kPropertyTypeInteger));
    canvas_text_props.AddProperty(Property("font_size", kPropertyTypeInteger, 16));
    canvas_text_props.AddProperty(Property("align", kPropertyTypeString, std::string("left")));
    canvas_text_props.AddProperty(Property("max_width", kPropertyTypeInteger, 276));
    canvas_text_props.AddProperty(Property("refresh", kPropertyTypeBoolean, false));

    mcp_server.AddTool("fridge.canvas.add_text",
        "Place a text element on the canvas page (page 6). Screen is 296x128 pixels. "
        "font_size: 12 or 16 (supports Chinese). align: left|center|right. "
        "Set refresh=true to immediately update the display, or batch multiple calls then call canvas.refresh.",
        canvas_text_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleCanvasAddText(properties);
        });

    // 工具 12: canvas.add_rect — 放置矩形
    PropertyList canvas_rect_props;
    canvas_rect_props.AddProperty(Property("id", kPropertyTypeString));
    canvas_rect_props.AddProperty(Property("x", kPropertyTypeInteger));
    canvas_rect_props.AddProperty(Property("y", kPropertyTypeInteger));
    canvas_rect_props.AddProperty(Property("w", kPropertyTypeInteger));
    canvas_rect_props.AddProperty(Property("h", kPropertyTypeInteger));
    canvas_rect_props.AddProperty(Property("filled", kPropertyTypeBoolean, false));
    canvas_rect_props.AddProperty(Property("refresh", kPropertyTypeBoolean, false));

    mcp_server.AddTool("fridge.canvas.add_rect",
        "Place a rectangle on the canvas page. Use filled=true for solid, false for outline.",
        canvas_rect_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleCanvasAddRect(properties);
        });

    // 工具 13: canvas.add_line — 放置线条
    PropertyList canvas_line_props;
    canvas_line_props.AddProperty(Property("id", kPropertyTypeString));
    canvas_line_props.AddProperty(Property("x1", kPropertyTypeInteger));
    canvas_line_props.AddProperty(Property("y1", kPropertyTypeInteger));
    canvas_line_props.AddProperty(Property("x2", kPropertyTypeInteger));
    canvas_line_props.AddProperty(Property("y2", kPropertyTypeInteger));
    canvas_line_props.AddProperty(Property("width", kPropertyTypeInteger, 1));
    canvas_line_props.AddProperty(Property("refresh", kPropertyTypeBoolean, false));

    mcp_server.AddTool("fridge.canvas.add_line",
        "Place a line on the canvas page from (x1,y1) to (x2,y2).",
        canvas_line_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleCanvasAddLine(properties);
        });

    // 工具 15: canvas.clear — 清空画布（或删除单个元素）
    PropertyList canvas_clear_props;
    canvas_clear_props.AddProperty(Property("refresh", kPropertyTypeBoolean, true));
    canvas_clear_props.AddProperty(Property("id", kPropertyTypeString, std::string("")));

    mcp_server.AddTool("fridge.canvas.clear",
        "Clear all elements from the canvas page. Default refresh=true. "
        "Also supports removing a single element: pass id parameter to remove only that element.",
        canvas_clear_props,
        [this](const PropertyList& properties) -> ReturnValue {
            // 如果传了 id 参数且非空，走 remove 逻辑；否则清空全部
            bool has_id = false;
            std::string id_val;
            try {
                id_val = properties["id"].value<std::string>();
                if (!id_val.empty()) has_id = true;
            } catch (...) {
                has_id = false;
            }
            if (has_id) {
                return HandleCanvasRemove(properties);
            }
            return HandleCanvasClear(properties);
        });

    // 工具 16: canvas.add_image — 从文件加载图片到画布
    PropertyList canvas_image_props;
    canvas_image_props.AddProperty(Property("id", kPropertyTypeString));
    canvas_image_props.AddProperty(Property("name", kPropertyTypeString));
    canvas_image_props.AddProperty(Property("x", kPropertyTypeInteger));
    canvas_image_props.AddProperty(Property("y", kPropertyTypeInteger));
    canvas_image_props.AddProperty(Property("w", kPropertyTypeInteger));
    canvas_image_props.AddProperty(Property("h", kPropertyTypeInteger));
    canvas_image_props.AddProperty(Property("refresh", kPropertyTypeBoolean, false));

    mcp_server.AddTool("fridge.canvas.add_image",
        "Load a bitmap image from canvas storage (uploaded via POST /api/canvas_image?name=xxx) "
        "and place it on the canvas page. The image must be a 1-bpp (black/white) raw bitmap "
        "with the given width and height. Use the upload API first to transfer the file, "
        "then call this tool to display it.",
        canvas_image_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleCanvasAddImage(properties);
        });

    // ==================== 自定义页面工具 (page 7-15) ====================

    // 工具 19: page.create — 创建自定义页面
    PropertyList page_create_props;
    page_create_props.AddProperty(Property("name", kPropertyTypeString));

    mcp_server.AddTool("fridge.page.create",
        "Create a custom e-paper page (page number 7-15). Returns the assigned page number. "
        "Custom pages persist across reboots. Use fridge.page.element.add to place elements.",
        page_create_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandlePageCreate(properties);
        });

    // 工具 20: page.delete — 删除自定义页面
    PropertyList page_delete_props;
    page_delete_props.AddProperty(Property("page", kPropertyTypeInteger, 7, 15));

    mcp_server.AddTool("fridge.page.delete",
        "Delete a custom page and all its elements. Built-in pages (1-6) cannot be deleted.",
        page_delete_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandlePageDelete(properties);
        });

    // 工具 21: page.list — 列出所有页面
    mcp_server.AddTool("fridge.page.list",
        "List all pages: built-in (1-6) and custom (7-15) with their names and element counts.",
        PropertyList(),
        [this](const PropertyList& properties) -> ReturnValue {
            return HandlePageList(properties);
        });

    // 工具 22: page.rename — 重命名自定义页面
    PropertyList page_rename_props;
    page_rename_props.AddProperty(Property("page", kPropertyTypeInteger, 7, 15));
    page_rename_props.AddProperty(Property("name", kPropertyTypeString));

    mcp_server.AddTool("fridge.page.rename",
        "Rename a custom page. Only applies to custom pages (7-15).",
        page_rename_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandlePageRename(properties);
        });

    // 工具 23: page.element.add — 在指定页面添加元素
    PropertyList elem_add_props;
    elem_add_props.AddProperty(Property("page", kPropertyTypeInteger, 7, 15));
    elem_add_props.AddProperty(Property("id", kPropertyTypeString));
    elem_add_props.AddProperty(Property("type", kPropertyTypeString,
        std::string("text|rect|line")));
    elem_add_props.AddProperty(Property("x", kPropertyTypeInteger, 0, 295));
    elem_add_props.AddProperty(Property("y", kPropertyTypeInteger, 0, 127));
    elem_add_props.AddProperty(Property("text", kPropertyTypeString, std::string("")));
    elem_add_props.AddProperty(Property("font_size", kPropertyTypeInteger, 16));
    elem_add_props.AddProperty(Property("align", kPropertyTypeString, std::string("left")));
    elem_add_props.AddProperty(Property("w", kPropertyTypeInteger, 40));
    elem_add_props.AddProperty(Property("h", kPropertyTypeInteger, 30));
    elem_add_props.AddProperty(Property("filled", kPropertyTypeBoolean, false));
    elem_add_props.AddProperty(Property("x1", kPropertyTypeInteger, 0));
    elem_add_props.AddProperty(Property("y1", kPropertyTypeInteger, 0));
    elem_add_props.AddProperty(Property("x2", kPropertyTypeInteger, 0));
    elem_add_props.AddProperty(Property("y2", kPropertyTypeInteger, 0));
    elem_add_props.AddProperty(Property("width", kPropertyTypeInteger, 1));
    elem_add_props.AddProperty(Property("max_width", kPropertyTypeInteger, 276));
    elem_add_props.AddProperty(Property("dynamic", kPropertyTypeBoolean, false));
    elem_add_props.AddProperty(Property("dynamic_type", kPropertyTypeString, std::string("")));
    elem_add_props.AddProperty(Property("refresh", kPropertyTypeBoolean, false));

    mcp_server.AddTool("fridge.page.element.add",
        "Add an element to a custom page (7-15). type: text/rect/line. "
        "Set dynamic=true for elements whose text will be updated via element.update "
        "(e.g. clock, fan count, weather). Screen is 296x128. "
        "text params: text,font_size,align,max_width. rect params: w,h,filled. "
        "line params: x1,y1,x2,y2,width. Set refresh=true to immediately update display. "
        "dynamic_type (optional, for text only): device-side auto-updating value. "
        "Supported: clock(HH:MM), date(YYYY-MM-DD 周X), datetime(MM-DD HH:MM), "
        "cpu_temp(XX.X°C), heap(Heap: XXKB), uptime(Up: Xd Xh Xm). "
        "When dynamic_type is set, the text param is ignored and the device updates "
        "the value automatically every second.",
        elem_add_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleElementAdd(properties);
        });

    // 工具 24: page.element.update — 更新动态元素文本（核心）
    PropertyList elem_update_props;
    elem_update_props.AddProperty(Property("page", kPropertyTypeInteger, 7, 15));
    elem_update_props.AddProperty(Property("id", kPropertyTypeString));
    elem_update_props.AddProperty(Property("text", kPropertyTypeString));
    elem_update_props.AddProperty(Property("refresh", kPropertyTypeBoolean, true));

    mcp_server.AddTool("fridge.page.element.update",
        "Update the text of a dynamic element on a custom page. "
        "This is the core mechanism for Hermes to push dynamic data "
        "(fan counts, weather, countdowns, stock prices, etc.) to the e-paper display. "
        "Call this from a scheduled cron job to keep the display updated. "
        "Set refresh=true (default) to immediately refresh the screen.",
        elem_update_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleElementUpdate(properties);
        });

    // 工具 25: page.element.remove — 删除元素
    PropertyList elem_remove_props;
    elem_remove_props.AddProperty(Property("page", kPropertyTypeInteger, 7, 15));
    elem_remove_props.AddProperty(Property("id", kPropertyTypeString));
    elem_remove_props.AddProperty(Property("refresh", kPropertyTypeBoolean, false));

    mcp_server.AddTool("fridge.page.element.remove",
        "Remove an element from a custom page by id.",
        elem_remove_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleElementRemove(properties);
        });

    // 工具 26: page.element.list — 列出页面所有元素
    PropertyList elem_list_props;
    elem_list_props.AddProperty(Property("page", kPropertyTypeInteger, 7, 15));

    mcp_server.AddTool("fridge.page.element.list",
        "List all elements on a custom page with their properties.",
        elem_list_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleElementList(properties);
        });

    // 工具 27: page.clear — 清空自定义页面
    PropertyList page_clear_props;
    page_clear_props.AddProperty(Property("page", kPropertyTypeInteger, 7, 15));
    page_clear_props.AddProperty(Property("refresh", kPropertyTypeBoolean, true));

    mcp_server.AddTool("fridge.page.clear",
        "Clear all elements from a custom page. The page itself is not deleted.",
        page_clear_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandlePageClear(properties);
        });

    ESP_LOGI(TAG, "FridgeMcpTools initialized with 24 tools (10 fridge + 5 canvas + 9 custom page)");
    // 注意：LoadCanvasLayout() 不在这里调用，因为 LittleFS 还没挂载
    // 由 LocalControl::MountCanvasStorage() 挂载后调用 LoadCanvasLayout()
    // 自定义页面也由 RestoreCanvasLayout() 一并恢复
}

// 在 LittleFS 挂载后恢复 canvas 布局
void FridgeMcpTools::RestoreCanvasLayout() {
    LoadCanvasLayout();
    // 恢复自定义页面布局
    CustomPageManager::GetInstance().LoadAllPages();
}

ReturnValue FridgeMcpTools::HandleGetItem(const PropertyList& properties) {
    try {
        ItemId item_id = properties["item_id"].value<int>();
        
        auto& fridge = FridgeManager::GetInstance();
        FridgeItem item = fridge.GetItem(item_id);
        
        // 检查是否找到该食材
        if (item.id == 0) {
            return std::string("Item not found");
        }
        
        // 使用 FridgeItem 的 MCP 专用转换函数
        std::string result_str = item.ToMcpJson();
        ESP_LOGI(TAG, "[DEBUG] fridge.item.get result: %s", result_str.c_str());
        
        ESP_LOGI(TAG, "Retrieved item %lu: %s", item_id, item.name.c_str());
        return result_str;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error getting item: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleAddItem(const PropertyList& properties) {
    try {
        // 从 MCP 属性中提取字段
        std::string name = properties["name"].value<std::string>();
        std::string category_str = properties["category"].value<std::string>();
        int quantity_int = properties["quantity"].value<int>();
        float quantity = static_cast<float>(quantity_int);
        std::string unit = properties["unit"].value<std::string>();
        std::string expire_time_str = properties["expire_time"].value<std::string>();
        
        // 获取storage_state，需要检查是否存在该属性
        std::string storage_state_str = "Fresh";  // 默认值
        try {
            storage_state_str = properties["storage_state"].value<std::string>();
        } catch (...) {
            // 属性不存在，使用默认值
        }
        
        // 转换字符串为枚举值
        ItemCategory category = StringToItemCategory(category_str);
        if (category == -1) {
            category = ITEM_CATEGORY_OTHER;  // 回退到 "其他" 分类
        }
        StorageState storage_state = StringToStorageState(storage_state_str);
        
        // 解析过期时间字符串
        time_t expire_time = ParseTime(expire_time_str);
        if (expire_time == 0) {
            return std::string("Invalid expire_time format. Use: YYYY-MM-DD HH:MM:SS");
        }
        
        // 添加食材到冰箱
        auto& fridge = FridgeManager::GetInstance();
        ItemId new_item_id = fridge.AddItem(name, category, quantity, unit, expire_time, storage_state);
        
        if (new_item_id == 0) {
            return std::string("Failed to add item to fridge (max items exceeded?)");
        }
        
        // 获取新添加的食材信息并返回
        FridgeItem new_item = fridge.GetItem(new_item_id);
        std::string result_str = new_item.ToMcpJson();
        
        ESP_LOGI(TAG, "[DEBUG] fridge.item.add result: %s", result_str.c_str());
        ESP_LOGI(TAG, "Added item %lu: %s (%.1f %s, expires: %s)", 
                 new_item_id, name.c_str(), quantity, unit.c_str(), expire_time_str.c_str());
        
        return result_str;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error adding item: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleRemoveItem(const PropertyList& properties) {
    try {
        ItemId item_id = properties["item_id"].value<int>();
        
        auto& fridge = FridgeManager::GetInstance();
        
        // 先获取食材信息用于日志记录
        FridgeItem item = fridge.GetItem(item_id);
        if (item.id == 0) {
            return std::string("Item not found");
        }
        
        // 删除食材
        bool success = fridge.RemoveItem(item_id);
        
        if (!success) {
            return std::string("Failed to remove item");
        }
        
        // 构造返回结果
        std::string result_json = "{\"item_id\":" + std::to_string(item_id) + ",\"name\":\"" + 
                                  item.name + "\",\"status\":\"removed\"}";
        
        ESP_LOGI(TAG, "[DEBUG] fridge.item.remove result: %s", result_json.c_str());
        ESP_LOGI(TAG, "Removed item %lu: %s", item_id, item.name.c_str());
        
        return result_json;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error removing item: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleClearAll(const PropertyList& properties) {
    try {
        auto& fridge = FridgeManager::GetInstance();
        
        // 获取当前食材总数用于日志
        FridgeStatistics stats = fridge.GetStatistics();
        int cleared_count = stats.total_items;
        
        // 清空所有食材
        fridge.ClearAllItems();
        
        // 验证清空成功
        FridgeStatistics stats_after = fridge.GetStatistics();
        
        // 构造返回结果
        std::string result_json = "{\"cleared_items\":" + std::to_string(cleared_count) + 
                                  ",\"remaining_items\":" + std::to_string(stats_after.total_items) + 
                                  ",\"status\":\"success\"}";
        
        ESP_LOGI(TAG, "[DEBUG] fridge.item.clear_all result: %s", result_json.c_str());
        ESP_LOGI(TAG, "Cleared all items from fridge. Removed: %d items", cleared_count);
        
        return result_json;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error clearing all items: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleStatsSummary(const PropertyList& properties) {
    try {
        auto& fridge = FridgeManager::GetInstance();
        FridgeStatistics stats = fridge.GetStatistics();
        
        // 构造 JSON 响应，包含统计摘要
        std::string result_json = "{";
        result_json += "\"total_items\":" + std::to_string(stats.total_items);
        result_json += ",\"expired_items\":" + std::to_string(stats.expired_items);
        result_json += ",\"expiring_soon_items\":" + std::to_string(stats.expiring_soon_items);
        
        // 添加分类统计
        result_json += ",\"category_count\":{";
        
        bool first = true;
        // 遍历所有分类，只添加非零的分类
        const char* category_names[] = {
            "vegetable", "fruit", "meat", "egg", "dairy", 
            "cooked", "seasoning", "beverage", "quick", "other"
        };
        
        for (int i = 0; i < 10; ++i) {
            if (stats.category_count[i] > 0) {
                if (!first) result_json += ",";
                result_json += "\"" + std::string(category_names[i]) + "\":" + std::to_string(stats.category_count[i]);
                first = false;
            }
        }
        
        result_json += "}";
        result_json += "}";
        
        ESP_LOGI(TAG, "[DEBUG] fridge.stats.summary result: %s", result_json.c_str());
        ESP_LOGI(TAG, "Fridge Stats - Total: %d, Expired: %d, Expiring Soon: %d", 
                 stats.total_items, stats.expired_items, stats.expiring_soon_items);
        
        return result_json;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error getting stats summary: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleStatsQuery(const PropertyList& properties) {
    try {
        auto& fridge = FridgeManager::GetInstance();
        
        // 解析查询参数
        FridgeQuery query;
        
        // 解析分类过滤（可选）
        try {
            std::string category_str = properties["category"].value<std::string>();
            if (!category_str.empty() && category_str != "all") {
                ItemCategory cat = StringToItemCategory(category_str);
                if (cat != -1) {
                    query.category = cat;
                }
            }
        } catch (...) {
            // 属性不存在，使用默认值（无分类过滤）
        }
        
        // 解析过滤类型（all|expired|expiring_soon）
        try {
            std::string filter_str = properties["filter"].value<std::string>();
            if (filter_str == "expired") {
                query.only_expired = true;
            } else if (filter_str == "expiring_soon") {
                query.expiring_soon = true;
            }
            // "all" 或其他值都使用默认值
        } catch (...) {
            // 属性不存在，使用默认值（all）
        }
        
        // 解析过期天数（可选）
        try {
            int days = properties["expiring_days"].value<int>();
            if (days > 0) {
                query.expiring_days = days;
            }
        } catch (...) {
            // 属性不存在，使用默认值 7 天
        }
        
        // 执行查询
        std::vector<FridgeItem> results = fridge.Query(query);
        
        // 构造 JSON 数组响应
        std::string result_json = "[";
        for (size_t i = 0; i < results.size(); ++i) {
            if (i > 0) result_json += ",";
            result_json += results[i].ToMcpJson();
        }
        result_json += "]";
        
        ESP_LOGI(TAG, "[DEBUG] fridge.stats.query result: returned %u items", (unsigned int)results.size());
        ESP_LOGI(TAG, "[DEBUG] fridge.stats.query content: %s", result_json.c_str());
        
        // 构造日志信息（避免临时对象的野指针问题）
        const char* category_str = "none";
        if (query.category.has_value()) {
            // 直接使用枚举的转换函数
            category_str = ItemCategoryToString(query.category.value());
        }
        const char* filter_str = query.only_expired ? "expired" : (query.expiring_soon ? "expiring_soon" : "all");
        ESP_LOGI(TAG, "Query executed: category=%s, filter=%s, results=%u",
                 category_str, filter_str, (unsigned int)results.size());
        
        return result_json;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error querying items: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleItemList(const PropertyList& properties) {
    try {
        auto& fridge = FridgeManager::GetInstance();
        FridgeQuery query;

        // 解析分类过滤
        try {
            std::string category_str = properties["category"].value<std::string>();
            if (!category_str.empty() && category_str != "all") {
                ItemCategory cat = StringToItemCategory(category_str);
                if (cat != -1) {
                    query.category = cat;
                }
            }
        } catch (...) {}

        // 解析限制数量
        try {
            query.limit = properties["limit"].value<int>();
        } catch (...) {}

        // 解析排序字段
        try {
            query.sort_by = properties["sort_by"].value<std::string>();
        } catch (...) {}

        // 解析排序顺序
        try {
            query.order = properties["order"].value<std::string>();
            if (query.order != "asc" && query.order != "desc") {
                query.order = "desc"; // 默认降序
            }
        } catch (...) {
            query.order = "desc";
        }

        // 执行查询
        std::vector<FridgeItem> results = fridge.Query(query);

        // 构造 JSON 数组响应
        std::string result_json = "[";
        for (size_t i = 0; i < results.size(); ++i) {
            if (i > 0) result_json += ",";
            result_json += results[i].ToMcpJson();
        }
        result_json += "]";

        ESP_LOGI(TAG, "[DEBUG] fridge.item.list result: returned %u items", (unsigned int)results.size());
        ESP_LOGI(TAG, "[DEBUG] fridge.item.list content: %s", result_json.c_str());
        return result_json;

    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error listing items: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleItemUpdate(const PropertyList& properties) {
    try {
        ItemId item_id = properties["item_id"].value<int>();
        auto& fridge = FridgeManager::GetInstance();
        
        // 获取现有食材
        FridgeItem item = fridge.GetItem(item_id);
        if (item.id == 0) {
            return std::string("Error: Item not found with ID ") + std::to_string(item_id);
        }
        
        bool updated = false;
        
        // 更新名称
        try {
            std::string name = properties["name"].value<std::string>();
            if (!name.empty()) {
                item.name = name;
                updated = true;
            }
        } catch (...) {}
        
        // 更新分类
        try {
            std::string category_str = properties["category"].value<std::string>();
            if (!category_str.empty()) {
                ItemCategory cat = StringToItemCategory(category_str);
                if (cat != -1) {
                    item.category = cat;
                    updated = true;
                }
            }
        } catch (...) {}
        
        // 更新数量
        try {
            int quantity_int = properties["quantity"].value<int>();
            item.quantity = static_cast<float>(quantity_int);
            updated = true;
        } catch (...) {}
        
        // 更新单位
        try {
            std::string unit = properties["unit"].value<std::string>();
            if (!unit.empty()) {
                item.unit = unit;
                updated = true;
            }
        } catch (...) {}
        
        // 更新过期时间
        try {
            std::string expire_time_str = properties["expire_time"].value<std::string>();
            if (!expire_time_str.empty()) {
                time_t expire_time = ParseTime(expire_time_str);
                if (expire_time > 0) {
                    item.expire_time = expire_time;
                    updated = true;
                }
            }
        } catch (...) {}
        
        // 更新存储状态
        try {
            std::string storage_state_str = properties["storage_state"].value<std::string>();
            if (!storage_state_str.empty()) {
                item.state = StringToStorageState(storage_state_str);
                updated = true;
            }
        } catch (...) {}
        
        if (updated) {
            item.last_update_time = std::time(nullptr);
            fridge.UpdateItem(item);
            ESP_LOGI(TAG, "Updated item %lu: %s", item_id, item.name.c_str());
        }
        
        std::string result_json = item.ToMcpJson();
        ESP_LOGI(TAG, "[DEBUG] fridge.item.update result: %s", result_json.c_str());
        return result_json;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error updating item: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandlePageManager(const PropertyList& properties) {
    try {
        int page = properties["target_page"].value<int>();
        if (page < 1 || page > 15) {
            return ReturnValue("Invalid page index. Must be between 1 and 15.");
        }
        
        auto* epaper = Board::GetInstance().GetEpaperDisplay();
        if (epaper == nullptr) {
            return ReturnValue("E-paper display not found on this board.");
        }
        
        ESP_LOGI(TAG, "Switching epaper to page %d via MCP", page);
        epaper->SetPage(page);
        
        std::string result_json = "{\"status\":\"success\",\"current_page\":" + std::to_string(page) + "}";
        ESP_LOGI(TAG, "[DEBUG] fridge.pagemanager result: %s", result_json.c_str());
        return result_json;
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in page manager: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleRecipeRecommend(const PropertyList& properties) {
    try {
        std::string recommendation_mode = properties["recommendation_mode"].value<std::string>();
        std::string dish_name = properties["dish_name"].value<std::string>();
        std::string summary = properties["summary"].value<std::string>();
        std::string required_ingredients = properties["required_ingredients"].value<std::string>();
        std::string extra_ingredients = properties["extra_ingredients"].value<std::string>();
        std::string cooking_time = properties["cooking_time"].value<std::string>();
        bool switch_page = true;
        try {
            switch_page = properties["switch_page"].value<bool>();
        } catch (...) {
            switch_page = true;
        }

        if (recommendation_mode != "fridge_only" && recommendation_mode != "mixed_purchase") {
            return std::string("Invalid recommendation_mode. Use `fridge_only` or `mixed_purchase`.");
        }

        if (dish_name.empty()) {
            return std::string("dish_name cannot be empty.");
        }

        if (required_ingredients.empty()) {
            return std::string("required_ingredients cannot be empty.");
        }

        auto* epaper = Board::GetInstance().GetEpaperDisplay();
        if (epaper == nullptr) {
            return ReturnValue("E-paper display not found on this board.");
        }

        auto inventory_items = FridgeManager::GetInstance().GetAllItems();

        // 自动比对冰箱库存，计算缺失食材
        std::string missing_ingredients = ComputeMissingIngredients(required_ingredients, inventory_items);

        // fridge_only 模式：所有食材必须在冰箱里，否则报错提示需要采购
        if (recommendation_mode == "fridge_only" && !missing_ingredients.empty()) {
            return std::string("fridge_only mode requires all ingredients to be in the fridge. "
                "Missing: " + missing_ingredients +
                ". Either add them to the fridge first, or use mixed_purchase mode.");
        }

        // 自动填充 extra_ingredients：如果调用方未指定，且有缺失食材，则自动填入
        std::string effective_extra_ingredients = extra_ingredients;
        if (!missing_ingredients.empty()) {
            if (effective_extra_ingredients.empty()) {
                // 调用方未指定采购列表，自动填入缺失食材
                effective_extra_ingredients = missing_ingredients;
            } else {
                // 调用方指定了采购列表，但也可能不全，合并缺失项
                auto extra_list = SplitIngredients(effective_extra_ingredients);
                for (const auto& ing : SplitIngredients(missing_ingredients)) {
                    bool found = false;
                    for (const auto& ex : extra_list) {
                        std::string ing_lower = ing, ex_lower = ex;
                        for (auto& c : ing_lower) if (c >= 'A' && c <= 'Z') c += 32;
                        for (auto& c : ex_lower) if (c >= 'A' && c <= 'Z') c += 32;
                        if (ex_lower.find(ing_lower) != std::string::npos ||
                            ing_lower.find(ex_lower) != std::string::npos) {
                            found = true;
                            break;
                        }
                    }
                    if (!found) {
                        if (!effective_extra_ingredients.empty()) effective_extra_ingredients += "、";
                        effective_extra_ingredients += ing;
                    }
                }
            }
        }

        if (recommendation_mode == "mixed_purchase" && effective_extra_ingredients.empty()) {
            return std::string("extra_ingredients cannot be empty when recommendation_mode is `mixed_purchase`.");
        }

        std::string recipe_text = BuildRecipeDisplayText(
            recommendation_mode,
            dish_name,
            summary,
            required_ingredients,
            effective_extra_ingredients,
            cooking_time
        );

        epaper->SetRecipeContent(recipe_text.c_str());
        if (switch_page) {
            epaper->SetPage(RECIPE_PAGE);
        }

        std::string result_json = "{";
        result_json += "\"status\":\"success\"";
        result_json += ",\"page\":4";
        result_json += ",\"recommendation_mode\":\"" + EscapeJsonString(recommendation_mode) + "\"";
        result_json += ",\"dish_name\":\"" + EscapeJsonString(dish_name) + "\"";
        result_json += ",\"summary\":\"" + EscapeJsonString(summary) + "\"";
        result_json += ",\"required_ingredients\":\"" + EscapeJsonString(required_ingredients) + "\"";
        result_json += ",\"extra_ingredients\":\"" + EscapeJsonString(effective_extra_ingredients) + "\"";
        result_json += ",\"missing_ingredients\":\"" + EscapeJsonString(missing_ingredients) + "\"";
        result_json += ",\"cooking_time\":\"" + EscapeJsonString(cooking_time) + "\"";
        result_json += ",\"recipe_text\":\"" + EscapeJsonString(recipe_text) + "\"";
        result_json += ",\"current_fridge_items\":" + BuildInventorySnapshotJson(inventory_items);
        result_json += "}";

        ESP_LOGI(TAG, "[DEBUG] fridge.recipe.recommend result: %s", result_json.c_str());
        return result_json;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in recipe recommend: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

// ==================== Canvas 工具实现 ====================

// canvas 前缀，用于标识动态添加的画布控件
static const char* CANVAS_PREFIX = "canvas_";

// font_size → u8g2 字体指针映射
static const uint8_t* GetCanvasFont(int font_size) {
    if (font_size <= 12) {
        return u8g2_font_wqy12_t_gb2312;
    }
    return u8g2_font_wqy16_t_gb2312;
}

// align 字符串 → EpaperTextAlign 枚举
static EpaperTextAlign ParseAlign(const std::string& align) {
    if (align == "center") return EpaperTextAlign::CENTER;
    if (align == "right") return EpaperTextAlign::RIGHT;
    return EpaperTextAlign::LEFT;
}

// 统一的刷新辅助：如果 refresh=true 则刷新画布页
static void RefreshCanvasIfNeeded(bool refresh) {
    if (!refresh) return;
    auto* epaper = Board::GetInstance().GetEpaperDisplay();
    if (epaper) {
        epaper->ShowCanvasPage();
    }
}

// 构造带 canvas_ 前缀的完整 id
static std::string MakeCanvasId(const std::string& id) {
    return std::string(CANVAS_PREFIX) + id;
}

// Canvas 控件数量上限
static const int CANVAS_MAX_LABELS = 30;

// 统计当前 canvas_ 前缀的 label 数量
static int CountCanvasLabels() {
    auto* epaper = Board::GetInstance().GetEpaperDisplay();
    if (epaper == nullptr) return 0;
    auto* labels = epaper->GetAllLabels();
    if (labels == nullptr) return 0;
    int count = 0;
    for (const auto& pair : *labels) {
        if (strncmp(pair.first.c_str(), CANVAS_PREFIX, 7) == 0) {
            count++;
        }
    }
    return count;
}

// 检查控件数量是否超限（用于 add 操作前检查）
// 如果 label_id 已存在则允许替换（不增加数量）
// 返回 true 表示允许操作，false 表示超限
static bool CheckCanvasLabelLimit(const std::string& full_id) {
    auto* epaper = Board::GetInstance().GetEpaperDisplay();
    if (epaper == nullptr) return true;  // 没有屏幕就不管了
    // 如果同 ID 已存在，是替换操作，不增加数量
    if (epaper->GetLabel(String(full_id.c_str())) != nullptr) {
        return true;
    }
    // 新增操作，检查上限
    int count = CountCanvasLabels();
    if (count >= CANVAS_MAX_LABELS) {
        return false;
    }
    return true;
}

// ==================== Canvas 布局持久化 ====================
//
// 布局文件格式 (layout.json):
// [
//   {"type":"text","id":"title","text":"...","x":10,"y":5,"font_size":16,"align":"center","max_width":276},
//   {"type":"line","id":"div","x1":10,"y1":28,"x2":286,"y2":28,"width":2},
//   {"type":"rect","id":"box","x":8,"y":33,"w":280,"h":60,"filled":false},
//   {"type":"image","id":"heart","name":"heart","x":116,"y":30,"w":64,"h":64}
// ]

// 保存当前 canvas 布局到 LittleFS
static void SaveCanvasLayout() {
    auto* epaper = Board::GetInstance().GetEpaperDisplay();
    if (epaper == nullptr) return;

    // 如果没有 canvas 控件，删除布局文件而不是写空数组
    int count = CountCanvasLabels();
    if (count == 0) {
        unlink(CANVAS_LAYOUT_FILE);
        ESP_LOGI(TAG, "No canvas labels, layout file deleted");
        return;
    }

    FILE* f = fopen(CANVAS_LAYOUT_FILE, "w");
    if (!f) {
        ESP_LOGW(TAG, "Failed to open layout file for writing");
        return;
    }

    fputc('[', f);
    bool first = true;

    // 遍历所有 canvas_ 前缀的 label
    auto* labels = epaper->GetAllLabels();
    if (labels) {
        for (const auto& pair : *labels) {
            const String& label_id = pair.first;
            EpaperLabel* label = pair.second;

            // 只保存 canvas_ 前缀的
            if (strncmp(label_id.c_str(), CANVAS_PREFIX, 7) != 0) continue;
            // 跳过 layout.json 自身
            if (label->page != 6) continue;

            // 提取不含前缀的 id
            std::string short_id = label_id.c_str() + 7;

            if (!first) fputc(',', f);
            first = false;

            switch (label->type) {
                case EpaperObjectType::TEXT:
                    fprintf(f, "{\"type\":\"text\",\"id\":\"%s\",\"text\":\"%s\",\"x\":%d,\"y\":%d,\"font_size\":%d,\"align\":\"%s\",\"max_width\":%d}",
                            short_id.c_str(),
                            EscapeJsonString(label->text().c_str()).c_str(),
                            (int)label->x, (int)(label->y - label->h + 4),  // 反算原始 y
                            (int)label->h - 4,  // 反算 font_size
                            label->align == EpaperTextAlign::CENTER ? "center" :
                            label->align == EpaperTextAlign::RIGHT ? "right" : "left",
                            (int)label->w_max);
                    break;
                case EpaperObjectType::RECT:
                    fprintf(f, "{\"type\":\"rect\",\"id\":\"%s\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"filled\":%s}",
                            short_id.c_str(),
                            (int)label->x, (int)label->y, (int)label->w, (int)label->h,
                            label->filled ? "true" : "false");
                    break;
                case EpaperObjectType::LINE:
                    fprintf(f, "{\"type\":\"line\",\"id\":\"%s\",\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d,\"width\":%d}",
                            short_id.c_str(),
                            (int)label->x, (int)label->y, (int)label->x1, (int)label->y1,
                            (int)label->width);
                    break;
                case EpaperObjectType::BITMAP:
                    // 保存图片信息：name, x, y, w, h
                    if (label->image_name[0] != '\0') {
                        fprintf(f, "{\"type\":\"image\",\"id\":\"%s\",\"name\":\"%s\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}",
                                short_id.c_str(),
                                label->image_name,
                                (int)label->x, (int)label->y, (int)label->w, (int)label->h);
                    }
                    break;
                default:
                    break;
            }
        }
    }

    fputc(']', f);
    fclose(f);
    ESP_LOGI(TAG, "Canvas layout saved to %s", CANVAS_LAYOUT_FILE);
}

// 从 LittleFS 恢复 canvas 布局
static void LoadCanvasLayout() {
    FILE* f = fopen(CANVAS_LAYOUT_FILE, "r");
    if (!f) {
        ESP_LOGI(TAG, "No saved canvas layout found");
        return;
    }

    // 读取整个文件
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    if (file_size <= 0 || file_size > 16384) {
        fclose(f);
        ESP_LOGW(TAG, "Invalid layout file size: %ld", file_size);
        return;
    }
    fseek(f, 0, SEEK_SET);

    char* buf = (char*)malloc(file_size + 1);
    if (!buf) {
        fclose(f);
        return;
    }
    size_t rd = fread(buf, 1, file_size, f);
    buf[rd] = '\0';
    fclose(f);

    // 简单解析 JSON 数组，逐个恢复控件
    // 格式: [{"type":"text","id":"title",...},{...},...]
    auto* epaper = Board::GetInstance().GetEpaperDisplay();
    if (!epaper) {
        free(buf);
        return;
    }

    // 清理旧 canvas labels
    epaper->ClearCanvasLabels();

    const char* p = buf;
    int count = 0;

    while (*p) {
        // 跳过空白和到 {
        while (*p && *p != '{') p++;
        if (!*p) break;
        const char* obj_start = p;

        // 找到对应的 }
        int depth = 0;
        while (*p) {
            if (*p == '{') depth++;
            if (*p == '}') depth--;
            p++;
            if (depth == 0) break;
        }

        // 从 obj_start 到 p 是一个完整的 JSON 对象
        std::string obj_str(obj_start, p - obj_start);

        // 解析字段
        std::string type = "", id = "", text = "", align = "left";
        int x = 0, y = 0, w = 0, h = 0, font_size = 16, max_width = 276;
        int x1 = 0, y1_ = 0, x2 = 0, y2 = 0, width = 1;
        bool filled = false;

        // 提取 type
        size_t pos = obj_str.find("\"type\":\"");
        if (pos != std::string::npos) {
            size_t start = pos + 8;
            size_t end = obj_str.find("\"", start);
            type = obj_str.substr(start, end - start);
        }
        // 提取 id
        pos = obj_str.find("\"id\":\"");
        if (pos != std::string::npos) {
            size_t start = pos + 6;
            size_t end = obj_str.find("\"", start);
            id = obj_str.substr(start, end - start);
        }
        // 提取 text
        pos = obj_str.find("\"text\":\"");
        if (pos != std::string::npos) {
            size_t start = pos + 8;
            size_t end = obj_str.find("\"", start);
            text = obj_str.substr(start, end - start);
        }
        // 提取 x
        pos = obj_str.find("\"x\":");
        if (pos != std::string::npos) x = atoi(obj_str.c_str() + pos + 4);
        // 提取 y
        pos = obj_str.find("\"y\":");
        if (pos != std::string::npos) y = atoi(obj_str.c_str() + pos + 4);
        // 提取 w
        pos = obj_str.find("\"w\":");
        if (pos != std::string::npos) w = atoi(obj_str.c_str() + pos + 4);
        // 提取 h
        pos = obj_str.find("\"h\":");
        if (pos != std::string::npos) h = atoi(obj_str.c_str() + pos + 4);
        // 提取 font_size ("font_size": 共12字符)
        pos = obj_str.find("\"font_size\":");
        if (pos != std::string::npos) font_size = atoi(obj_str.c_str() + pos + 12);
        // 提取 align
        pos = obj_str.find("\"align\":\"");
        if (pos != std::string::npos) {
            size_t start = pos + 9;
            size_t end = obj_str.find("\"", start);
            align = obj_str.substr(start, end - start);
        }
        // 提取 max_width ("max_width": 共12字符)
        pos = obj_str.find("\"max_width\":");
        if (pos != std::string::npos) max_width = atoi(obj_str.c_str() + pos + 12);
        // 提取 filled
        pos = obj_str.find("\"filled\":");
        if (pos != std::string::npos) {
            filled = (obj_str.substr(pos + 9, 4) == "true");
        }
        // 提取 x1,y1,x2,y2,width
        pos = obj_str.find("\"x1\":");
        if (pos != std::string::npos) x1 = atoi(obj_str.c_str() + pos + 5);
        pos = obj_str.find("\"y1\":");
        if (pos != std::string::npos) y1_ = atoi(obj_str.c_str() + pos + 5);
        pos = obj_str.find("\"x2\":");
        if (pos != std::string::npos) x2 = atoi(obj_str.c_str() + pos + 5);
        pos = obj_str.find("\"y2\":");
        if (pos != std::string::npos) y2 = atoi(obj_str.c_str() + pos + 5);

        // 提取 name (图片文件名)
        std::string img_name = "";
        pos = obj_str.find("\"name\":\"");
        if (pos != std::string::npos) {
            size_t start = pos + 8;
            size_t end = obj_str.find("\"", start);
            img_name = obj_str.substr(start, end - start);
        }

        const uint8_t* font = GetCanvasFont(font_size);
        EpaperTextAlign ealign = ParseAlign(align);
        std::string full_id = MakeCanvasId(id);

        if (type == "text" && !id.empty()) {
            int text_h = font_size + 4;
            epaper->AddLabel(String(full_id.c_str()), new EpaperLabel(
                EpaperLabel::Text(text.c_str(), x, y, max_width, text_h, font_size,
                                 font, GxEPD_BLACK, ealign, 1, true, false, 6)));
            count++;
        } else if (type == "rect" && !id.empty()) {
            epaper->AddLabel(String(full_id.c_str()), new EpaperLabel(
                EpaperLabel::Rect(x, y, w, h, filled, GxEPD_BLACK, 1, true, 6)));
            count++;
        } else if (type == "line" && !id.empty()) {
            epaper->AddLabel(String(full_id.c_str()), new EpaperLabel(
                EpaperLabel::Line(x1, y1_, x2, y2, width, GxEPD_BLACK, 1, true, 6)));
            count++;
        } else if (type == "image" && !id.empty() && !img_name.empty()) {
            // 从 LittleFS 加载图片文件
            std::string img_path = std::string("/canvas/") + img_name;
            FILE* imgf = fopen(img_path.c_str(), "rb");
            if (imgf) {
                size_t total_bytes = w * h / 8;
                uint8_t* bitmap = (uint8_t*)malloc(total_bytes);
                if (bitmap) {
                    size_t rd = fread(bitmap, 1, total_bytes, imgf);
                    if (rd < total_bytes) {
                        memset(bitmap + rd, 0, total_bytes - rd);
                    }
                    epaper->AddLabel(String(full_id.c_str()), new EpaperLabel(
                        EpaperLabel::Bitmap(x, y, bitmap, w, h, 1, 1, false, false, false, true, 6, img_name.c_str())));
                    count++;
                    ESP_LOGI(TAG, "Restored image: %s (%dx%d)", img_name.c_str(), w, h);
                }
                fclose(imgf);
            } else {
                ESP_LOGW(TAG, "Image file not found during restore: %s", img_path.c_str());
            }
        }
    }

    free(buf);
    ESP_LOGI(TAG, "Canvas layout restored: %d elements", count);
}

// ==================== 自定义页面工具实现 ====================

ReturnValue FridgeMcpTools::HandlePageCreate(const PropertyList& properties) {
    try {
        std::string name = properties["name"].value<std::string>();
        auto& cpm = CustomPageManager::GetInstance();
        int page = cpm.CreatePage(name);
        if (page < 0) {
            return ReturnValue("Cannot create page: limit reached (max 9 custom pages).");
        }
        std::string result = "{\"status\":\"success\",\"page\":" + std::to_string(page) +
            ",\"name\":\"" + name + "\"}";
        ESP_LOGI(TAG, "[DEBUG] page.create result: %s", result.c_str());
        return result;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in page.create: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandlePageDelete(const PropertyList& properties) {
    try {
        int page = properties["page"].value<int>();
        auto& cpm = CustomPageManager::GetInstance();
        if (!cpm.DeletePage(page)) {
            return ReturnValue("Failed to delete page " + std::to_string(page) + ". Page may not exist or is built-in.");
        }
        // 如果当前显示的是被删除的页面，切回主页
        auto* epaper = Board::GetInstance().GetEpaperDisplay();
        if (epaper) {
            epaper->SetPage(5);  // HOME_PIC_DISPLAY
        }
        std::string result = "{\"status\":\"success\",\"removed\":" + std::to_string(page) + "}";
        ESP_LOGI(TAG, "[DEBUG] page.delete result: %s", result.c_str());
        return result;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in page.delete: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandlePageList(const PropertyList& properties) {
    try {
        auto& cpm = CustomPageManager::GetInstance();
        // 内置页面
        std::string json = "[";
        // 内置页面信息
        const char* builtin_names[] = {"", "聊天", "冰箱统计", "食物列表", "菜谱", "主页图片", "默认画布"};
        for (int i = 1; i <= 6; i++) {
            if (i > 1) json += ",";
            json += "{\"page\":" + std::to_string(i);
            json += ",\"name\":\"" + std::string(builtin_names[i]) + "\"";
            json += ",\"builtin\":true}";
        }
        // 自定义页面
        auto& pages = cpm.GetPages();
        for (const auto& pi : pages) {
            json += ",{\"page\":" + std::to_string(pi.page);
            json += ",\"name\":\"" + pi.name + "\"";
            json += ",\"builtin\":false}";
        }
        json += "]";
        ESP_LOGI(TAG, "[DEBUG] page.list result: %s", json.c_str());
        return json;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in page.list: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandlePageRename(const PropertyList& properties) {
    try {
        int page = properties["page"].value<int>();
        std::string name = properties["name"].value<std::string>();
        auto& cpm = CustomPageManager::GetInstance();
        if (!cpm.RenamePage(page, name)) {
            return ReturnValue("Failed to rename page " + std::to_string(page) + ". Page may not exist or is built-in.");
        }
        std::string result = "{\"status\":\"success\",\"page\":" + std::to_string(page) +
            ",\"name\":\"" + name + "\"}";
        ESP_LOGI(TAG, "[DEBUG] page.rename result: %s", result.c_str());
        return result;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in page.rename: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleElementAdd(const PropertyList& properties) {
    try {
        int page = properties["page"].value<int>();
        std::string id = properties["id"].value<std::string>();
        std::string type = properties["type"].value<std::string>();

        // 通用参数
        int x = properties["x"].value<int>();
        int y = properties["y"].value<int>();

        // text 参数
        std::string text = "";
        try { text = properties["text"].value<std::string>(); } catch (...) {}
        int font_size = 16;
        try { font_size = properties["font_size"].value<int>(); } catch (...) {}
        std::string align = "left";
        try { align = properties["align"].value<std::string>(); } catch (...) {}
        int max_width = 276;
        try { max_width = properties["max_width"].value<int>(); } catch (...) {}

        // rect 参数
        int w = 40;
        try { w = properties["w"].value<int>(); } catch (...) {}
        int h = 30;
        try { h = properties["h"].value<int>(); } catch (...) {}
        bool filled = false;
        try { filled = properties["filled"].value<bool>(); } catch (...) {}

        // line 参数
        int x1 = 0, y1 = 0, x2 = 0, y2 = 0, width = 1;
        try { x1 = properties["x1"].value<int>(); } catch (...) {}
        try { y1 = properties["y1"].value<int>(); } catch (...) {}
        try { x2 = properties["x2"].value<int>(); } catch (...) {}
        try { y2 = properties["y2"].value<int>(); } catch (...) {}
        try { width = properties["width"].value<int>(); } catch (...) {}

        bool dynamic = false;
        try { dynamic = properties["dynamic"].value<bool>(); } catch (...) {}
        std::string dynamic_type;
        try { dynamic_type = properties["dynamic_type"].value<std::string>(); } catch (...) {}
        bool refresh = false;
        try { refresh = properties["refresh"].value<bool>(); } catch (...) {}

        (void)max_width;  // max_width 在 CustomPageManager::AddElement 内部固定为 276
        (void)dynamic;    // dynamic 标志暂不使用，由 dynamic_type 决定是否为动态元素

        auto& cpm = CustomPageManager::GetInstance();
        if (!cpm.AddElement(page, id, type, text, x, y,
                           font_size, align, w, h, filled,
                           x1, y1, x2, y2, width,
                           "",  // image_name（暂不支持通过此工具添加图片）
                           dynamic, dynamic_type, "", 0)) {
            return ReturnValue("Failed to add element to page " + std::to_string(page) +
                ". Page may not exist or element limit (30) reached.");
        }

        // 如果 refresh=true 且当前页是目标页，刷新显示
        if (refresh) {
            auto* epaper = Board::GetInstance().GetEpaperDisplay();
            if (epaper) {
                epaper->SetPage(page);
            }
        }

        std::string result = "{\"status\":\"success\",\"page\":" + std::to_string(page) +
            ",\"id\":\"" + id + "\",\"type\":\"" + type + "\"}";
        ESP_LOGI(TAG, "[DEBUG] page.element.add result: %s", result.c_str());
        return result;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in page.element.add: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleElementUpdate(const PropertyList& properties) {
    try {
        int page = properties["page"].value<int>();
        std::string id = properties["id"].value<std::string>();
        std::string text = properties["text"].value<std::string>();
        bool refresh = true;
        try { refresh = properties["refresh"].value<bool>(); } catch (...) {}

        auto& cpm = CustomPageManager::GetInstance();
        if (!cpm.UpdateElementText(page, id, text)) {
            return ReturnValue("Element '" + id + "' not found on page " + std::to_string(page) + ".");
        }

        if (refresh) {
            auto* epaper = Board::GetInstance().GetEpaperDisplay();
            if (epaper) {
                // 只刷新当前页（避免不必要的全屏刷新）
                // SetPage 内部会比较，如果已在目标页则不刷新
                // 这里直接调 SetPage 触发 UpdateUI
                epaper->SetPage(page);
            }
        }

        std::string result = "{\"status\":\"success\",\"page\":" + std::to_string(page) +
            ",\"id\":\"" + id + "\",\"text\":\"" + text + "\"}";
        ESP_LOGI(TAG, "[DEBUG] page.element.update result: %s", result.c_str());
        return result;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in page.element.update: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleElementRemove(const PropertyList& properties) {
    try {
        int page = properties["page"].value<int>();
        std::string id = properties["id"].value<std::string>();
        bool refresh = false;
        try { refresh = properties["refresh"].value<bool>(); } catch (...) {}

        auto& cpm = CustomPageManager::GetInstance();
        if (!cpm.RemoveElement(page, id)) {
            return ReturnValue("Element '" + id + "' not found on page " + std::to_string(page) + ".");
        }

        if (refresh) {
            auto* epaper = Board::GetInstance().GetEpaperDisplay();
            if (epaper) {
                epaper->SetPage(page);
            }
        }

        std::string result = "{\"status\":\"success\",\"removed\":\"" + id +
            "\",\"page\":" + std::to_string(page) + "}";
        ESP_LOGI(TAG, "[DEBUG] page.element.remove result: %s", result.c_str());
        return result;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in page.element.remove: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleElementList(const PropertyList& properties) {
    try {
        int page = properties["page"].value<int>();
        auto& cpm = CustomPageManager::GetInstance();
        std::string result = cpm.ListElements(page);
        ESP_LOGI(TAG, "[DEBUG] page.element.list result: %s", result.c_str());
        return result;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in page.element.list: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandlePageClear(const PropertyList& properties) {
    try {
        int page = properties["page"].value<int>();
        bool refresh = true;
        try { refresh = properties["refresh"].value<bool>(); } catch (...) {}

        auto& cpm = CustomPageManager::GetInstance();
        if (!cpm.ClearPage(page)) {
            return ReturnValue("Failed to clear page " + std::to_string(page) + ".");
        }

        if (refresh) {
            auto* epaper = Board::GetInstance().GetEpaperDisplay();
            if (epaper) {
                epaper->SetPage(page);
            }
        }

        std::string result = "{\"status\":\"success\",\"page\":" + std::to_string(page) + "}";
        ESP_LOGI(TAG, "[DEBUG] page.clear result: %s", result.c_str());
        return result;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in page.clear: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleCanvasAddText(const PropertyList& properties) {
    try {
        std::string id = properties["id"].value<std::string>();
        std::string text = properties["text"].value<std::string>();
        int x = properties["x"].value<int>();
        int y = properties["y"].value<int>();
        int font_size = 16;
        try { font_size = properties["font_size"].value<int>(); } catch (...) {}
        std::string align_str = "left";
        try { align_str = properties["align"].value<std::string>(); } catch (...) {}
        int max_width = 276;
        try { max_width = properties["max_width"].value<int>(); } catch (...) {}
        bool refresh = false;
        try { refresh = properties["refresh"].value<bool>(); } catch (...) {}

        auto* epaper = Board::GetInstance().GetEpaperDisplay();
        if (epaper == nullptr) {
            return ReturnValue("E-paper display not found on this board.");
        }

        const uint8_t* font = GetCanvasFont(font_size);
        EpaperTextAlign align = ParseAlign(align_str);
        std::string full_id = MakeCanvasId(id);

        // 检查控件数量上限
        if (!CheckCanvasLabelLimit(full_id)) {
            return ReturnValue("Canvas label limit reached (30). Call fridge.canvas.clear first.");
        }

        // font_height = font_size (12 或 16)
        int h = font_size + 4;  // 给点余量

        epaper->AddLabel(String(full_id.c_str()), new EpaperLabel(
            EpaperLabel::Text(text.c_str(), x, y, max_width, h, font_size,
                             font, GxEPD_BLACK, align, 1, true, false, 6)));

        SaveCanvasLayout();
        RefreshCanvasIfNeeded(refresh);

        std::string result = "{\"status\":\"success\",\"id\":\"" + EscapeJsonString(id) +
            "\",\"full_id\":\"" + EscapeJsonString(full_id) +
            "\",\"type\":\"text\",\"page\":6}";
        return result;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in canvas.add_text: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleCanvasAddRect(const PropertyList& properties) {
    try {
        std::string id = properties["id"].value<std::string>();
        int x = properties["x"].value<int>();
        int y = properties["y"].value<int>();
        int w = properties["w"].value<int>();
        int h = properties["h"].value<int>();
        bool filled = false;
        try { filled = properties["filled"].value<bool>(); } catch (...) {}
        bool refresh = false;
        try { refresh = properties["refresh"].value<bool>(); } catch (...) {}

        auto* epaper = Board::GetInstance().GetEpaperDisplay();
        if (epaper == nullptr) {
            return ReturnValue("E-paper display not found on this board.");
        }

        std::string full_id = MakeCanvasId(id);

        if (!CheckCanvasLabelLimit(full_id)) {
            return ReturnValue("Canvas label limit reached (30). Call fridge.canvas.clear first.");
        }

        epaper->AddLabel(String(full_id.c_str()), new EpaperLabel(
            EpaperLabel::Rect(x, y, w, h, filled, GxEPD_BLACK, 1, true, 6)));

        SaveCanvasLayout();
        RefreshCanvasIfNeeded(refresh);

        std::string result = "{\"status\":\"success\",\"id\":\"" + EscapeJsonString(id) +
            "\",\"full_id\":\"" + EscapeJsonString(full_id) +
            "\",\"type\":\"rect\",\"page\":6}";
        return result;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in canvas.add_rect: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleCanvasAddLine(const PropertyList& properties) {
    try {
        std::string id = properties["id"].value<std::string>();
        int x1 = properties["x1"].value<int>();
        int y1 = properties["y1"].value<int>();
        int x2 = properties["x2"].value<int>();
        int y2 = properties["y2"].value<int>();
        int width = 1;
        try { width = properties["width"].value<int>(); } catch (...) {}
        bool refresh = false;
        try { refresh = properties["refresh"].value<bool>(); } catch (...) {}

        auto* epaper = Board::GetInstance().GetEpaperDisplay();
        if (epaper == nullptr) {
            return ReturnValue("E-paper display not found on this board.");
        }

        std::string full_id = MakeCanvasId(id);

        if (!CheckCanvasLabelLimit(full_id)) {
            return ReturnValue("Canvas label limit reached (30). Call fridge.canvas.clear first.");
        }

        epaper->AddLabel(String(full_id.c_str()), new EpaperLabel(
            EpaperLabel::Line(x1, y1, x2, y2, width, GxEPD_BLACK, 1, true, 6)));

        SaveCanvasLayout();
        RefreshCanvasIfNeeded(refresh);

        std::string result = "{\"status\":\"success\",\"id\":\"" + EscapeJsonString(id) +
            "\",\"full_id\":\"" + EscapeJsonString(full_id) +
            "\",\"type\":\"line\",\"page\":6}";
        return result;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in canvas.add_line: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleCanvasRemove(const PropertyList& properties) {
    try {
        std::string id = properties["id"].value<std::string>();
        bool refresh = false;
        try { refresh = properties["refresh"].value<bool>(); } catch (...) {}

        auto* epaper = Board::GetInstance().GetEpaperDisplay();
        if (epaper == nullptr) {
            return ReturnValue("E-paper display not found on this board.");
        }

        std::string full_id = MakeCanvasId(id);
        epaper->RemoveLabel(String(full_id.c_str()));

        SaveCanvasLayout();
        RefreshCanvasIfNeeded(refresh);

        std::string result = "{\"status\":\"success\",\"removed\":\"" + EscapeJsonString(id) + "\"}";
        return result;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in canvas.remove: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleCanvasClear(const PropertyList& properties) {
    try {
        bool refresh = true;
        try { refresh = properties["refresh"].value<bool>(); } catch (...) {}

        auto* epaper = Board::GetInstance().GetEpaperDisplay();
        if (epaper == nullptr) {
            return ReturnValue("E-paper display not found on this board.");
        }

        int removed = epaper->ClearCanvasLabels();

        // 清空后直接删除 layout.json 文件，而不是写空数组
        unlink(CANVAS_LAYOUT_FILE);
        ESP_LOGI(TAG, "Canvas layout file deleted after clear");
        RefreshCanvasIfNeeded(refresh);

        std::string result = "{\"status\":\"success\",\"removed_count\":" + std::to_string(removed) + "}";
        return result;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in canvas.clear: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleCanvasAddImage(const PropertyList& properties) {
    try {
        std::string id = properties["id"].value<std::string>();
        std::string name = properties["name"].value<std::string>();
        int x = properties["x"].value<int>();
        int y = properties["y"].value<int>();
        int w = properties["w"].value<int>();
        int h = properties["h"].value<int>();
        bool refresh = false;
        try { refresh = properties["refresh"].value<bool>(); } catch (...) {}

        auto* epaper = Board::GetInstance().GetEpaperDisplay();
        if (epaper == nullptr) {
            return ReturnValue("E-paper display not found on this board.");
        }

        std::string full_id = MakeCanvasId(id);

        // 检查控件数量上限
        if (!CheckCanvasLabelLimit(full_id)) {
            return ReturnValue("Canvas label limit reached (30). Call fridge.canvas.clear first.");
        }

        // 构造文件路径
        std::string path = std::string("/canvas/") + name;
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) {
            return ReturnValue("Image file not found: " + name + ". Upload it first via POST /api/canvas_image?name=" + name);
        }

        // 读取文件到缓冲区
        size_t total_bytes = w * h / 8;  // 1-bpp: 每像素1位，8像素=1字节
        uint8_t* bitmap = (uint8_t*)malloc(total_bytes);
        if (!bitmap) {
            fclose(f);
            return ReturnValue("Not enough memory for image");
        }

        size_t read = fread(bitmap, 1, total_bytes, f);
        fclose(f);

        if (read < total_bytes) {
            ESP_LOGW(TAG, "Image file smaller than expected: %d/%d bytes", (int)read, (int)total_bytes);
            // 剩余部分填0（白色）
            memset(bitmap + read, 0, total_bytes - read);
        }

        epaper->AddLabel(String(full_id.c_str()), new EpaperLabel(
            EpaperLabel::Bitmap(x, y, bitmap, w, h, 1, 1, false, false, false, true, 6, name.c_str())));

        // 注意：bitmap 指针由 EpaperLabel 持有，不再在这里释放
        SaveCanvasLayout();
        RefreshCanvasIfNeeded(refresh);

        std::string result = "{\"status\":\"success\",\"id\":\"" + EscapeJsonString(id) +
            "\",\"full_id\":\"" + EscapeJsonString(full_id) +
            "\",\"type\":\"image\",\"name\":\"" + EscapeJsonString(name) +
            "\",\"size\":" + std::to_string(read) + ",\"page\":6}";
        return result;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in canvas.add_image: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleCanvasList(const PropertyList& properties) {
    try {
        auto* epaper = Board::GetInstance().GetEpaperDisplay();
        if (epaper == nullptr) {
            return ReturnValue("E-paper display not found on this board.");
        }

        auto* labels = epaper->GetAllLabels();
        if (!labels) {
            return std::string("[]");
        }

        std::string json = "[";
        bool first = true;
        for (const auto& pair : *labels) {
            const String& label_id = pair.first;
            EpaperLabel* label = pair.second;

            if (strncmp(label_id.c_str(), CANVAS_PREFIX, 7) != 0) continue;
            if (label->page != 6) continue;

            std::string short_id = label_id.c_str() + 7;

            if (!first) json += ",";
            first = false;

            switch (label->type) {
                case EpaperObjectType::TEXT:
                    json += "{\"type\":\"text\",\"id\":\"" + EscapeJsonString(short_id) +
                           "\",\"text\":\"" + EscapeJsonString(label->text().c_str()) +
                           "\",\"x\":" + std::to_string((int)label->x) +
                           ",\"y\":" + std::to_string((int)(label->y - label->h + 4)) +
                           ",\"font_size\":" + std::to_string((int)label->h - 4) +
                           ",\"align\":\"" +
                           (label->align == EpaperTextAlign::CENTER ? "center" :
                            label->align == EpaperTextAlign::RIGHT ? "right" : "left") +
                           "\",\"max_width\":" + std::to_string((int)label->w_max) + "}";
                    break;
                case EpaperObjectType::RECT:
                    json += "{\"type\":\"rect\",\"id\":\"" + EscapeJsonString(short_id) +
                           "\",\"x\":" + std::to_string((int)label->x) +
                           ",\"y\":" + std::to_string((int)label->y) +
                           ",\"w\":" + std::to_string((int)label->w) +
                           ",\"h\":" + std::to_string((int)label->h) +
                           ",\"filled\":" + std::string(label->filled ? "true" : "false") + "}";
                    break;
                case EpaperObjectType::LINE:
                    json += "{\"type\":\"line\",\"id\":\"" + EscapeJsonString(short_id) +
                           "\",\"x1\":" + std::to_string((int)label->x) +
                           ",\"y1\":" + std::to_string((int)label->y) +
                           ",\"x2\":" + std::to_string((int)label->x1) +
                           ",\"y2\":" + std::to_string((int)label->y1) +
                           ",\"width\":" + std::to_string((int)label->width) + "}";
                    break;
                case EpaperObjectType::BITMAP:
                    if (label->image_name[0] != '\0') {
                        json += "{\"type\":\"image\",\"id\":\"" + EscapeJsonString(short_id) +
                               "\",\"name\":\"" + EscapeJsonString(label->image_name) +
                               "\",\"x\":" + std::to_string((int)label->x) +
                               ",\"y\":" + std::to_string((int)label->y) +
                               ",\"w\":" + std::to_string((int)label->w) +
                               ",\"h\":" + std::to_string((int)label->h) + "}";
                    }
                    break;
                default:
                    break;
            }
        }
        json += "]";
        return json;
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in canvas.list: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}

ReturnValue FridgeMcpTools::HandleCanvasRefresh(const PropertyList& properties) {
    try {
        auto* epaper = Board::GetInstance().GetEpaperDisplay();
        if (epaper == nullptr) {
            return ReturnValue("E-paper display not found on this board.");
        }

        epaper->ShowCanvasPage();

        return std::string("{\"status\":\"success\",\"page\":6}");
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Error in canvas.refresh: %s", e.what());
        return std::string("Error: ") + e.what();
    }
}
