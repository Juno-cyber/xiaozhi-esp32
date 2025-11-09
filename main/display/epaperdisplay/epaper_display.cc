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

}

void EpaperDisplay::SetEmotion(const char* emotion) {  

 
}  
  
void EpaperDisplay::SetChatMessage(const char* role, const char* content) {  

}  

void EpaperDisplay::ShowNotification(const std::string &notification, int duration_ms) {

}

void EpaperDisplay::ShowNotification(const char* notification, int duration_ms) {

}

void EpaperDisplay::UpdateStatusBar(bool update_all) {
    auto& app = Application::GetInstance();
    
    // 更新时间显示
    if (app.GetDeviceState() == kDeviceStateIdle || update_all) {
        // 每 10 秒更新一次时间
        if (update_all || last_status_update_time_ + std::chrono::seconds(10) < std::chrono::system_clock::now()) {
            time_t now = time(NULL);
            struct tm* tm = localtime(&now);
            
            // 检查系统时间是否已设置
            if (tm->tm_year >= 2025 - 1900) {
                char time_str[16];
                strftime(time_str, sizeof(time_str), "%H:%M", tm);
                
                ESP_LOGI(TAG, "Updating time to: %s", time_str);
                
                // 更新 status-time label
                auto* time_label = GetLabel("status-time");
                if (time_label != nullptr) {
                    time_label->text = String(time_str);
                    UpdateLabel("status-time"); // 局部刷新时间显示
                    ESP_LOGI(TAG, "Updated status-time label to: %s", time_label->text.c_str());
                } else {
                    ESP_LOGW(TAG, "status-time label not found!");
                }
                
                // 更新时间戳
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
    
    // 示例：使用工厂函数初始化 UI 元素
    
    // 标题文本（使用 U8g2 字体）
    // #include <u8g2_font_wqy12_t_gb2312.h>
    // ui_labels_["title"] = new EpaperLabel(EpaperLabel::Text(
    //     "Xiaozhi AI", 10, 20, u8g2_font_wqy12_t_gb2312, GxEPD_BLACK, EpaperTextAlign::LEFT, 1
    // ));
    
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
    // ui_labels_["image"] = new EpaperLabel(EpaperLabel::Bitmap(
    //     0, 0, (const uint8_t*)Image1_leju, 128, 296, 1, 2,false,true,true
    // ));

    // 小智应答界面
    // 时间显示（U8g2 字体同样支持英文）
    ui_labels_["status-time"] = new EpaperLabel(
        EpaperLabel::Text("05:20", 103, 20, 90, u8g2_font_crox4tb_tn, GxEPD_BLACK,
                          EpaperTextAlign::CENTER, 1));
    // 图标显示
    ui_labels_["icon-show"] = new EpaperLabel(
        EpaperLabel::Text("\u0032", 10,30, 21, u8g2_font_emoticons21_tr, GxEPD_BLACK,
                          EpaperTextAlign::CENTER, 1));    
    
    // 分隔线
    ui_labels_["divider"] =
        new EpaperLabel(EpaperLabel::Line(20, 30, 276, 30, GxEPD_BLACK, 1));
    
    // 中文显示示例
    ui_labels_["talk_chinese"] = new EpaperLabel(EpaperLabel::Text(
        "你好，世界！这是小智的中文显示，huanhangkankan", 48, 80, 200, u8g2_font_wqy16_t_gb2312,
        GxEPD_BLACK, EpaperTextAlign::CENTER, 1
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
            // 统一使用 U8g2 字体渲染（支持中英文）
            if (label->u8g2_font != nullptr) {
                u8g2_for_gfx.setFont(label->u8g2_font);
                u8g2_for_gfx.setForegroundColor(label->color);
                
                // 如果设置了最大宽度，执行换行逻辑
                if (label->w > 0) {
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
    DisplayLockGuard lock(this);
    
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
    uint16_t refresh_w = label->w;
    uint16_t refresh_h = label->h;
    
    // 如果是文本类型，需要动态计算边界
    if (label->type == EpaperObjectType::TEXT) {
        display_epaper.setRotation(label->rotation);
        
        // 使用 U8g2 字体边界计算
        if (label->u8g2_font != nullptr) {
            u8g2_for_gfx.setFont(label->u8g2_font);
            
            // 如果设置了宽度限制，计算多行文本边界
            if (label->w > 0) {
                auto bounds = CalculateTextBounds(label);
                refresh_x = bounds.x;
                refresh_y = bounds.y;
                refresh_w = bounds.w;
                refresh_h = bounds.h;
            } else {
                // 单行文本
                int16_t text_w = u8g2_for_gfx.getUTF8Width(label->text.c_str());
                int16_t text_h = u8g2_for_gfx.getFontAscent();
                
                // 根据对齐方式计算实际渲染起始位置
                int16_t render_x = label->x;
                if (label->align == EpaperTextAlign::CENTER) {
                    render_x = label->x - text_w / 2;
                } else if (label->align == EpaperTextAlign::RIGHT) {
                    render_x = label->x - text_w;
                }
                
                // 设置刷新区域（添加小边距以确保完全覆盖）
                // label->y 是基线位置，getFontAscent() 是基线以上的高度
                refresh_x = render_x;
                refresh_y = label->y - u8g2_for_gfx.getFontAscent();  // 从基线向上找到文本顶部
                refresh_w = text_w;
                refresh_h = text_h;
            }
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

// 渲染带有换行的文本
void EpaperDisplay::RenderTextWithWrap(EpaperLabel* label) {
    if (label->u8g2_font == nullptr || label->w == 0) {
        return;
    }
    
    u8g2_for_gfx.setFont(label->u8g2_font);
    u8g2_for_gfx.setForegroundColor(label->color);
    
    String text = label->text;
    
    // 先检查整个文本是否超出宽度，如果没超出就不换行
    int16_t total_width = u8g2_for_gfx.getUTF8Width(text.c_str());
    if (total_width <= label->w) {
        // 文本没有超出宽度，单行显示
        int16_t cursor_x = label->x;
        if (label->align == EpaperTextAlign::CENTER) {
            cursor_x = label->x + (label->w - total_width) / 2;
        } else if (label->align == EpaperTextAlign::RIGHT) {
            cursor_x = label->x + label->w - total_width;
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
            
            if (test_width > label->w && line.length() > 0) {
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
            cursor_x = label->x + (label->w - line_width) / 2;
        } else if (label->align == EpaperTextAlign::RIGHT) {
            cursor_x = label->x + label->w - line_width;
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
    
    if (label->u8g2_font == nullptr) {
        return bounds;
    }
    
    u8g2_for_gfx.setFont(label->u8g2_font);
    
    if (label->w > 0) {
        // 先检查整个文本是否超出宽度
        int16_t total_width = u8g2_for_gfx.getUTF8Width(label->text.c_str());
        
        if (total_width <= label->w) {
            // 文本没有超出，按单行计算
            int16_t bounds_x = label->x;
            if (label->align == EpaperTextAlign::CENTER) {
                bounds_x = label->x + (label->w - total_width) / 2;
            } else if (label->align == EpaperTextAlign::RIGHT) {
                bounds_x = label->x + label->w - total_width;
            }
            
            bounds.x = bounds_x;
            bounds.y = label->y - u8g2_for_gfx.getFontAscent();
            bounds.w = total_width;
            bounds.h = u8g2_for_gfx.getFontAscent();
        } else {
            // 有宽度限制且超出，计算多行文本边界
            String text = label->text;
            int text_len = text.length();
            int start_idx = 0;
            // getFontAscent() 就是行高，再加上2像素行间距
            int16_t line_height = u8g2_for_gfx.getFontAscent() + 6;
            int line_count = 0;
            int16_t max_width = 0;
            
            while (start_idx < text_len) {
                String line = "";
                int end_idx = start_idx;
                
                // 逐个字符测试
                while (end_idx < text_len) {
                    String next_char = "";
                    uint8_t c = text[end_idx];
                    
                    if ((c & 0x80) == 0) {
                        next_char = String((char)c);
                        end_idx++;
                    } else if ((c & 0xE0) == 0xC0) {
                        if (end_idx + 1 < text_len) {
                            next_char = text.substring(end_idx, end_idx + 2);
                            end_idx += 2;
                        } else {
                            break;
                        }
                    } else if ((c & 0xF0) == 0xE0) {
                        if (end_idx + 2 < text_len) {
                            next_char = text.substring(end_idx, end_idx + 3);
                            end_idx += 3;
                        } else {
                            break;
                        }
                    } else if ((c & 0xF8) == 0xF0) {
                        if (end_idx + 3 < text_len) {
                            next_char = text.substring(end_idx, end_idx + 4);
                            end_idx += 4;
                        } else {
                            break;
                        }
                    } else {
                        end_idx++;
                        continue;
                    }
                    
                    String test_line = line + next_char;
                    int16_t test_width = u8g2_for_gfx.getUTF8Width(test_line.c_str());
                    
                    if (test_width > label->w && line.length() > 0) {
                        end_idx -= next_char.length();
                        break;
                    }
                    
                    line = test_line;
                }
                
                // 记录最大宽度
                int16_t line_width = u8g2_for_gfx.getUTF8Width(line.c_str());
                if (line_width > max_width) {
                    max_width = line_width;
                }
                
                line_count++;
                start_idx = end_idx;
            }
            
            // 根据对齐方式计算 x 起始位置
            int16_t bounds_x = label->x;
            if (label->align == EpaperTextAlign::CENTER) {
                bounds_x = label->x;  // 中心对齐，使用文本框宽度
            } else if (label->align == EpaperTextAlign::RIGHT) {
                bounds_x = label->x + label->w - max_width;  // 右对齐
            }
            
            bounds.x = bounds_x;
            bounds.y = label->y - u8g2_for_gfx.getFontAscent();
            bounds.w = label->w;  // 使用文本框宽度
            bounds.h = line_count * line_height;
        }
    } else {
        // 单行文本
        int16_t text_w = u8g2_for_gfx.getUTF8Width(label->text.c_str());
        int16_t text_h = u8g2_for_gfx.getFontAscent();
        
        int16_t bounds_x = label->x;
        if (label->align == EpaperTextAlign::CENTER) {
            bounds_x = label->x - text_w / 2;
        } else if (label->align == EpaperTextAlign::RIGHT) {
            bounds_x = label->x - text_w;
        }
        
        bounds.x = bounds_x;
        bounds.y = label->y - u8g2_for_gfx.getFontAscent();
        bounds.w = text_w;
        bounds.h = text_h;
    }
    
    return bounds;
}

