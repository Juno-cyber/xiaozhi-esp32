#ifndef _EpaperImage_H_
#define _EpaperImage_H_

#if defined(ESP8266) || defined(ESP32)
#include <pgmspace.h>
#else
#include <avr/pgmspace.h>
#endif

// ===============================
//   墨水屏图标位图定义
// ===============================
// 格式: 1位黑白位图 (1=黑色, 0=白色

namespace EpaperImage {

// ============ WiFi 图标 (16x16) ============
extern const unsigned char wifi_full_24x24[72] PROGMEM;      // WiFi 全信号
extern const unsigned char wifi_medium_24x24[72] PROGMEM;    // WiFi 中等信号
extern const unsigned char wifi_weak_24x24[72] PROGMEM;      // WiFi 弱信号
extern const unsigned char wifi_off_24x24[72] PROGMEM;       // WiFi 断开

// ============ 电池图标 (24x12) ============
extern const unsigned char battery_full_24x24[72] PROGMEM;      // 电池满电 100%
extern const unsigned char battery_75_24x24[72] PROGMEM;        // 电池 75%
extern const unsigned char battery_50_24x24[72] PROGMEM;        // 电池 50%
extern const unsigned char battery_25_24x24[72] PROGMEM;        // 电池 25%
extern const unsigned char battery_low_24x24[72] PROGMEM;       // 电池低电量
extern const unsigned char battery_charging_24x24[72] PROGMEM;  // 充电中

// ============ 通知图标 (16x16) ============
extern const unsigned char notification_24x24[72] PROGMEM;   // 消息通知
extern const unsigned char bell_24x24[72] PROGMEM;           // 铃铛
extern const unsigned char mute_24x24[72] PROGMEM;           // 静音

// ============ 状态图标 (16x16) ============
extern const unsigned char mic_16x16[32] PROGMEM;            // 麦克风/聆听
extern const unsigned char loading_16x16[32] PROGMEM;        // 加载/思考
extern const unsigned char speaker_16x16[32] PROGMEM;        // 扬声器/播放
extern const unsigned char warning_16x16[32] PROGMEM;        // 警告/错误
extern const unsigned char check_16x16[32] PROGMEM;          // 成功/勾选

// ============ 箭头图标 (12x12) ============
extern const unsigned char arrow_up_16x16[32] PROGMEM;       // 上箭头
extern const unsigned char arrow_down_16x16[32] PROGMEM;     // 下箭头
extern const unsigned char arrow_left_16x16[32] PROGMEM;     // 左箭头
extern const unsigned char arrow_right_16x16[32] PROGMEM;    // 右箭头

// ============ 表情图标 (32x32) ============
extern const unsigned char EMO_NEUTRAL_32x32[128] PROGMEM;     // 😶 NEUTRAL
extern const unsigned char EMO_HAPPY_32x32[128] PROGMEM;       // 🙂 HAPPY
extern const unsigned char EMO_LAUGHING_32x32[128] PROGMEM;    // 😆 LAUGHING
extern const unsigned char EMO_FUNNY_32x32[128] PROGMEM;       // 😂 FUNNY
extern const unsigned char EMO_SAD_32x32[128] PROGMEM;         // 😔 SAD
extern const unsigned char EMO_ANGRY_32x32[128] PROGMEM;       // 😠 ANGRY
extern const unsigned char EMO_CRYING_32x32[128] PROGMEM;      // 😭 CRYING
extern const unsigned char EMO_LOVING_32x32[128] PROGMEM;      // 😍 LOVING
extern const unsigned char EMO_EMBARRASSED_32x32[128] PROGMEM; // 😳 EMBARRASSED
extern const unsigned char EMO_SURPRISED_32x32[128] PROGMEM;   // 😯 SURPRISED
extern const unsigned char EMO_SHOCKED_32x32[128] PROGMEM;     // 😱 SHOCKED
extern const unsigned char EMO_THINKING_32x32[128] PROGMEM;    // 🤔 THINKING
extern const unsigned char EMO_WINKING_32x32[128] PROGMEM;     // 😉 WINKING
extern const unsigned char EMO_COOL_32x32[128] PROGMEM;        // 😎 COOL
extern const unsigned char EMO_RELAXED_32x32[128] PROGMEM;     // 😌 RELAXED
extern const unsigned char EMO_DELICIOUS_32x32[128] PROGMEM;    // 🤤 DELICIOUS
extern const unsigned char EMO_KISSY_32x32[128] PROGMEM;        // 😘 KISSY
extern const unsigned char EMO_CONFIDENT_32x32[128] PROGMEM;    // 😏 CONFIDENT
extern const unsigned char EMO_SLEEPY_32x32[128] PROGMEM;       // 😴 SLEEPY
extern const unsigned char EMO_SILLY_32x32[128] PROGMEM;        // 😜 SILLY
extern const unsigned char EMO_CONFUSED_32x32[128] PROGMEM;     // 🙄 CONFUSED

// ============ 冰箱的icon (32x32) ============
extern const unsigned char Fridge_32x32[128] PROGMEM;           // 冰箱
extern const unsigned char Fridge_warning_32x32[128] PROGMEM;          // 告警
extern const unsigned char Fridge_category_32x32[128] PROGMEM;          // 分类
// ============ 冰箱的icon (24x24) ============
extern const unsigned char Fridge_24x24[72] PROGMEM;           // 冰箱
extern const unsigned char Fridge_warning_24x24[72] PROGMEM;          // 告警
extern const unsigned char Fridge_category_24x24[72] PROGMEM;          // 分类
// ============ 食材icon (24x24) ============
extern const unsigned char food_vegetable_24x24[72] PROGMEM;     // 蔬菜
extern const unsigned char food_fruit_24x24[72] PROGMEM;         // 水果
extern const unsigned char food_meat_24x24[72] PROGMEM;           // 肉类
extern const unsigned char food_egg_24x24[72] PROGMEM;            // 蛋类
extern const unsigned char food_dairy_24x24[72] PROGMEM;          // 乳制品
extern const unsigned char food_cooked_24x24[72] PROGMEM;         // 熟食
extern const unsigned char food_seasoning_24x24[72] PROGMEM;      // 调味
extern const unsigned char food_beverage_24x24[72] PROGMEM;       // 饮料
extern const unsigned char food_quick_24x24[72] PROGMEM;         // 速食食品
extern const unsigned char food_other_24x24[72] PROGMEM;          // 其他
// ============ 食材icon (72x72) ============
extern const unsigned char food_cooker_72x72[648] PROGMEM;          // cooker
}  // namespace EpaperImage



#endif  // _EpaperImage_H_
