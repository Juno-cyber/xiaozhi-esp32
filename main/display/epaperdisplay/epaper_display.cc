#include <esp_log.h>
#include <esp_err.h>
#include <string>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <algorithm>

#include "board.h"
#include "application.h"
#include "settings.h"
#include "epaper_display.h"
#include "epaper_font.h"

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

            // 通知时间到，隐藏通知
            ESP_LOGI(TAG, "Notification time out, hiding notification");
            display->LabelHide("notification_label");
            
            // 根据设备状态决定显示时间还是状态
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateIdle) {
                // 空闲状态显示时间
                ESP_LOGI(TAG, "Showing time label");
                display->LabelShow("time_label");
            } else {
                // 非空闲状态显示状态
                ESP_LOGI(TAG, "Showing status label");
                display->LabelShow("status_label");
            }
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
    
    // 初始化 U8g2 字体渲染器
    u8g2_for_gfx.begin(display_epaper);
    
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
    DisplayLockGuard lock(this);
    
    auto* status_label = GetLabel("status_label");
    if (status_label == nullptr) {
        ESP_LOGW(TAG, "status_label not found");
        return;
    }
    
    // 设置状态文本
    status_label->text = String(status);

    // 隐藏通知和时间，显示状态
    ESP_LOGD(TAG, "Hiding notification/time and showing status");
    auto* notification_label = GetLabel("notification_label");
    if (notification_label && notification_label->visible) {
        LabelHide("notification_label");
    }
    auto* time_label = GetLabel("time_label");
    if (time_label && time_label->visible) {
        LabelHide("time_label");
    }
    LabelShow("status_label");
    
    last_status_update_time_ = std::chrono::system_clock::now();
    ESP_LOGD(TAG, "SetStatus: %s", status);
}

void EpaperDisplay::SetEmotion(const char* emotion) {
    DisplayLockGuard lock(this);
    
    auto* emoji_image = GetLabel("emoji_image");
    if (emoji_image == nullptr) {
        ESP_LOGW(TAG, "emoji_image not found");
        return;
    }
    
    // 从输入的 emotion 字符串映射到位图图标
    const unsigned char* emotion_bitmap = nullptr;
    
    // 进行字符串比较，映射到对应的表情位图
    if (strcmp(emotion, "neutral") == 0) {
        emotion_bitmap = EpaperImage::EMO_NEUTRAL_32x32;
    } else if (strcmp(emotion, "happy") == 0) {
        emotion_bitmap = EpaperImage::EMO_HAPPY_32x32;
    } else if (strcmp(emotion, "laughing") == 0) {
        emotion_bitmap = EpaperImage::EMO_LAUGHING_32x32;
    } else if (strcmp(emotion, "funny") == 0) {
        emotion_bitmap = EpaperImage::EMO_FUNNY_32x32;
    } else if (strcmp(emotion, "sad") == 0) {
        emotion_bitmap = EpaperImage::EMO_SAD_32x32;
    } else if (strcmp(emotion, "angry") == 0) {
        emotion_bitmap = EpaperImage::EMO_ANGRY_32x32;
    } else if (strcmp(emotion, "crying") == 0) {
        emotion_bitmap = EpaperImage::EMO_CRYING_32x32;
    } else if (strcmp(emotion, "loving") == 0) {
        emotion_bitmap = EpaperImage::EMO_LOVING_32x32;
    } else if (strcmp(emotion, "embarrassed") == 0) {
        emotion_bitmap = EpaperImage::EMO_EMBARRASSED_32x32;
    } else if (strcmp(emotion, "surprised") == 0) {
        emotion_bitmap = EpaperImage::EMO_SURPRISED_32x32;
    } else if (strcmp(emotion, "shocked") == 0) {
        emotion_bitmap = EpaperImage::EMO_SHOCKED_32x32;
    } else if (strcmp(emotion, "thinking") == 0) {
        emotion_bitmap = EpaperImage::EMO_THINKING_32x32;
    } else if (strcmp(emotion, "winking") == 0) {
        emotion_bitmap = EpaperImage::EMO_WINKING_32x32;
    } else if (strcmp(emotion, "cool") == 0) {
        emotion_bitmap = EpaperImage::EMO_COOL_32x32;
    } else if (strcmp(emotion, "relaxed") == 0) {
        emotion_bitmap = EpaperImage::EMO_RELAXED_32x32;
    } else if (strcmp(emotion, "delicious") == 0) {
        emotion_bitmap = EpaperImage::EMO_DELICIOUS_32x32;
    } else if (strcmp(emotion, "kissy") == 0) {
        emotion_bitmap = EpaperImage::EMO_KISSY_32x32;
    } else if (strcmp(emotion, "confident") == 0) {
        emotion_bitmap = EpaperImage::EMO_CONFIDENT_32x32;
    } else if (strcmp(emotion, "sleepy") == 0) {
        emotion_bitmap = EpaperImage::EMO_SLEEPY_32x32;
    } else if (strcmp(emotion, "silly") == 0) {
        emotion_bitmap = EpaperImage::EMO_SILLY_32x32;
    } else if (strcmp(emotion, "confused") == 0) {
        emotion_bitmap = EpaperImage::EMO_CONFUSED_32x32;
    } else {
        // 默认使用 neutral
        emotion_bitmap = EpaperImage::EMO_NEUTRAL_32x32;
        ESP_LOGD(TAG, "Unknown emotion '%s', using neutral", emotion);
    }
    
    // 更新表情标签的位图
    emoji_image->bitmap = emotion_bitmap;
    emoji_image->visible = true;
    
    // 使用 UpdateLabel 统一刷新
    UpdateLabel("emoji_image");
    
    ESP_LOGD(TAG, "SetEmotion: %s", emotion);
}  
  
void EpaperDisplay::SetChatMessage(const char* role, const char* content) {
    DisplayLockGuard lock(this);
    
    auto* chat_message_label = GetLabel("chat_message_label");
    if (chat_message_label == nullptr) {
        ESP_LOGW(TAG, "chat_message_label not found");
        return;
    }
    
    // 如果内容为空，隐藏聊天消息
    if (content == nullptr || content[0] == '\0') {
        if (chat_message_label->visible) {
            chat_message_label->visible = false;
            chat_message_label->text = "";
            // 使用 UpdateLabel 清除
            UpdateLabel("chat_message_label");
        }
        return;
    }
    
    // 设置聊天消息文本并确保可见
    chat_message_label->text = String(content);
    chat_message_label->visible = true;
    
    // 使用 UpdateLabel 统一刷新
    UpdateLabel("chat_message_label");
    
    ESP_LOGD(TAG, "SetChatMessage [%s]: %s", role, content);
}  

void EpaperDisplay::ShowNotification(const std::string &notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}

void EpaperDisplay::ShowNotification(const char* notification, int duration_ms) {
    DisplayLockGuard lock(this);
    
    auto* notification_label = GetLabel("notification_label");
    if (notification_label == nullptr) {
        ESP_LOGW(TAG, "notification_label not found");
        return;
    }
    
    // 设置通知文本
    notification_label->text = String(notification);

    // 隐藏状态和时间，显示通知
    ESP_LOGD(TAG, "Showing notification and hiding status/time");
    auto* status_label = GetLabel("status_label");
    if (status_label && status_label->visible) {
        LabelHide("status_label");
    }
    auto* time_label = GetLabel("time_label");
    if (time_label && time_label->visible) {
        LabelHide("time_label");
    }
    LabelShow("notification_label");
    
    // 停止之前的定时器
    if (notification_timer_ != nullptr) {
        esp_timer_stop(notification_timer_);
    }
    
    // 启动定时器，在指定时间后恢复显示状态
    if (notification_timer_ != nullptr && duration_ms > 0) {
        ESP_ERROR_CHECK(esp_timer_start_once(notification_timer_, duration_ms * 1000));
    }
}

void EpaperDisplay::UpdateStatusBar(bool update_all) {
    auto& app = Application::GetInstance();
    auto& board = Board::GetInstance();
    
    // 每 10 秒更新一次网络图标
    static int seconds_counter = 0;
    if (update_all || seconds_counter++ % 10 == 0) {
        // 升级固件时，不读取 4G 网络状态，避免占用 UART 资源
        auto device_state = app.GetDeviceState();
        static const std::vector<DeviceState> allowed_states = {
            kDeviceStateIdle,
            kDeviceStateStarting,
            kDeviceStateWifiConfiguring,
            kDeviceStateListening,
            kDeviceStateActivating,
        };
        if (std::find(allowed_states.begin(), allowed_states.end(), device_state) != allowed_states.end()) {
            const char* icon = board.GetNetworkStateIcon();
            if (icon != nullptr) {
                // 根据 Font Awesome 图标映射到 Siji 字体的 Unicode 编码                
                // 保存上次的icon用于对比
                static const char* last_icon = nullptr;                
                // 只在icon变化时才更新
                if (last_icon != icon) {
                    const char* siji_icon = nullptr;
                    // 通过判断 UTF-8 字节来区分WiFi状态
                    // Font Awesome WiFi Slash 的 UTF-8 编码是: \xef\x87\xab
                    // 其他WiFi图标统一映射为 siji wifi
                    if (strstr(icon, "\xef\x9a\xac") != nullptr) {  
                        // WiFi 未连接  
                        siji_icon = EpaperFont::Siji::WIFI_DISCONNECTED;
                    } else if (strstr(icon, "\xef\x9a\xaa") != nullptr) {
                    //   弱信号
                        siji_icon = EpaperFont::Siji::WIFI_WEAK;
                    } else if (strstr(icon, "\xef\x9a\xab") != nullptr) {
                    //   中信号
                        siji_icon = EpaperFont::Siji::WIFI_MEDIUM; 
                    } else {  
                        // WiFi 强信号或配置模式  
                        siji_icon = EpaperFont::Siji::WIFI_STRONG;
                    }  
                    
                    DisplayLockGuard lock(this);
                    auto* network_label = GetLabel("network_label");
                    if (network_label) {
                        network_label->text = String(siji_icon);
                        UpdateLabel("network_label");
                        ESP_LOGD(TAG, "Network icon updated");
                    }
                    
                    last_icon = icon;
                }
            }
        }
    }
    
    // 只在空闲状态下更新时间显示
    if (app.GetDeviceState() == kDeviceStateIdle) {
        // 每 30 秒更新一次时间
        if (update_all || last_status_update_time_ + std::chrono::seconds(30) < std::chrono::system_clock::now()) {
            time_t now = time(NULL);
            struct tm* tm = localtime(&now);
            
            // 检查系统时间是否已设置
            if (tm->tm_year >= 2025 - 1900) {
                char time_str[16];
                strftime(time_str, sizeof(time_str), "%H:%M", tm);
                
                ESP_LOGD(TAG, "Updating time to: %s", time_str);
                
                DisplayLockGuard lock(this);
                
                // 更新 time_label 的文本
                auto* time_label = GetLabel("time_label");
                if (time_label) {
                    time_label->text = String(time_str);                    
                    // 隐藏通知和状态，显示时间
                    auto* notification_label = GetLabel("notification_label");
                    if (notification_label && notification_label->visible) {
                        LabelHide("notification_label");
                    }
                    auto* status_label = GetLabel("status_label");
                    if (status_label && status_label->visible) {
                        LabelHide("status_label");
                    }                    
                    // 显示时间标签
                    LabelShow("time_label");
                }
                
                last_status_update_time_ = std::chrono::system_clock::now();
            } else {
                ESP_LOGW(TAG, "System time is not set, tm_year: %d", tm->tm_year);
            }
        }
    } else {
        ESP_LOGD(TAG, "Skip time update, state: %d", app.GetDeviceState());
    }
}

void EpaperDisplay::SetTheme(Theme* theme) {  

}  

void EpaperDisplay::SetupUI() {
    DisplayLockGuard lock(this);
    
    // ================================
    // 屏幕尺寸：128x296 (旋转后可能为296x128，根据rotation设置)
    // 布局参考LCD的垂直结构，适配墨水屏尺寸
    // ================================

    // ==========================================================page 1 start==========================================================
    // ===== 1. 状态栏 (status_bar) =====
    // 位置：屏幕顶部，高度20像素
    // 水平布局：网络 | 通知(隐藏) | 状态/时间 | 静音 | 电池
    
    // 1.1 网络图标 (network_label)
    AddLabel("network_label", new EpaperLabel(
        EpaperLabel::Bitmap(240, 0, EpaperImage::wifi_full_24x24, 24, 24, 1, 1, false, false, false, true, 1)));
    
    // 1.2 通知文本 (notification_label) - 默认隐藏
    AddLabel("notification_label", new EpaperLabel(
        EpaperLabel::Text("", 88, 15, 120,0, u8g2_font_wqy12_t_gb2312, 
                         GxEPD_BLACK, EpaperTextAlign::CENTER, 1, false, 1)));
    
    // 1.3 状态标签 (status_label) - 居中显示
    AddLabel("status_label",
             new EpaperLabel(EpaperLabel::Text(
                 "waiting", 98, 15, 100,0, u8g2_font_wqy12_t_gb2312, GxEPD_BLACK,
                 EpaperTextAlign::CENTER, 1, true, 1)));
    // 1.4 时间标签（time_label） - 居中显示
    AddLabel("time_label",
             new EpaperLabel(EpaperLabel::Text(
                 "05:20", 98, 25, 100,0, u8g2_font_freedoomr25_mn, GxEPD_BLACK,
                 EpaperTextAlign::CENTER, 1, true, 1)));
    
    // 1.5 静音图标 (mute_label)
    AddLabel("mute_label", new EpaperLabel(
        EpaperLabel::Text("", 260, 15, 0,0, u8g2_font_emoticons21_tr, 
                         GxEPD_BLACK, EpaperTextAlign::LEFT, 1, true, 1)));
    
    // 1.6 电池图标 (battery_label)
    AddLabel("battery_label", new EpaperLabel(
        EpaperLabel::Bitmap(270, 0, EpaperImage::battery_full_24x24, 24, 24, 1, 1, false, false, false, true, 1)));
    
    // 1.7 状态栏分隔线
    AddLabel("status_bar_divider", new EpaperLabel(
        EpaperLabel::Line(10, 40, 286, 40, GxEPD_BLACK, 1, true, 1)));
    
    // ===== 2. 内容区 (content) =====
    // 位置：状态栏下方，占据剩余空间 (y: 25 ~ 128)
    
    // 2.1 表情容器 (emoji_box)
    // 2.1.1 表情图标 (emoji_label) - 中央显示
    AddLabel("emoji_label", new EpaperLabel(
        EpaperLabel::Text(EpaperFont::Emoticons::NEUTRAL, 193, 60, 30,21, u8g2_font_emoticons21_tr, 
                         GxEPD_BLACK, EpaperTextAlign::CENTER, 1, false, 1)));
    
    // 2.1.2 表情图片 (emoji_image) - 默认隐藏
    // 注：墨水屏位图需要实际资源，这里先占位
    AddLabel("emoji_image", new EpaperLabel(
        EpaperLabel::Bitmap(132, 35, EpaperImage::EMO_NEUTRAL_32x32, 32, 32, 1, 1, false, false, false, true, 1)));
    
    // 2.2 预览图片 (preview_image) - 默认隐藏
    // AddLabel("preview_image", new EpaperLabel(
    //     EpaperLabel::Bitmap(10, 30, nullptr, 276, 70, 1, 1, false, false, false, false)));
    
    // 2.3 聊天消息 (chat_message_label)
    AddLabel("chat_message_label", new EpaperLabel(
        EpaperLabel::Text("", 28, 85, 240,0, u8g2_font_wqy16_t_gb2312, 
                         GxEPD_BLACK, EpaperTextAlign::CENTER, 1, true, 1)));
    
    // ===== 3. 低电量弹窗 (low_battery_popup) - 默认隐藏 =====
    // 位置：底部居中
    AddLabel("low_battery_popup_bg", new EpaperLabel(
        EpaperLabel::RoundRect(20, 100, 256, 20, 6, true, GxEPD_BLACK, 1, false, 1)));
    
    AddLabel("low_battery_label", new EpaperLabel(
        EpaperLabel::Text("电量低，请充电", 103, 113, 90,0, u8g2_font_wqy16_t_gb2312, 
                         GxEPD_WHITE, EpaperTextAlign::CENTER, 1, false, 1)));
// ==========================================================page 1 end==========================================================    
// ==========================================================page 2 start========================================================
    AddLabel("status_bar_divider_2", new EpaperLabel(
        EpaperLabel::Line(10, 40, 286, 40, GxEPD_BLACK, 1, true, 2)));

    
// ==========================================================page 2 end==========================================================
    ui_dirty_ = true;
}

void EpaperDisplay::SetPowerSaveMode(bool on) {

}

void EpaperDisplay::SetPage(uint16_t page) {
    if (current_page_ != page) {
        current_page_ = page;
        UpdateUI(true);
    }
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

void EpaperDisplay::LabelShow(const String& id) {
    // 注意：调用者必须已持有锁
    EpaperLabel* label = GetLabel(id);
    if (label != nullptr) {
        label->visible = true;
        ESP_LOGD(TAG, "Label '%s' shown", id.c_str());
        UpdateLabel(id);
    } else {
        ESP_LOGW(TAG, "Label '%s' not found for show", id.c_str());
    }
}

void EpaperDisplay::LabelHide(const String& id) {
    // 注意：调用者必须已持有锁
    EpaperLabel* label = GetLabel(id);
    if (label != nullptr) {
      if (label->visible) {
            label->visible = false;
            UpdateLabel(id);
            ESP_LOGD(TAG, "Label '%s' hidden", id.c_str());        
        }

    } else {
        ESP_LOGW(TAG, "Label '%s' not found for hide", id.c_str());
    }
}

void EpaperDisplay::RenderLabel(EpaperLabel* label) {
    if (label == nullptr) return;
    
    // 设置旋转方向
    display_epaper.setRotation(label->rotation);
    
    // 如果不可见，仅用白色填充区域来清除内容
    if (!label->visible||label->page != current_page_) {
        // 计算需要清除的区域
        int16_t clear_x = label->x;
        int16_t clear_y = label->y;
        uint16_t clear_w = 100;
        uint16_t clear_h = 30;
        
        // 对于文本类型，需要动态计算边界以确保清除完整
        if (label->type == EpaperObjectType::TEXT &&
            label->u8g2_font != nullptr) {
            // 保存旧的宽度和高度
            uint16_t old_h = label->h;
            auto bounds = CalculateTextBounds(label);
            uint16_t new_h = bounds.h;            
            clear_x = label->x;
            clear_y = label->y;
            clear_w = label->w_max;  // 取较大的宽度
            clear_h = (old_h > new_h) ? old_h : new_h; // 取较大的高度
            // 更新 label 的 h 为新计算的值
            label->h = new_h;            
        }
        
        // 用白色填充区域（清除）
        if (clear_w > 0 && clear_h > 0) {
            display_epaper.fillRect(clear_x, clear_y, clear_w, clear_h, GxEPD_WHITE);
        }
        return;
    }
    
    switch (label->type) {
    case EpaperObjectType::TEXT: {
            // 统一使用 U8g2 字体渲染（支持中英文）
            if (label->u8g2_font != nullptr) {
                u8g2_for_gfx.setFont(label->u8g2_font);
                u8g2_for_gfx.setForegroundColor(label->color);
                
                // 如果设置了最大宽度限制，执行换行逻辑
                if (label->w_max > 0) {
                    RenderTextWithWrap(label);
                } else {
                    // 单行文本，处理文本对齐
                    int16_t cursor_x = label->x;
                    if (label->align == EpaperTextAlign::CENTER) {
                        int16_t text_w = u8g2_for_gfx.getUTF8Width(label->text.c_str());
                        cursor_x = label->x - text_w / 2;
                    } else if (label->align == EpaperTextAlign::RIGHT) {
                        int16_t text_w = u8g2_for_gfx.getUTF8Width(label->text.c_str());
                        cursor_x = label->x - text_w;
                    }
                    
                    u8g2_for_gfx.setCursor(cursor_x, label->y);
                    u8g2_for_gfx.print(label->text);
                }
            }
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
  // 注意：调用者必须已持有锁
    auto it = ui_labels_.find(id);
    if (it == ui_labels_.end()) {
        ESP_LOGW(TAG, "Label '%s' not found for update", id.c_str());
        return;
    }
    
    // 局部刷新：根据 label 区域设置局部窗口
    EpaperLabel* label = it->second;
    
    // 计算刷新区域
    int16_t refresh_x = label->x;
    int16_t refresh_y = label->y;
    uint16_t refresh_w = 100;
    uint16_t refresh_h = 30;
    
    // 如果是文本类型，需要动态计算边界
    if (label->type == EpaperObjectType::TEXT) {
        display_epaper.setRotation(label->rotation);
        
        // 使用 U8g2 字体边界计算
        if (label->u8g2_font != nullptr) {
            u8g2_for_gfx.setFont(label->u8g2_font);
            
            // 保存旧的宽度和高度
            uint16_t old_h = label->h;
            
            // 动态计算新文本的边界
            auto bounds = CalculateTextBounds(label);
            uint16_t new_w = bounds.w;
            uint16_t new_h = bounds.h;
            
            // 宽度和高度都比较取最大值
            refresh_x = bounds.x;
            refresh_y = bounds.y;
            refresh_w = label->w_max;  // 取较大的宽度
            refresh_h = (old_h > new_h) ? old_h : new_h;  // 取较大的高度

            // 更新 label 的 h 为新计算的值
            label->h = new_h;
            
            ESP_LOGI(TAG, "Label '%s' old_h=%d, new: w=%d h=%d, refresh: w=%d h=%d", 
                     id.c_str(), old_h, new_w, new_h, refresh_w, refresh_h);
        } else {
            // 如果没有设置字体，使用默认尺寸
            refresh_x = label->x;
            refresh_y = label->y - 20;
            refresh_w = 100;
            refresh_h = 30;
        }
    } else {
        // 其他类型使用 label 自身的尺寸
        refresh_x = label->x;
        refresh_y = label->y;
        refresh_w = label->w;
        refresh_h = label->h;
    }
    
    // 边界检查
    if (refresh_x < 0) refresh_x = 0;
    if (refresh_y < 0) refresh_y = 0;
    if (refresh_w <= 0) refresh_w = 100;  // 最小宽度
    if (refresh_h <= 0) refresh_h = 30;   // 最小高度
    
    display_epaper.setPartialWindow(label->x, refresh_y, refresh_w, refresh_h);
    display_epaper.firstPage();
    do {
        display_epaper.fillScreen(GxEPD_WHITE);  // 清空局部区域
        RenderLabel(label);
    } while (display_epaper.nextPage());
}

void EpaperDisplay::UpdateUI(bool fullRefresh) {
    // 注意：调用者必须已持有锁
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

// 渲染带有换行的文本
void EpaperDisplay::RenderTextWithWrap(EpaperLabel* label) {
    if (label->u8g2_font == nullptr || label->w_max == 0) {
        return;
    }
    
    u8g2_for_gfx.setFont(label->u8g2_font);
    u8g2_for_gfx.setForegroundColor(label->color);
    
    String text = label->text;
    
    // 先检查整个文本是否超出宽度，如果没超出就不换行
    int16_t total_width = u8g2_for_gfx.getUTF8Width(text.c_str());
    if (total_width <= label->w_max) {
        // 文本没有超出宽度，单行显示
        int16_t cursor_x = label->x;
        if (label->align == EpaperTextAlign::CENTER) {
            cursor_x = label->x + (label->w_max - total_width) / 2;
        } else if (label->align == EpaperTextAlign::RIGHT) {
            cursor_x = label->x + label->w_max - total_width;
        }
        u8g2_for_gfx.setCursor(cursor_x, label->y);
        u8g2_for_gfx.print(text);
        return;
    }
    
    // 文本超出宽度，需要换行
    int16_t cursor_y = label->y;
    // getFontAscent() 就是行高，再加上2像素行间距
    int16_t line_height = u8g2_for_gfx.getFontAscent() + 6;
    
    // 按照宽度限制进行换行
    int text_len = text.length();
    int start_idx = 0;
    
    while (start_idx < text_len) {
        String line = "";
        int end_idx = start_idx;
        int16_t line_width = 0;
        
        // 逐个字符测试，直到超出宽度
        while (end_idx < text_len) {
            // 获取下一个 UTF-8 字符
            String next_char = "";
            uint8_t c = text[end_idx];
            
            if ((c & 0x80) == 0) {
                // ASCII 字符 (0xxxxxxx)
                next_char = String((char)c);
                end_idx++;
            } else if ((c & 0xE0) == 0xC0) {
                // 2字节 UTF-8 (110xxxxx 10xxxxxx)
                if (end_idx + 1 < text_len) {
                    next_char = text.substring(end_idx, end_idx + 2);
                    end_idx += 2;
                } else {
                    break;
                }
            } else if ((c & 0xF0) == 0xE0) {
                // 3字节 UTF-8 (1110xxxx 10xxxxxx 10xxxxxx)
                if (end_idx + 2 < text_len) {
                    next_char = text.substring(end_idx, end_idx + 3);
                    end_idx += 3;
                } else {
                    break;
                }
            } else if ((c & 0xF8) == 0xF0) {
                // 4字节 UTF-8 (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
                if (end_idx + 3 < text_len) {
                    next_char = text.substring(end_idx, end_idx + 4);
                    end_idx += 4;
                } else {
                    break;
                }
            } else {
                // 无效的 UTF-8，跳过
                end_idx++;
                continue;
            }
            
            // 测试添加该字符后的宽度
            String test_line = line + next_char;
            int16_t test_width = u8g2_for_gfx.getUTF8Width(test_line.c_str());
            
            if (test_width > label->w_max && line.length() > 0) {
                // 超出宽度，回退
                end_idx -= next_char.length();
                break;
            }
            
            line = test_line;
            line_width = test_width;
        }
        
        // 渲染这一行，应用对齐方式
        int16_t cursor_x = label->x;
        if (label->align == EpaperTextAlign::CENTER) {
            cursor_x = label->x + (label->w_max - line_width) / 2;
        } else if (label->align == EpaperTextAlign::RIGHT) {
            cursor_x = label->x + label->w_max - line_width;
        }
        
        u8g2_for_gfx.setCursor(cursor_x, cursor_y);
        u8g2_for_gfx.print(line);
        
        // 移动到下一行
        cursor_y += line_height;
        start_idx = end_idx;
    }
}

// 计算文本边界（支持换行）
EpaperDisplay::TextBounds EpaperDisplay::CalculateTextBounds(EpaperLabel* label) {
    TextBounds bounds = {0, 0, 0, 0};
    
    if (!label || !label->u8g2_font) return bounds;

    u8g2_for_gfx.setFont(label->u8g2_font);
    
    int16_t ascent  = u8g2_for_gfx.getFontAscent();
    int16_t descent = u8g2_for_gfx.getFontDescent();
    int16_t line_height = ascent - descent + 2; // 推荐换行高度（更稳定）

    // --- ①：如果 w_max==0 → 单行文本 ---
    if (label->w_max == 0) {
        int16_t text_w = u8g2_for_gfx.getUTF8Width(label->text.c_str());
        int16_t bounds_x = label->x;

        if (label->align == EpaperTextAlign::CENTER)
            bounds_x = label->x - text_w / 2;
        else if (label->align == EpaperTextAlign::RIGHT)
            bounds_x = label->x - text_w;

        bounds.x = bounds_x;
        bounds.y = label->y - ascent;
        bounds.w = text_w;
        bounds.h = line_height;
        return bounds;
    }

    // --- ②：有宽度限制，先测单行是否 fits ---
    int16_t total_width = u8g2_for_gfx.getUTF8Width(label->text.c_str());
    if (total_width <= label->w_max) {
        int16_t bounds_x = label->x;
        if (label->align == EpaperTextAlign::CENTER)
            bounds_x = label->x + (label->w_max - total_width) / 2;
        else if (label->align == EpaperTextAlign::RIGHT)
            bounds_x = label->x + label->w_max - total_width;

        bounds.x = bounds_x;
        bounds.y = label->y - ascent;
        bounds.w = total_width;
        bounds.h = line_height;
        return bounds;
    }

    // --- ③：多行计算（核心）---
    String text = label->text;
    int text_len = text.length();
    int start_idx = 0;

    int16_t max_width = 0;
    int line_count = 0;

    while (start_idx < text_len) {
        String line = "";
        int end_idx = start_idx;

        // UTF8 按字符扩展
        while (end_idx < text_len) {
            String next_char = "";
            uint8_t c = text[end_idx];

            if ((c & 0x80) == 0) {
                next_char = String((char)c);
                end_idx++;
            } else if ((c & 0xE0) == 0xC0) {
                next_char = text.substring(end_idx, end_idx + 2);
                end_idx += 2;
            } else if ((c & 0xF0) == 0xE0) {
                next_char = text.substring(end_idx, end_idx + 3);
                end_idx += 3;
            } else if ((c & 0xF8) == 0xF0) {
                next_char = text.substring(end_idx, end_idx + 4);
                end_idx += 4;
            } else {
                end_idx++;
                continue;
            }

            String test_line = line + next_char;
            int16_t test_w = u8g2_for_gfx.getUTF8Width(test_line.c_str());

            if (test_w > label->w_max && line.length() > 0) {
                end_idx -= next_char.length();
                break;
            }

            line = test_line;
        }

        int16_t line_width = u8g2_for_gfx.getUTF8Width(line.c_str());
        if (line_width > max_width)
            max_width = line_width;

        line_count++;
        start_idx = end_idx;
    }

    // --- ④：根据对齐方式计算 bounds.x ---
    int16_t bounds_x = label->x;
    if (label->align == EpaperTextAlign::CENTER)
        bounds_x = label->x + (label->w_max - max_width) / 2;
    else if (label->align == EpaperTextAlign::RIGHT)
        bounds_x = label->x + label->w_max - max_width;

    bounds.x = bounds_x;
    bounds.y = label->y - ascent;
    bounds.w = max_width;          // 重要修正：非 label->w
    bounds.h = line_count * line_height;

    return bounds;
}