#include "fridge_manager.h"
#include "../../../settings.h"
#include <esp_log.h>
#include <algorithm>
#include <ctime>
#include <unordered_set>

static const char* TAG = "FridgeManager";   

// 单例实现
FridgeManager* FridgeManager::instance_ = nullptr;

FridgeManager& FridgeManager::GetInstance() {
    if (instance_ == nullptr) {
        instance_ = new FridgeManager();
    }
    return *instance_;
}

// 构造函数
FridgeManager::FridgeManager() {
    LoadFromNVS();
}

// 析构函数
FridgeManager::~FridgeManager() = default;

// ========== 内部方法 ==========

// 从 NVS 加载所有食材数据
void FridgeManager::LoadFromNVS() {
    items_.clear();
    category_index_.clear();
    id_list_.clear();
    
    Settings settings(NVS_NAMESPACE, false);
    
    int loaded_count = 0;
    
    for (int i = 0; i < Fridge_MAX_ITEMS; ++i) {
        ItemId id = Fridge_ID_START + i;
        std::string key = "item:" + std::to_string(id);
        std::string json_str = settings.GetString(key, "");
        
        if (!json_str.empty()) {
            try {
                FridgeItem item = FridgeItem::FromJson(json_str);
                items_[id] = item;
                category_index_.insert({item.category, item.id});
                id_list_.push_back(id);  // 编构時动态的生成 ID 列表
                loaded_count++;
                ESP_LOGD(TAG, "Loaded item ID=%lu from NVS: %s", id, item.name.c_str());
            } catch (const std::exception& e) {
                ESP_LOGW(TAG, "Failed to load item ID=%lu: %s", id, e.what());
            }
        }
    }
    
    if (loaded_count > 0) {
        ESP_LOGI(TAG, "Loaded %d items from NVS", loaded_count);
    } else {
        ESP_LOGI(TAG, "No items found in NVS (first time startup)");
    }
}

// 保存单个食材到 NVS
void FridgeManager::SaveItem(const FridgeItem& item) {
    Settings settings(NVS_NAMESPACE, true);
    
    std::string key = "item:" + std::to_string(item.id);
    std::string json_str = item.ToJson();
    
    settings.SetString(key, json_str);
    ESP_LOGI(TAG, "Saved item ID=%lu (name=%s) to NVS, json_len=%u",
             item.id, item.name.c_str(), static_cast<unsigned int>(json_str.length()));
}

// 从 NVS 删除食材
void FridgeManager::DeleteItemFromNVS(ItemId id) {
    Settings settings(NVS_NAMESPACE, true);
    
    std::string key = "item:" + std::to_string(id);
    settings.EraseKey(key);
    ESP_LOGV(TAG, "Deleted item ID=%lu from NVS", id);
}

// 获取下一个物品 ID
// 从 Fridge_MAX_ITEMS 开始，找到第一个未被占用的 ID
ItemId FridgeManager::GetNextItemId() {
    // 将 id_list_ 转换为 set 便于快速查找
    std::unordered_set<ItemId> used_ids(id_list_.begin(), id_list_.end());
    
    // 从 Fridge_MAX_ITEMS 开始搜索第一个未被占用的 ID
    ItemId next_id = Fridge_ID_START;
    while (used_ids.find(next_id) != used_ids.end()) {
        next_id++;
    }
    
    ESP_LOGD(TAG, "GetNextItemId: found available ID=%lu", next_id);
    return next_id;
}

// ========== 基础操作 ==========

// 添加食材
ItemId FridgeManager::AddItem(const std::string& name, ItemCategory category, 
                              float quantity, const std::string& unit, 
                              time_t expire_time, StorageState state) {
    // 生成新的 ID
    ItemId new_id = GetNextItemId();
    
    // 使用工厂函数创建完整的 FridgeItem 对象
    FridgeItem new_item = CreateFridgeItem(new_id, name, category, quantity, 
                                           unit, expire_time, state);
    
    // 添加到内存
    items_[new_id] = new_item;
    category_index_.insert({new_item.category, new_id});
    id_list_.push_back(new_id);
    
    // 保存到 NVS
    SaveItem(new_item);
    
    ESP_LOGI(TAG, "Added item ID=%lu, name=%s, category=%d",
             new_id, new_item.name.c_str(), static_cast<int>(new_item.category));
    
    return new_id;
}

// 删除食材
bool FridgeManager::RemoveItem(ItemId id) {
    auto it = items_.find(id);
    if (it == items_.end()) {
        ESP_LOGW(TAG, "Item ID=%lu not found", id);
        return false;
    }
    
    ItemCategory category = it->second.category;
    
    // 从索引中移除
    auto cat_it = category_index_.equal_range(category);
    for (auto iter = cat_it.first; iter != cat_it.second; ++iter) {
        if (iter->second == id) {
            category_index_.erase(iter);
            break;
        }
    }
    
    // 从内存移除
    items_.erase(it);
    
    // 从 ID 列表中移除（不需要保存，下次启动重新构造）
    auto id_it = std::find(id_list_.begin(), id_list_.end(), id);
    if (id_it != id_list_.end()) {
        id_list_.erase(id_it);
    }
    
    // 从 NVS 删除
    DeleteItemFromNVS(id);
    
    ESP_LOGI(TAG, "Removed item ID=%lu", id);
    return true;
}

// 清空所有食材
void FridgeManager::ClearAllItems() {
    // 跨床清空itemems_、category_index_、id_list_三个数据结构
    std::vector<ItemId> ids_to_delete = id_list_;  // 拷贝一份ID列表，以便安全地遍历
    
    for (ItemId id : ids_to_delete) {
        RemoveItem(id);
    }
    
    ESP_LOGI(TAG, "Cleared all items (%zu items deleted)", ids_to_delete.size());
}

// 更新食材
bool FridgeManager::UpdateItem(const FridgeItem& item) {
    auto it = items_.find(item.id);
    if (it == items_.end()) {
        ESP_LOGW(TAG, "Item ID=%lu not found", item.id);
        return false;
    }
    
    // 如果分类改变，更新索引
    if (it->second.category != item.category) {
        // 移除旧的索引
        auto cat_it = category_index_.equal_range(it->second.category);
        for (auto iter = cat_it.first; iter != cat_it.second; ++iter) {
            if (iter->second == item.id) {
                category_index_.erase(iter);
                break;
            }
        }
        
        // 添加新的索引
        category_index_.insert({item.category, item.id});
    }
    
    // 更新内存
    items_[item.id] = item;
    
    // 保存到 NVS
    SaveItem(item);
    
    ESP_LOGI(TAG, "Updated item ID=%lu, name=%s", item.id, item.name.c_str());
    return true;
}

// 消耗食材
bool FridgeManager::ConsumeItem(ItemId id, float amount) {
    auto it = items_.find(id);
    if (it == items_.end()) {
        ESP_LOGW(TAG, "Item ID=%lu not found", id);
        return false;
    }
    
    // 检查数量是否充足
    if (it->second.quantity < amount) {
        ESP_LOGW(TAG, "Item ID=%lu: insufficient quantity (have=%.2f, consume=%.2f)",
                 id, it->second.quantity, amount);
        return false;
    }
    
    // 减少数量
    it->second.quantity -= amount;
    it->second.last_update_time = std::time(nullptr);
    
    // 添加消耗记录
    ConsumeRecord record;
    record.time = std::time(nullptr);
    record.amount = amount;
    it->second.AddConsumeRecord(record);
    
    // 保存到 NVS
    SaveItem(it->second);
    
    ESP_LOGI(TAG, "Consumed %.2f from item ID=%lu, remaining=%.2f",
             amount, id, it->second.quantity);
    return true;
}

// 获取食材
FridgeItem FridgeManager::GetItem(ItemId id) const {
    auto it = items_.find(id);
    if (it == items_.end()) {
        ESP_LOGW(TAG, "Item ID=%lu not found", id);
        return FridgeItem();  // 返回默认对象
    }
    
    return it->second;
}

// ========== 查询 ==========

// 获取所有食材
std::vector<FridgeItem> FridgeManager::GetAllItems() const {
    std::vector<FridgeItem> result;
    for (const auto& pair : items_) {
        result.push_back(pair.second);
    }
    ESP_LOGD(TAG, "GetAllItems: returned %d items", static_cast<int>(result.size()));
    return result;
}

// 按条件查询食材
std::vector<FridgeItem> FridgeManager::Query(const FridgeQuery& query) const {
    std::vector<FridgeItem> result;
    time_t now = std::time(nullptr);
    
    for (const auto& pair : items_) {
        const FridgeItem& item = pair.second;
        
        // 检查分类过滤
        if (query.category.has_value() && item.category != query.category.value()) {
            continue;
        }
        
        // 检查过期过滤
        if (query.only_expired && !item.IsExpired(now)) {
            continue;
        }
        
        // 检查即将过期过滤
        if (query.expiring_soon) {
            int remaining = item.RemainingDays(now);
            if (remaining < 0 || remaining > query.expiring_days) {
                continue;  // 已过期或距离过期还很远
            }
        }
        
        result.push_back(item);
    }
    
    ESP_LOGD(TAG, "Query: returned %zu items", result.size());
    return result;
}

// 获取即将过期的食材
std::vector<FridgeItem> FridgeManager::ExpiringSoon(int days) const {
    FridgeQuery query;
    query.expiring_soon = true;
    query.expiring_days = days;
    return Query(query);
}

// ========== 统计 ==========

// 获取统计信息
FridgeStatistics FridgeManager::GetStatistics() const {
    FridgeStatistics stats;
    time_t now = std::time(nullptr);
    
    stats.total_items = items_.size();
    
    // 初始化各分类计数为 0
    for (int i = 0; i < ITEM_CATEGORY_OTHER + 1; ++i) {
        stats.category_count[static_cast<ItemCategory>(i)] = 0;
    }
    
    // 遍历所有食材统计
    for (const auto& pair : items_) {
        const FridgeItem& item = pair.second;
        
        // 统计分类数量
        stats.category_count[item.category]++;
        
        // 统计过期数量
        if (item.IsExpired(now)) {
            stats.expired_items++;
        }
        // 统计即将过期数量（Fridge_Alert_Days天内过期，但还未过期）
        else if (item.RemainingDays(now) <= Fridge_Alert_Days) {
            stats.expiring_soon_items++;
        }
    }
    
    ESP_LOGI(TAG, "GetStatistics RESULT: total=%d, expired=%d, expiring_soon=%d",
             stats.total_items, stats.expired_items, stats.expiring_soon_items);
    return stats;
}

// ========== 报警 ==========

// 更新报警列表
std::vector<FridgeAlert> FridgeManager::UpdateAlerts(time_t now) {
    std::vector<FridgeAlert> alerts;
    
    for (const auto& pair : items_) {
        const FridgeItem& item = pair.second;
        AlertLevel level = item.GetAlertLevel(now);
        
        // 只添加有警告的食材
        if (level != ALERT_LEVEL_NONE) {
            FridgeAlert alert;
            alert.id = item.id;
            alert.level = level;
            alert.trigger_time = now;
            alerts.push_back(alert);
        }
    }
    
    ESP_LOGD(TAG, "UpdateAlerts: found %zu alerts", alerts.size());
    return alerts;
}