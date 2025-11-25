#ifndef EPAPER_DISPLAY_H
#define EPAPER_DISPLAY_H

#include "display.h"

#include <esp_timer.h>
#include <esp_log.h>
#include <esp_pm.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string>
#include <chrono>

//##################   墨水屏头文件 start
#include <stdio.h>
#include "driver/gpio.h"

#define ENABLE_GxEPD2_GFX 1
#if ARDUINO >= 100
#include <Arduino.h>
#else
#include <WProgram.h>
#endif
#include <pins_arduino.h>
#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <GxEPD2_7C.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold18pt7b.h>
#include <U8g2_for_Adafruit_GFX.h>
// ESP32S3主控接法： 
//    SS(CS)=10 MOSI(SDA)=11  MISO(不接)=13 SCK=12 DC=8 RST=7 BUSY=9
// 2.9寸屏
// GxEPD2_BW<GxEPD2_290_T5D, GxEPD2_290_T5D::HEIGHT> display_epaper(GxEPD2_290_T5D(/*CS=5*/ 10, /*DC=*/ 8, /*RST=*/ 7, /*BUSY=*/ 9));
// select the display constructor line in one of the following files (old style):
#include "GxEPD2_display_selection.h"
#include "GxEPD2_display_selection_added.h"
// #include "GxEPD2_display_selection_more.h" // private

// or select the display class and display driver class in the following file (new style):
#include "GxEPD2_display_selection_new_style.h"

#include "Bitmaps_128x296.h" // 2.9"  b/w
//##################   墨水屏头文件 end

//##################   Epaperdisplay类的实现 start
#include "display/epaperdisplay/epaper_display.h"
#include "display/epaperdisplay/epaperui.h"
#include "display/epaperdisplay/epaper_image.h"
#include <map>

//##################   Epaperdisplay类的实现 end

class EpaperDisplay : public Display {
public:
    EpaperDisplay(gpio_num_t cs, gpio_num_t dc, gpio_num_t rst, gpio_num_t busy);
    virtual ~EpaperDisplay();

    virtual void SetStatus(const char* status);
    virtual void ShowNotification(const char* notification, int duration_ms = 3000);
    virtual void ShowNotification(const std::string &notification, int duration_ms = 3000);
    virtual void UpdateStatusBar(bool update_all = false);
    virtual void SetPowerSaveMode(bool on);
    virtual void SetEmotion(const char *emotion);
    virtual void SetChatMessage(const char *role, const char *content);
    virtual void SetTheme(Theme *theme);
    virtual void SetupUI();

    // UI 管理方法
    EpaperLabel* GetLabel(const String& id);           // 获取 label 指针，用于修改属性
    void UpdateLabel(const String& id);                 // 刷新单个 label
    void UpdateUI(bool fullRefresh = false);            // 刷新所有 label
    void SetPage(uint16_t page);                         // 切换页面并全局刷新
    void AddLabel(const String& id, EpaperLabel* label); // 动态添加 label
    void RemoveLabel(const String& id);                 // 移除 label
    
    // 显示/隐藏控制方法
    void LabelShow(const String& id);                      // 显示指定 label
    void LabelHide(const String& id);                      // 隐藏指定 label

protected:
    esp_pm_lock_handle_t pm_lock_ = nullptr;
    bool muted_ = false;
    SemaphoreHandle_t mutex_ = nullptr; 
    // 2.9寸屏
    GxEPD2_BW<GxEPD2_290_T5D, GxEPD2_290_T5D::HEIGHT> display_epaper;
    U8G2_FOR_ADAFRUIT_GFX u8g2_for_gfx;  // U8g2 字体渲染器

    std::chrono::system_clock::time_point last_status_update_time_;
    esp_timer_handle_t notification_timer_ = nullptr;

    // UI 管理
    std::map<String, EpaperLabel*> ui_labels_;  // 存储所有 UI 元素
    bool ui_dirty_ = false;                      // 标记是否需要刷新
    uint16_t current_page_ = 2;                  // 当前页面

    // 内部渲染方法
    void RenderLabel(EpaperLabel *label); // 渲染单个 label
    void RenderTextWithWrap(EpaperLabel* label); // 渲染换行文本
    
    // 文本边界计算
    struct TextBounds {
        int16_t x, y;
        uint16_t w, h;
    };
    TextBounds CalculateTextBounds(EpaperLabel* label); // 计算文本边界
    
    uint8_t reverseByte(uint8_t b); // 反转一个字节的 bit 顺序
    uint8_t* mirrorBitmap(const uint8_t* src, int16_t w, int16_t h,
                                  bool mirror_h, bool mirror_v);

    friend class DisplayLockGuard;
    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;
};

#endif
