#include "fridge_mcp.h"
#include <esp_log.h>

static const char* TAG = "FridgeMCP";

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
    
    ESP_LOGI(TAG, "FridgeMcpTools initialized with 2 tools");
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
