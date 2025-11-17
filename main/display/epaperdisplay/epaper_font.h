#pragma once

// ===============================
//   Siji 图标字体索引定义
// ===============================
// 使用 u8g2_font_siji_t_6x10 字体

namespace EpaperFont {

// WiFi 图标 (Siji字体)
namespace Siji {
    constexpr const char* WIFI_DISCONNECTED = "\ue217";  // WiFi 未连接
    constexpr const char* WIFI_WEAK        = "\ue218";  // WiFi 弱信号
    constexpr const char* WIFI_MEDIUM      = "\ue219";  // WiFi 中信号
    constexpr const char* WIFI_STRONG      = "\ue21a";  // WiFi 强信号
}

// Emoticons 图标 (u8g2_font_emoticons21_tr)
namespace Emoticons {
    constexpr const char* NEUTRAL     = "\u0036";  // 😶
    constexpr const char* HAPPY       = "\u0021";  // 🙂
    constexpr const char* LAUGHING    = "\u0036";  // 😆
    constexpr const char* FUNNY       = "\u0023";  // 😂
    constexpr const char* SAD         = "\u0036";  // 😔
    constexpr const char* ANGRY       = "\u0028";  // 😠
    constexpr const char* CRYING      = "\u0027";  // 😭
    constexpr const char* LOVING      = "\u0033";  // 😍
    constexpr const char* EMBARRASSED = "\u0034";  // 😳
    constexpr const char* SURPRISED   = "\u0035";  // 😯
    constexpr const char* SHOCKED     = "\u0035";  // 😱
    constexpr const char* THINKING    = "\u0036";  // 🤔
    constexpr const char* WINKING     = "\u0030";  // 😉
    constexpr const char* COOL        = "\u0036";  // 😎
    constexpr const char* RELAXED     = "\u0037";  // 😌
    constexpr const char* DELICIOUS   = "\u0031";  // 🤤
    constexpr const char* KISSY       = "\u0033";  // 😘
    constexpr const char* CONFIDENT   = "\u0036";  // 😏
    constexpr const char* SLEEPY      = "\u0029";  // 😴
    constexpr const char* SILLY       = "\u0024";  // 😜
    constexpr const char* CONFUSED    = "\u0029";  // 🙄
}

}  // namespace EpaperFont
