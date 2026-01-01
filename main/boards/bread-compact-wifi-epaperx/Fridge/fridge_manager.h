#ifndef FRIDGE_MANAGER_H
#define FRIDGE_MANAGER_H

#include "fridge_item.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <map>
#include <ctime>
#include <optional>

#define Fridge_MAX_ITEMS 200  // 最大食材数量
#define Fridge_ID_START 1001   // 食材起始计数ID
#define Fridge_Alert_Days 3   // 过期提前预警天数



// 为 ItemCategory 枚举提供 hash 函数，以支持用作 unordered_map 的键
namespace std {
    template<>
    struct hash<ItemCategory> {
        size_t operator()(const ItemCategory& cat) const {
            return hash<int>()(static_cast<int>(cat));
        }
    };
}

// 查询条件
struct FridgeQuery {
    std::optional<ItemCategory> category;  // 可选的分类过滤
    bool only_expired = false;
    bool expiring_soon = false;
    int expiring_days = 7;  // 默认7天内过期
};

// 统计结果
struct FridgeStatistics {
    int total_items = 0;
    int expired_items = 0;
    int expiring_soon_items = 0;
    std::map<ItemCategory, int> category_count;  // 使用 ItemCategory 枚举作为键
};

// 报警结构
struct FridgeAlert {
    ItemId id;
    AlertLevel level;
    time_t trigger_time;
};

// FridgeManager 类
// NVS key 设计:
//   fridge:item:<id>     -> FridgeItem JSON
//   fridge:meta:last_id  -> ItemId (生成下一个ID用)
class FridgeManager {
public:
    // 单例获取
    static FridgeManager& GetInstance();
    
    // 禁止复制构造和赋值
    FridgeManager(const FridgeManager&) = delete;
    FridgeManager& operator=(const FridgeManager&) = delete;
    
    ~FridgeManager();
    
    // ========== 基础操作 ==========
    // 添加食材（参数列表），内部初始化FridgeItem对象
    ItemId AddItem(const std::string& name, ItemCategory category, 
                   float quantity, const std::string& unit, 
                   time_t expire_time, StorageState state = StorageState::Fresh);
    bool RemoveItem(ItemId id);
    void ClearAllItems();  // 清空所有食材
    bool UpdateItem(const FridgeItem& item);
    bool ConsumeItem(ItemId id, float amount);
    FridgeItem GetItem(ItemId id) const;
    
    // ========== 查询 ==========
    std::vector<FridgeItem> GetAllItems() const;
    std::vector<FridgeItem> Query(const FridgeQuery& query) const;
    std::vector<FridgeItem> ExpiringSoon(int days) const;
    
    // ========== 统计 ==========
    FridgeStatistics GetStatistics() const;
    
    // ========== 报警 ==========
    std::vector<FridgeAlert> UpdateAlerts(time_t now);
    
    // ========== LLM 接口 ==========
    std::string BuildLLMPrompt() const;
    
private:
    // 单例实现
    FridgeManager();
    static FridgeManager* instance_;
    
    // 内存数据
    std::unordered_map<ItemId, FridgeItem> items_;
    std::unordered_multimap<ItemCategory, ItemId> category_index_;  // 使用 ItemCategory 枚举作为键
    std::vector<ItemId> id_list_;  // ID 索引列表，记录所有已存在的物品 ID，用于高效加载和遍历
    
    // NVS 命名空间
    static constexpr const char* NVS_NAMESPACE = "fridge";
    
    // 内部方法
    void LoadFromNVS();
    void SaveItem(const FridgeItem& item);
    void DeleteItemFromNVS(ItemId id);
    ItemId GetNextItemId();
};

#endif
