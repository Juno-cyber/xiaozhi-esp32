#ifndef FRIDGE_ENUM_UTILS_H
#define FRIDGE_ENUM_UTILS_H

#include <string>
#include <ctime>
#include <cstdio>

// ========== 枚举类型定义（使用整数映射）==========

// 存储状态：0=Fresh(新鲜冷藏), 1=Frozen(冷冻)
using StorageState = int;
const StorageState STORAGE_STATE_FRESH = 0;
const StorageState STORAGE_STATE_FROZEN = 1;

// 包装状态：0=Sealed(未开封), 1=Opened(已开封)
using PackageState = int;
const PackageState PACKAGE_STATE_SEALED = 0;
const PackageState PACKAGE_STATE_OPENED = 1;

// 食材分类：0-9对应不同分类
using ItemCategory = int;
const ItemCategory ITEM_CATEGORY_VEGETABLE = 0;   // 蔬菜
const ItemCategory ITEM_CATEGORY_FRUIT = 1;       // 水果
const ItemCategory ITEM_CATEGORY_MEAT = 2;        // 肉类
const ItemCategory ITEM_CATEGORY_EGG = 3;         // 蛋类
const ItemCategory ITEM_CATEGORY_DAIRY = 4;       // 乳制品
const ItemCategory ITEM_CATEGORY_COOKED = 5;      // 熟食
const ItemCategory ITEM_CATEGORY_SEASONING = 6;   // 调味料
const ItemCategory ITEM_CATEGORY_BEVERAGE = 7;    // 饮料
const ItemCategory ITEM_CATEGORY_QUICK = 8;       // 速食食品
const ItemCategory ITEM_CATEGORY_OTHER = 9;       // 其他

// 报警级别：0=None(正常), 1=Warning(即将过期), 2=Critical(已过期)
using AlertLevel = int;
const AlertLevel ALERT_LEVEL_NONE = 0;
const AlertLevel ALERT_LEVEL_WARNING = 1;
const AlertLevel ALERT_LEVEL_CRITICAL = 2;

// ========== 枚举转字符串函数 ==========

// 存储状态转字符串
inline const char* StorageStateToString(StorageState state) {
    static const char* names[] = {
        "Fresh",    // 新鲜冷藏
        "Frozen"    // 冷冻
    };
    if (state < 0 || state >= 2) return "Unknown";
    return names[state];
}

// 包装状态转字符串
inline const char* PackageStateToString(PackageState state) {
    static const char* names[] = {
        "Sealed",   // 未开封
        "Opened"    // 已开封
    };
    if (state < 0 || state >= 2) return "Unknown";
    return names[state];
}

// 食材分类转字符串
inline const char* ItemCategoryToString(ItemCategory category) {
    static const char* names[] = {
        "vegetable",    // 蔬菜
        "fruit",        // 水果
        "meat",         // 肉类
        "egg",          // 蛋类
        "dairy",        // 乳制品
        "cooked",       // 熟食
        "seasoning",    // 调味料
        "beverage",     // 饮料
        "quick",        // 速食食品
        "Other"         // 其他
    };
    if (category < 0 || category >= 10) return "Unknown";
    return names[category];
}

// 报警级别转字符串
inline const char* AlertLevelToString(AlertLevel level) {
    static const char* names[] = {
        "None",     // 正常
        "Warning",  // 即将过期
        "Critical"  // 已过期
    };
    if (level < 0 || level >= 3) return "Unknown";
    return names[level];
}

// 将时间戳转换为可读的日期时间字符串
inline std::string FormatTime(time_t timestamp) {
    if (timestamp == 0) {
        return "N/A";
    }
    struct tm* tm_info = localtime(&timestamp);
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", tm_info);
    return std::string(buffer);
}

// ========== 字符串转枚举反向转换函数 ==========

// 字符串转存储状态
inline StorageState StringToStorageState(const std::string& str) {
    if (str == "Fresh" || str == "fresh") return STORAGE_STATE_FRESH;
    if (str == "Frozen" || str == "frozen") return STORAGE_STATE_FROZEN;
    return STORAGE_STATE_FRESH;  // 默认值
}

// 字符串转包装状态
inline PackageState StringToPackageState(const std::string& str) {
    if (str == "Sealed" || str == "sealed") return PACKAGE_STATE_SEALED;
    if (str == "Opened" || str == "opened") return PACKAGE_STATE_OPENED;
    return PACKAGE_STATE_SEALED;  // 默认值
}

// 字符串转食材分类
inline ItemCategory StringToItemCategory(const std::string& str) {
    if (str == "vegetable") return ITEM_CATEGORY_VEGETABLE;
    if (str == "fruit") return ITEM_CATEGORY_FRUIT;
    if (str == "meat") return ITEM_CATEGORY_MEAT;
    if (str == "egg") return ITEM_CATEGORY_EGG;
    if (str == "dairy") return ITEM_CATEGORY_DAIRY;
    if (str == "cooked") return ITEM_CATEGORY_COOKED;
    if (str == "seasoning") return ITEM_CATEGORY_SEASONING;
    if (str == "beverage") return ITEM_CATEGORY_BEVERAGE;
    if (str == "quick") return ITEM_CATEGORY_QUICK;
    if (str == "Other") return ITEM_CATEGORY_OTHER;
    return ITEM_CATEGORY_OTHER;  // 默认值
}

// 将日期时间字符串解析为时间戳
inline time_t ParseTime(const std::string& time_str) {
    if (time_str.empty() || time_str == "N/A") {
        return 0;
    }
    
    struct tm tm_info = {};
    // 尝试解析 "YYYY-MM-DD HH:MM:SS" 格式
    if (sscanf(time_str.c_str(), "%d-%d-%d %d:%d:%d",
               &tm_info.tm_year, &tm_info.tm_mon, &tm_info.tm_mday,
               &tm_info.tm_hour, &tm_info.tm_min, &tm_info.tm_sec) == 6) {
        tm_info.tm_year -= 1900;
        tm_info.tm_mon -= 1;
        return mktime(&tm_info);
    }
    
    // 如果解析失败，尝试作为整数时间戳
    try {
        return static_cast<time_t>(std::stol(time_str));
    } catch (...) {
        return 0;
    }
}

#endif // FRIDGE_ENUM_UTILS_H
