#include <esp_log.h>
#include <esp_err.h>
#include <string>
#include <cstdlib>
#include <cstring>

#include "board.h"
#include "application.h"
#include "settings.h"
#include "epaper_display.h"

#define TAG "EpaperDisplay"

EpaperDisplay::EpaperDisplay(gpio_num_t cs, gpio_num_t dc, gpio_num_t rst, gpio_num_t busy) :
    display_epaper(GxEPD2_290_T5D(cs, dc, rst, busy)){
    // 创建互斥锁
    mutex_ = xSemaphoreCreateMutex();
    if (mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create mutex!");
    }
    // Notification timer
    esp_timer_create_args_t notification_timer_args = {
        .callback = [](void *arg) {
            EpaperDisplay *display = static_cast<EpaperDisplay*>(arg);
            DisplayLockGuard lock(display);
            // TODO: clear notification or trigger refresh            
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "notification_timer",
        .skip_unhandled_events = false,
    };
    ESP_ERROR_CHECK(esp_timer_create(&notification_timer_args, &notification_timer_));

    // Create a power management lock
    auto ret = esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "display_update", &pm_lock_);
    if (ret == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGI(TAG, "Power management not supported");
    } else {
        ESP_ERROR_CHECK(ret);
    }

    // 做墨水屏初始化显示
    display_epaper.init();
    display_epaper.setFullWindow();
    display_epaper.firstPage();
    do
    {
        display_epaper.fillScreen(GxEPD_WHITE);
    } while (display_epaper.nextPage()); 
    // 初始化 UI（不立即刷新，避免重复刷新卡顿）
    SetupUI();   
}

EpaperDisplay::~EpaperDisplay() {
    if (notification_timer_ != nullptr) {
        esp_timer_stop(notification_timer_);
        esp_timer_delete(notification_timer_);
    }
    if (pm_lock_ != nullptr) {
        esp_pm_lock_delete(pm_lock_);
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
    // 清理 UI 元素
    for (auto& pair : ui_labels_) {
        delete pair.second;
    }
    ui_labels_.clear();
}

void EpaperDisplay::SetStatus(const char* status) {

}

void EpaperDisplay::SetEmotion(const char* emotion) {  

 
}  
  
void EpaperDisplay::SetChatMessage(const char* role, const char* content) {  
    DisplayLockGuard lock(this);
    display_epaper.setRotation(1);
    display_epaper.setFont(&FreeMonoBold9pt7b);
    if (display_epaper.epd2.WIDTH < 104)
        display_epaper.setFont(0);
    display_epaper.setTextColor(GxEPD_BLACK);
    display_epaper.setPartialWindow(0, 0, 296, 128);  // 只更新小区域
    display_epaper.firstPage();
    do
    {
        display_epaper.fillScreen(GxEPD_WHITE);
        display_epaper.setCursor(10, 10);
        display_epaper.print(content);
        // display_epaper.drawRect(0, 0, 296, 64, GxEPD_BLACK);
        display_epaper.drawRect(296 - 50, 128 - 50, 50, 50, GxEPD_BLACK);

    } while (display_epaper.nextPage());
}  

void EpaperDisplay::ShowNotification(const std::string &notification, int duration_ms) {

}

void EpaperDisplay::ShowNotification(const char* notification, int duration_ms) {

}

void EpaperDisplay::UpdateStatusBar(bool update_all) {
    auto& app = Application::GetInstance();
    // Update time
    if (app.GetDeviceState() == kDeviceStateIdle) {
        if (last_status_update_time_ + std::chrono::seconds(10) < std::chrono::system_clock::now()) {
            // Set status to clock "HH:MM"
            time_t now = time(NULL);
            struct tm* tm = localtime(&now);
            // Check if the we have already set the time
            if (tm->tm_year >= 2025 - 1900) {
                char time_str[16];
                strftime(time_str, sizeof(time_str), "%H:%M  ", tm);
                SetStatus(time_str);
            } else {
                ESP_LOGW(TAG, "System time is not set, tm_year: %d", tm->tm_year);
            }
        }
    }

}

void EpaperDisplay::SetTheme(Theme* theme) {  

}  

void EpaperDisplay::SetupUI() {
    DisplayLockGuard lock(this);
    
    // 示例：使用工厂函数初始化 UI 元素
    
    // 标题文本
    ui_labels_["title"] = new EpaperLabel(EpaperLabel::Text(
        "Xiaozhi AI", 10, 20, &FreeMonoBold9pt7b, GxEPD_BLACK, EpaperTextAlign::LEFT, 1
    ));
    
    // // 分隔线
    // ui_labels_["divider"] = new EpaperLabel(EpaperLabel::Line(
    //     0, 60, 296, 60, GxEPD_BLACK, 1
    // ));
    
    // 消息区域边框
    // ui_labels_["msg_border"] = new EpaperLabel(EpaperLabel::RoundRect(
    //     0, 70, 296, 50, 4, false, GxEPD_BLACK, 1
    // ));
    
    // 圆形示例（可选）
    // ui_labels_["circle"] =
    //     new EpaperLabel(EpaperLabel::Circle(270, 40, 20, false, GxEPD_BLACK, 1));

    // 图像示例
    ui_labels_["image"] = new EpaperLabel(EpaperLabel::Bitmap(
        0, 0, (const uint8_t*)Image1_leju, 128, 296, 1, 2,false,true,true
    ));
    
    ui_dirty_ = true;
}

void EpaperDisplay::SetPowerSaveMode(bool on) {

}

bool EpaperDisplay::Lock(int timeout_ms) {
    if (mutex_ == nullptr) return false;

    TickType_t ticks = (timeout_ms <= 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    if (xSemaphoreTake(mutex_, ticks) == pdTRUE) {
        return true;
    } else {
        ESP_LOGW(TAG, "Lock timeout after %d ms", timeout_ms);
        return false;
    }
}

void EpaperDisplay::Unlock() {
    if (mutex_ != nullptr) {
        xSemaphoreGive(mutex_);
    }
}

// ===============================
//   UI 管理方法实现
// ===============================

EpaperLabel* EpaperDisplay::GetLabel(const String& id) {
    auto it = ui_labels_.find(id);
    if (it != ui_labels_.end()) {
        return it->second;
    }
    ESP_LOGW(TAG, "Label '%s' not found", id.c_str());
    return nullptr;
}

void EpaperDisplay::AddLabel(const String& id, EpaperLabel* label) {
    if (ui_labels_.find(id) != ui_labels_.end()) {
        ESP_LOGW(TAG, "Label '%s' already exists, replacing", id.c_str());
        delete ui_labels_[id];
    }
    ui_labels_[id] = label;
    ui_dirty_ = true;
}

void EpaperDisplay::RemoveLabel(const String& id) {
    auto it = ui_labels_.find(id);
    if (it != ui_labels_.end()) {
        delete it->second;
        ui_labels_.erase(it);
        ui_dirty_ = true;
    }
}

void EpaperDisplay::RenderLabel(EpaperLabel* label) {
    if (label == nullptr) return;
    
    // 设置旋转方向
    display_epaper.setRotation(label->rotation);
    
    switch (label->type) {
    case EpaperObjectType::TEXT: {
            if (label->font != nullptr) {
                display_epaper.setFont(label->font);
            }
            display_epaper.setTextColor(label->color);
            
            // 处理文本对齐
            int16_t cursor_x = label->x;
            if (label->align == EpaperTextAlign::CENTER) {
                int16_t x1, y1;
                uint16_t text_w, text_h;
                display_epaper.getTextBounds(label->text, 0, 0, &x1, &y1, &text_w, &text_h);
                cursor_x = label->x - text_w / 2;
            } else if (label->align == EpaperTextAlign::RIGHT) {
                int16_t x1, y1;
                uint16_t text_w, text_h;
                display_epaper.getTextBounds(label->text, 0, 0, &x1, &y1, &text_w, &text_h);
                cursor_x = label->x - text_w;
            }
            
            display_epaper.setCursor(cursor_x, label->y);
            display_epaper.print(label->text);
            break;
        }
            
        case EpaperObjectType::RECT: {
            if (label->filled) {
                display_epaper.fillRect(label->x, label->y, label->w, label->h, label->color);
            } else {
                display_epaper.drawRect(label->x, label->y, label->w, label->h, label->color);
            }
            break;
        }
            
        case EpaperObjectType::LINE: {
            display_epaper.drawLine(label->x, label->y, label->x1, label->y1, label->color);
            break;
        }
            
        case EpaperObjectType::CIRCLE: {
            if (label->filled) {
                display_epaper.fillCircle(label->x, label->y, label->radius, label->color);
            } else {
                display_epaper.drawCircle(label->x, label->y, label->radius, label->color);
            }
            break;
        }

        case EpaperObjectType::TRIANGLE: {
            if (label->filled) {
                display_epaper.fillTriangle(label->x, label->y, 
                                          label->x1, label->y1,
                                          label->x2, label->y2, 
                                          label->color);
            } else {
                display_epaper.drawTriangle(label->x, label->y, 
                                          label->x1, label->y1,
                                          label->x2, label->y2, 
                                          label->color);
            }
            break;
        }

        case EpaperObjectType::ROUND_RECT: {
            if (label->filled) {
                display_epaper.fillRoundRect(label->x, label->y, 
                                           label->w, label->h, 
                                           label->radius, label->color);
            } else {
                display_epaper.drawRoundRect(label->x, label->y, 
                                           label->w, label->h, 
                                           label->radius, label->color);
            }
            break;
        }

        case EpaperObjectType::PIXEL: {
            display_epaper.drawPixel(label->x, label->y, label->color);
            break;
        }
            
        case EpaperObjectType::BITMAP: {
            if (label->bitmap != nullptr) {
                // 处理镜像操作
                int16_t draw_x = label->x;
                int16_t draw_y = label->y;
                const uint8_t* bitmap_src = label->bitmap;
                uint8_t* temp_bitmap = nullptr;                
                
                // 如果需要镜像，记录警告（需要实现位图变换）
                if (label->mirror_h || label->mirror_v) {
                    temp_bitmap = mirrorBitmap(label->bitmap, label->w, label->h,
                                               label->mirror_h, label->mirror_v);
                    bitmap_src = temp_bitmap;
                }

                
                // 根据 depth 选择不同的绘制方法
                if (label->depth == 1) {
                    // 黑白位图绘制
                    if (label->invert) {
                        // 反色绘制：用背景色填充整个区域，然后反转前景色
                        uint16_t fg = (label->color == GxEPD_BLACK) ? GxEPD_WHITE : GxEPD_BLACK;
                        uint16_t bg = (label->color == GxEPD_BLACK) ? GxEPD_BLACK : GxEPD_WHITE;

                        display_epaper.fillRect(draw_x, draw_y, label->w, label->h, bg);
                        display_epaper.drawBitmap(draw_x, draw_y, bitmap_src,
                                                label->w, label->h, fg, bg);
                    } else {
                        // 正常绘制：仅使用前景色
                        display_epaper.drawBitmap(draw_x, draw_y, bitmap_src,
                                                label->w, label->h, label->color, GxEPD_WHITE);
                    }
                }
                // 多色屏支持（需要使用 GxEPD2_3C 或 GxEPD2_7C）
                // else if (label->depth == 3) {
                //     // 三色屏: 黑/白/红(或黄)
                //     // display_epaper.drawImage(draw_x, draw_y,
                //     label->bitmap, label->w, label->h);
                // }
                if (temp_bitmap) free(temp_bitmap);
            }
            break;
        }
    }
}

void EpaperDisplay::UpdateLabel(const String& id) {
    DisplayLockGuard lock(this);
    
    auto it = ui_labels_.find(id);
    if (it == ui_labels_.end()) {
        ESP_LOGW(TAG, "Label '%s' not found for update", id.c_str());
        return;
    }
    
    // 局部刷新：根据 label 区域设置局部窗口
    EpaperLabel* label = it->second;
    
    // 计算刷新区域（可以根据实际需要调整）
    int16_t refresh_x = label->x - 5;
    int16_t refresh_y = label->y - 5;
    uint16_t refresh_w = label->w + 10;
    uint16_t refresh_h = label->h + 10;
    
    // 边界检查
    if (refresh_x < 0) refresh_x = 0;
    if (refresh_y < 0) refresh_y = 0;
    
    display_epaper.setPartialWindow(refresh_x, refresh_y, refresh_w, refresh_h);
    display_epaper.firstPage();
    do {
        display_epaper.fillScreen(GxEPD_WHITE);  // 清空局部区域
        RenderLabel(label);
    } while (display_epaper.nextPage());
}

void EpaperDisplay::UpdateUI(bool fullRefresh) {
    DisplayLockGuard lock(this);
    
    if (fullRefresh) {
        // 全屏刷新
        display_epaper.setFullWindow();
    } else {
        // 局部刷新（默认刷新整个屏幕）
        display_epaper.setPartialWindow(0, 0, display_epaper.width(), display_epaper.height());
    }
    
    display_epaper.firstPage();
    do {
        display_epaper.fillScreen(GxEPD_WHITE);
        
        // 渲染所有 label
        for (auto& pair : ui_labels_) {
            RenderLabel(pair.second);
        }
    } while (display_epaper.nextPage());
    
    ui_dirty_ = false;
}

// 反转一个字节的 bit 顺序，比如 0b01100010 -> 0b01000110
uint8_t EpaperDisplay::reverseByte(uint8_t b) {
    b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
    b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
    b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
    return b;
}

// 对 PROGMEM 中的 bitmap 进行镜像复制到新的缓冲区
uint8_t* EpaperDisplay::mirrorBitmap(const uint8_t* src, int16_t w, int16_t h,
                      bool mirror_h, bool mirror_v) {
    int row_bytes = (w + 7) / 8;
    int total_bytes = row_bytes * h;
    uint8_t* dst = (uint8_t*)malloc(total_bytes);
    if (!dst) return nullptr;

    for (int y = 0; y < h; y++) {
        int src_row = mirror_v ? (h - 1 - y) : y;
        for (int bx = 0; bx < row_bytes; bx++) {
            int src_bx = mirror_h ? (row_bytes - 1 - bx) : bx;
            uint8_t b = pgm_read_byte(&src[src_row * row_bytes + src_bx]);
            if (mirror_h) b = reverseByte(b);
            dst[y * row_bytes + bx] = b;
        }
    }
    return dst;
}

