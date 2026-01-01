#pragma once
#include <cstdint>
#include <Arduino.h>
#include <functional>
#include <vector>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>

// ===============================
//   Epaper Label 抽象对象类型
// ===============================

enum class EpaperObjectType {
    TEXT,
    RECT,
    BITMAP,
    LINE,
    CIRCLE,
    TRIANGLE,           // 三角形
    ROUND_RECT,        // 圆角矩形
    PIXEL              // 像素点
};

enum class EpaperTextAlign {
    LEFT,
    CENTER,
    RIGHT
};

class EpaperLabel {
public:
    // 文本包装类，支持静态字符串和动态函数
    class TextValue {
        std::function<String()> func_;
    public:
        TextValue() : func_([]() { return String(""); }) {}
        TextValue(const char* s) {
            String str(s);
            func_ = [str]() { return str; };
        }
        TextValue(const String& s) {
            func_ = [s]() { return s; };
        }
        template<typename F>
        TextValue(F f) : func_(f) {}
        
        TextValue& operator=(const char* s) {
            String str(s);
            func_ = [str]() { return str; };
            return *this;
        }
        TextValue& operator=(const String& s) {
            func_ = [s]() { return s; };
            return *this;
        }
        template<typename F>
        TextValue& operator=(F f) {
            func_ = f;
            return *this;
        }

        String operator()() const { return func_(); }
    };

    // 类型
    EpaperObjectType type;

    // 公共属性
    int16_t x = 0, y = 0;
    uint16_t w = 0, h = 0;  // 实际渲染的宽度和高度（动态更新）
    uint16_t color = GxEPD_BLACK;
    uint8_t rotation = 1;
    bool mirror_h = false;  // 水平镜像
    bool mirror_v = false;  // 垂直镜像
    bool visible = true;    // 显示/隐藏属性
    uint16_t page = 1;      // 所属页面编号

    // 文本属性
    TextValue text;
    const uint8_t* u8g2_font = nullptr;         // U8g2 字体指针
    EpaperTextAlign align = EpaperTextAlign::LEFT;
    uint16_t w_max = 0;  // 文本最大宽度限制（用于换行）
    bool invert = false;     // 文本反色（背景色与前景色互换）

    // 位图属性
    const uint8_t* bitmap = nullptr;
    uint16_t depth = 1;      // 1=黑白, 3=三色, 7=七色

    // 线段/三角形属性
    int16_t x1 = 0, y1 = 0;
    int16_t x2 = 0, y2 = 0;  // 三角形第三个点
    uint8_t width = 1;        // 线宽（用于 LINE 类型）
    
    // 矩形/圆形属性
    bool filled = false;
    
    // 圆形/圆角矩形属性
    uint16_t radius = 0;

    // --- 工厂函数们 ---
    
    // 文本（统一使用 U8g2 字体，支持中英文）
    // 参数说明：
    //   x, y: 文本左上角位置（与其他 label 类型保持一致）
    //   max_width: 文本最大宽度限制（用于换行），0表示不限制
    //   font_height: 字体高度（用于计算实际绘制的y坐标偏移）
    //   h: 文本框高度（用于垂直对齐等）
    static EpaperLabel Text(TextValue text, int16_t x, int16_t y, uint16_t max_width, uint16_t h, uint16_t font_height,
                            const uint8_t* u8g2_font,
                            uint16_t color = GxEPD_BLACK,
                            EpaperTextAlign align = EpaperTextAlign::LEFT,
                            uint8_t rotation = 1,
                            bool visible = true,
                            bool invert = false,                            
                            uint16_t page = 1
                            ) {
        EpaperLabel obj;
        obj.type = EpaperObjectType::TEXT;
        obj.text = text;
        obj.x = x; 
        // 自动调整 y 坐标：由于文本从基线开始绘制，需要加上字体高度以对齐到左上角
        obj.y = y + font_height;
        obj.w_max = max_width; // 使用 w_max 存储最大宽度限制
        obj.h = h;        
        obj.u8g2_font = u8g2_font;
        obj.color = color;
        obj.align = align;
        obj.rotation = rotation;
        obj.visible = visible;
        obj.invert = invert;        
        obj.page = page;
        return obj;
    }

    // 矩形
    static EpaperLabel Rect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                            bool filled = false, uint16_t color = GxEPD_BLACK, uint8_t rotation = 1,
                            bool visible = true, uint16_t page = 1) {
        EpaperLabel obj;
        obj.type = EpaperObjectType::RECT;
        obj.x = x; obj.y = y;
        obj.w = w; obj.h = h;
        obj.filled = filled;
        obj.color = color;
        obj.rotation = rotation;
        obj.visible = visible;
        obj.page = page;
        return obj;
    }

    // 线段
    static EpaperLabel Line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t w,
                            uint16_t color = GxEPD_BLACK, uint8_t rotation = 1,
                            bool visible = true, uint16_t page = 1) {
        EpaperLabel obj;
        obj.type = EpaperObjectType::LINE;
        obj.x = x0; obj.y = y0;
        obj.x1 = x1; obj.y1 = y1;
        obj.width = w;
        obj.color = color;
        obj.rotation = rotation;
        obj.visible = visible;
        obj.page = page;
        return obj;
    }

    // 位图（支持镜像）
    static EpaperLabel Bitmap(int16_t x, int16_t y, const uint8_t* bitmap,
                              uint16_t w, uint16_t h, 
                              uint16_t depth = 1, 
                              uint8_t rotation = 1,
                              bool mirror_h = false,
                              bool mirror_v = false,
                              bool invert = false,
                              bool visible = true,
                              uint16_t page = 1) {
        EpaperLabel obj;
        obj.type = EpaperObjectType::BITMAP;
        obj.x = x; obj.y = y;
        obj.w = w; obj.h = h;
        obj.bitmap = bitmap;
        obj.depth = depth;
        obj.rotation = rotation;
        obj.mirror_h = mirror_h;
        obj.mirror_v = mirror_v;
        obj.invert = invert;
        obj.visible = visible;
        obj.page = page;
        return obj;
    }

    // 圆形
    static EpaperLabel Circle(int16_t x, int16_t y, uint16_t radius,
                              bool filled = false, uint16_t color = GxEPD_BLACK, uint8_t rotation = 1,
                              bool visible = true, uint16_t page = 1) {
        EpaperLabel obj;
        obj.type = EpaperObjectType::CIRCLE;
        obj.x = x; obj.y = y;
        obj.radius = radius;
        obj.filled = filled;
        obj.color = color;
        obj.rotation = rotation;
        obj.visible = visible;
        obj.page = page;
        return obj;
    }

    // 三角形
    static EpaperLabel Triangle(int16_t x0, int16_t y0, 
                                int16_t x1, int16_t y1, 
                                int16_t x2, int16_t y2,
                                bool filled = false, 
                                uint16_t color = GxEPD_BLACK, 
                                uint8_t rotation = 1,
                                bool visible = true,
                                uint16_t page = 1) {
        EpaperLabel obj;
        obj.type = EpaperObjectType::TRIANGLE;
        obj.x = x0; obj.y = y0;
        obj.x1 = x1; obj.y1 = y1;
        obj.x2 = x2; obj.y2 = y2;
        obj.filled = filled;
        obj.color = color;
        obj.rotation = rotation;
        obj.visible = visible;
        obj.page = page;
        return obj;
    }

    // 圆角矩形
    static EpaperLabel RoundRect(int16_t x, int16_t y, 
                                 uint16_t w, uint16_t h, 
                                 uint16_t radius,
                                 bool filled = false, 
                                 uint16_t color = GxEPD_BLACK, 
                                 uint8_t rotation = 1,
                                 bool visible = true,
                                 uint16_t page = 1) {
        EpaperLabel obj;
        obj.type = EpaperObjectType::ROUND_RECT;
        obj.x = x; obj.y = y;
        obj.w = w; obj.h = h;
        obj.radius = radius;
        obj.filled = filled;
        obj.color = color;
        obj.rotation = rotation;
        obj.visible = visible;
        obj.page = page;
        return obj;
    }

    // 像素点
    static EpaperLabel Pixel(int16_t x, int16_t y, 
                            uint16_t color = GxEPD_BLACK, 
                            uint8_t rotation = 1,
                            bool visible = true,
                            uint16_t page = 1) {
        EpaperLabel obj;
        obj.type = EpaperObjectType::PIXEL;
        obj.x = x; obj.y = y;
        obj.color = color;
        obj.rotation = rotation;
        obj.visible = visible;
        obj.page = page;
        return obj;
    }

private:
    EpaperLabel() = default; // 限制只能通过工厂函数创建
};



