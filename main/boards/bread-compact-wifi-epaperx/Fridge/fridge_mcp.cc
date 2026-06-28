#include "fridge_mcp.h"
#include "board.h"
#include "display/epaperdisplay/epaper_display.h"
#include <esp_log.h>
#include <sstream>

static const char* TAG = "FridgeMCP";

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
    page_props.AddProperty(Property("target_page", kPropertyTypeInteger, 1, 5));

    mcp_server.AddTool("fridge.pagemanager",
        "Switch the e-paper display page to show different fridge information. "
        "1: CHAT_PAGE (Clock/Summary), 2: FRIDGE_STATS_PAGE (Statistics), 3: FOOD_LIST_PAGE (Item list), 4: RECIPE_PAGE (AI Recipes), 5: HOME_PIC_DISPLAY (Home Picture). "
        "当需要查看冰箱统计、食材列表或AI菜谱时切换页面。",
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
        "1. `fridge_only`: only use ingredients already in the fridge.\n"
        "2. `mixed_purchase`: use some fridge ingredients and buy some extra ingredients.\n"
        "Fill the recipe in this normalized format: recommendation mode, dish name, brief recommendation reason, required ingredients, "
        "extra ingredients to buy when needed, and cooking time. "
        "The device will return the current fridge inventory snapshot together with the rendered recipe result.",
        recipe_props,
        [this](const PropertyList& properties) -> ReturnValue {
            return HandleRecipeRecommend(properties);
        });
    
    ESP_LOGI(TAG, "FridgeMcpTools initialized with 10 tools");
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
        if (page < 1 || page > 5) {
            return ReturnValue("Invalid page index. Must be between 1 and 5.");
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

        if (recommendation_mode == "mixed_purchase" && extra_ingredients.empty()) {
            return std::string("extra_ingredients cannot be empty when recommendation_mode is `mixed_purchase`.");
        }

        auto* epaper = Board::GetInstance().GetEpaperDisplay();
        if (epaper == nullptr) {
            return ReturnValue("E-paper display not found on this board.");
        }

        auto inventory_items = FridgeManager::GetInstance().GetAllItems();
        std::string recipe_text = BuildRecipeDisplayText(
            recommendation_mode,
            dish_name,
            summary,
            required_ingredients,
            extra_ingredients,
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
        result_json += ",\"extra_ingredients\":\"" + EscapeJsonString(extra_ingredients) + "\"";
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
