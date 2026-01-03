#ifndef FRIDGE_ITEM_H
#define FRIDGE_ITEM_H

#include <string>
#include <vector>
#include <ctime>
#include "fridge_enum_utils.h"

using ItemId = uint32_t;

// 食材消耗记录
struct ConsumeRecord {
    time_t time;
    float amount;
};

// 冰箱食材项
class FridgeItem {
private:
    static constexpr size_t MAX_CONSUME_RECORDS = 4;  // 最多保存4条消耗记录

public:
    ItemId id;                     // 唯一 ID
    std::string name;              // 食材名称
    ItemCategory category;         // 分类
    float quantity;                // 数量 / 重量
    std::string unit;              // g / ml / 个
    StorageState state;            // 存储状态
    PackageState package_state;    // 包装状态
    time_t add_time;               // 添加时间
    time_t expire_time;            // 过期时间
    time_t last_update_time;       // 最近更新时间
    time_t open_time;              // 第一次开封时间（Opened 时）
    std::vector<ConsumeRecord> consume_history;

public:
    FridgeItem() : id(0), name(""), category(ITEM_CATEGORY_OTHER), quantity(0.0f), unit(""),
                   state(STORAGE_STATE_FRESH), package_state(PACKAGE_STATE_SEALED), 
                   add_time(0), expire_time(0), last_update_time(0), open_time(0) {}
    
    // 添加消耗记录，自动维护长度限制
    void AddConsumeRecord(const ConsumeRecord& record) {
        consume_history.push_back(record);
        if (consume_history.size() > MAX_CONSUME_RECORDS) {
            consume_history.erase(consume_history.begin());
        }
    }
    
    bool IsExpired(time_t now) const;
    int RemainingDays(time_t now) const;
    AlertLevel GetAlertLevel(time_t now) const;
    // 用于数据持久化的JSON转换（包含所有内部字段）
    std::string ToJson() const;
    static FridgeItem FromJson(const std::string& json);
    
    // 用于MCP通信的JSON转换（格式化可读的字段，包含计算字段如remaining_days等）
    std::string ToMcpJson() const;
    // 从MCP通信格式的JSON创建对象（与ToMcpJson()对应，用于LLM下发的食材数据）
    static FridgeItem FromMcpJson(const std::string& json);
};

inline FridgeItem CreateFridgeItem(ItemId id, const std::string& name, 
                                   ItemCategory category, float quantity,
                                   const std::string& unit, time_t expire_time,
                                   StorageState state = STORAGE_STATE_FRESH) {
    FridgeItem item;
    item.id = id;
    item.name = name;
    item.category = category;
    item.quantity = quantity;
    item.unit = unit;
    item.state = state;
    item.expire_time = expire_time;
    item.add_time = std::time(nullptr);
    item.last_update_time = std::time(nullptr);
    item.package_state = PACKAGE_STATE_SEALED;
    item.open_time = 0;
    return item;
}

#endif
