#include "fridge_item.h"
#include "fridge_enum_utils.h"
#include <ctime>
#include <cmath>
#include <cstdio>
#include "cJSON.h"

// 检查是否过期
bool FridgeItem::IsExpired(time_t now) const {
    return expire_time > 0 && now >= expire_time;
}

// 计算剩余天数
int FridgeItem::RemainingDays(time_t now) const {
    if (expire_time <= 0) {
        return -1;  // 无有效过期时间
    }
    
    if (IsExpired(now)) {
        return 0;  // 已过期
    }
    
    time_t remaining_seconds = expire_time - now;
    int remaining_days = static_cast<int>(std::ceil(remaining_seconds / 86400.0));
    return remaining_days;
}

// 获取警告级别
AlertLevel FridgeItem::GetAlertLevel(time_t now) const {
    if (IsExpired(now)) {
        return ALERT_LEVEL_CRITICAL;  // 已过期
    }
    
    int remaining_days = RemainingDays(now);
    
    // 剩余天数 <= 3 天为警告
    if (remaining_days > 0 && remaining_days <= 3) {
        return ALERT_LEVEL_WARNING;
    }
    
    return ALERT_LEVEL_NONE;  // 正常
}

// 转换为JSON字符串
std::string FridgeItem::ToJson() const {
    cJSON* j = cJSON_CreateObject();
    
    cJSON_AddNumberToObject(j, "id", id);
    cJSON_AddStringToObject(j, "name", name.c_str());
    cJSON_AddNumberToObject(j, "category", category);
    cJSON_AddNumberToObject(j, "quantity", quantity);
    cJSON_AddStringToObject(j, "unit", unit.c_str());
    cJSON_AddNumberToObject(j, "state", state);
    cJSON_AddNumberToObject(j, "package_state", package_state);
    cJSON_AddNumberToObject(j, "add_time", add_time);
    cJSON_AddNumberToObject(j, "expire_time", expire_time);
    cJSON_AddNumberToObject(j, "last_update_time", last_update_time);
    cJSON_AddNumberToObject(j, "open_time", open_time);
    
    // 序列化消耗记录
    cJSON* consume_array = cJSON_CreateArray();
    for (const auto& record : consume_history) {
        cJSON* record_obj = cJSON_CreateObject();
        cJSON_AddNumberToObject(record_obj, "time", record.time);
        cJSON_AddNumberToObject(record_obj, "amount", record.amount);
        cJSON_AddItemToArray(consume_array, record_obj);
    }
    cJSON_AddItemToObject(j, "consume_history", consume_array);
    
    char* json_str = cJSON_PrintUnformatted(j);
    std::string result(json_str ? json_str : "");
    free(json_str);
    cJSON_Delete(j);
    
    return result;
}

// 转换为MCP通信用的JSON字符串（包含格式化和计算字段）
std::string FridgeItem::ToMcpJson() const {
    time_t now = std::time(nullptr);
    
    cJSON* j = cJSON_CreateObject();
    
    cJSON_AddNumberToObject(j, "item_id", id);
    cJSON_AddStringToObject(j, "name", name.c_str());
    cJSON_AddStringToObject(j, "category", ItemCategoryToString(category));
    cJSON_AddNumberToObject(j, "quantity", quantity);
    cJSON_AddStringToObject(j, "unit", unit.c_str());
    cJSON_AddStringToObject(j, "storage_state", StorageStateToString(state));
    cJSON_AddStringToObject(j, "package_state", PackageStateToString(package_state));
    cJSON_AddStringToObject(j, "add_time", FormatTime(add_time).c_str());
    cJSON_AddStringToObject(j, "expire_time", FormatTime(expire_time).c_str());
    cJSON_AddNumberToObject(j, "remaining_days", RemainingDays(now));
    cJSON_AddStringToObject(j, "alert_level", AlertLevelToString(GetAlertLevel(now)));
    cJSON_AddBoolToObject(j, "is_expired", IsExpired(now));
    
    char* json_str = cJSON_PrintUnformatted(j);
    std::string result(json_str ? json_str : "");
    free(json_str);
    cJSON_Delete(j);
    
    return result;
}

// 从JSON字符串创建对象
FridgeItem FridgeItem::FromJson(const std::string& json_str) {
    FridgeItem item;
    
    cJSON* j = cJSON_Parse(json_str.c_str());
    if (!j) {
        return item;  // JSON 解析失败，返回默认对象
    }
    
    // 解析基本字段
    cJSON* id_obj = cJSON_GetObjectItem(j, "id");
    if (id_obj && cJSON_IsNumber(id_obj)) {
        item.id = static_cast<ItemId>(id_obj->valuedouble);
    }
    
    cJSON* name_obj = cJSON_GetObjectItem(j, "name");
    if (name_obj && cJSON_IsString(name_obj)) {
        item.name = name_obj->valuestring;
    }
    
    cJSON* category_obj = cJSON_GetObjectItem(j, "category");
    if (category_obj && cJSON_IsNumber(category_obj)) {
        item.category = static_cast<ItemCategory>(static_cast<int>(category_obj->valuedouble));
    }
    
    cJSON* quantity_obj = cJSON_GetObjectItem(j, "quantity");
    if (quantity_obj && cJSON_IsNumber(quantity_obj)) {
        item.quantity = static_cast<float>(quantity_obj->valuedouble);
    }
    
    cJSON* unit_obj = cJSON_GetObjectItem(j, "unit");
    if (unit_obj && cJSON_IsString(unit_obj)) {
        item.unit = unit_obj->valuestring;
    }
    
    cJSON* state_obj = cJSON_GetObjectItem(j, "state");
    if (state_obj && cJSON_IsNumber(state_obj)) {
        item.state = static_cast<StorageState>(static_cast<int>(state_obj->valuedouble));
    }
    
    cJSON* package_state_obj = cJSON_GetObjectItem(j, "package_state");
    if (package_state_obj && cJSON_IsNumber(package_state_obj)) {
        item.package_state = static_cast<PackageState>(static_cast<int>(package_state_obj->valuedouble));
    }
    
    cJSON* add_time_obj = cJSON_GetObjectItem(j, "add_time");
    if (add_time_obj && cJSON_IsNumber(add_time_obj)) {
        item.add_time = static_cast<time_t>(add_time_obj->valuedouble);
    }
    
    cJSON* expire_time_obj = cJSON_GetObjectItem(j, "expire_time");
    if (expire_time_obj && cJSON_IsNumber(expire_time_obj)) {
        item.expire_time = static_cast<time_t>(expire_time_obj->valuedouble);
    }
    
    cJSON* last_update_obj = cJSON_GetObjectItem(j, "last_update_time");
    if (last_update_obj && cJSON_IsNumber(last_update_obj)) {
        item.last_update_time = static_cast<time_t>(last_update_obj->valuedouble);
    }
    
    cJSON* open_time_obj = cJSON_GetObjectItem(j, "open_time");
    if (open_time_obj && cJSON_IsNumber(open_time_obj)) {
        item.open_time = static_cast<time_t>(open_time_obj->valuedouble);
    }
    
    // 反序列化消耗记录
    cJSON* consume_history_obj = cJSON_GetObjectItem(j, "consume_history");
    if (consume_history_obj && cJSON_IsArray(consume_history_obj)) {
        cJSON* record_item = NULL;
        cJSON_ArrayForEach(record_item, consume_history_obj) {
            ConsumeRecord record;
            
            cJSON* time_obj = cJSON_GetObjectItem(record_item, "time");
            if (time_obj && cJSON_IsNumber(time_obj)) {
                record.time = static_cast<time_t>(time_obj->valuedouble);
            }
            
            cJSON* amount_obj = cJSON_GetObjectItem(record_item, "amount");
            if (amount_obj && cJSON_IsNumber(amount_obj)) {
                record.amount = static_cast<float>(amount_obj->valuedouble);
            }
            
            item.consume_history.push_back(record);
        }
    }
    
    cJSON_Delete(j);
    return item;
}

// 从MCP通信格式的JSON创建对象
FridgeItem FridgeItem::FromMcpJson(const std::string& json_str) {
    FridgeItem item;
    
    cJSON* j = cJSON_Parse(json_str.c_str());
    if (!j) {
        return item;
    }
    
    // item_id 字段
    cJSON* item_id_obj = cJSON_GetObjectItem(j, "item_id");
    if (item_id_obj && cJSON_IsNumber(item_id_obj)) {
        item.id = static_cast<ItemId>(item_id_obj->valuedouble);
    }
    
    // name 字段
    cJSON* name_obj = cJSON_GetObjectItem(j, "name");
    if (name_obj && cJSON_IsString(name_obj)) {
        item.name = name_obj->valuestring;
    }
    
    // category 字段（字符串格式）
    cJSON* category_obj = cJSON_GetObjectItem(j, "category");
    if (category_obj && cJSON_IsString(category_obj)) {
        item.category = StringToItemCategory(category_obj->valuestring);
    }
    
    // quantity 字段
    cJSON* quantity_obj = cJSON_GetObjectItem(j, "quantity");
    if (quantity_obj && cJSON_IsNumber(quantity_obj)) {
        item.quantity = static_cast<float>(quantity_obj->valuedouble);
    }
    
    // unit 字段
    cJSON* unit_obj = cJSON_GetObjectItem(j, "unit");
    if (unit_obj && cJSON_IsString(unit_obj)) {
        item.unit = unit_obj->valuestring;
    }
    
    // storage_state 字段（字符串格式）
    cJSON* storage_state_obj = cJSON_GetObjectItem(j, "storage_state");
    if (storage_state_obj && cJSON_IsString(storage_state_obj)) {
        item.state = StringToStorageState(storage_state_obj->valuestring);
    }
    
    // package_state 字段（字符串格式）
    cJSON* package_state_obj = cJSON_GetObjectItem(j, "package_state");
    if (package_state_obj && cJSON_IsString(package_state_obj)) {
        item.package_state = StringToPackageState(package_state_obj->valuestring);
    }
    
    // add_time 字段（日期时间字符串格式）
    cJSON* add_time_obj = cJSON_GetObjectItem(j, "add_time");
    if (add_time_obj && cJSON_IsString(add_time_obj)) {
        item.add_time = ParseTime(add_time_obj->valuestring);
    }
    
    // expire_time 字段（日期时间字符串格式）
    cJSON* expire_time_obj = cJSON_GetObjectItem(j, "expire_time");
    if (expire_time_obj && cJSON_IsString(expire_time_obj)) {
        item.expire_time = ParseTime(expire_time_obj->valuestring);
    }
    
    // 初始化 last_update_time 和 open_time
    item.last_update_time = std::time(nullptr);
    item.open_time = 0;  // 新添加的食材未开封
    
    cJSON_Delete(j);
    return item;
}
