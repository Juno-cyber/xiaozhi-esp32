#include "custom_page_manager.h"
#include "board.h"
#include "display/epaperdisplay/epaper_display.h"
#include "display/epaperdisplay/epaperui.h"
#include <esp_log.h>
#include <esp_system.h>
#include <esp_timer.h>
#include "driver/temperature_sensor.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <algorithm>
#include <unistd.h>

static const char* TAG = "CustomPageMgr";

// ==================== 单例 ====================

CustomPageManager& CustomPageManager::GetInstance() {
    static CustomPageManager instance;
    return instance;
}

// ==================== 工具函数 ====================
static const int MIN_CUSTOM_PAGE = 7;
static const int MAX_CUSTOM_PAGE = 15;
static const int MAX_ELEMENTS_PER_PAGE = 30;

// LittleFS 挂载点（与 local_control.cc 一致）
static const char* CANVAS_MOUNT_POINT = "/canvas";
static const char* REGISTRY_FILE = "/canvas/custom_pages.json";

// ==================== 工具函数 ====================

std::string CustomPageManager::EscapeJson(const std::string& input) {
    std::string output;
    output.reserve(input.size() + 16);
    for (char ch : input) {
        switch (ch) {
            case '\\': output += "\\\\"; break;
            case '"': output += "\\\""; break;
            case '\n': output += "\\n"; break;
            case '\r': break;
            case '\t': output += "\\t"; break;
            default: output += ch; break;
        }
    }
    return output;
}

std::string CustomPageManager::GetLayoutPath(int page) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s/page_%d.json", CANVAS_MOUNT_POINT, page);
    return std::string(buf);
}

std::string CustomPageManager::GetPagePrefix(int page) {
    char buf[16];
    snprintf(buf, sizeof(buf), "cp_p%d_", page);
    return std::string(buf);
}

bool CustomPageManager::IsCustomPage(int page) {
    return page >= MIN_CUSTOM_PAGE && page <= MAX_CUSTOM_PAGE;
}

// ==================== 页面注册表 ====================

void CustomPageManager::LoadRegistry() {
    if (loaded_) return;

    FILE* f = fopen(REGISTRY_FILE, "r");
    if (!f) {
        ESP_LOGI(TAG, "No custom pages registry found");
        loaded_ = true;
        return;
    }

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize <= 0 || fsize > 8192) {
        fclose(f);
        ESP_LOGW(TAG, "Invalid registry file size: %ld", fsize);
        loaded_ = true;
        return;
    }
    fseek(f, 0, SEEK_SET);

    char* buf = (char*)malloc(fsize + 1);
    if (!buf) { fclose(f); loaded_ = true; return; }
    size_t rd = fread(buf, 1, fsize, f);
    buf[rd] = '\0';
    fclose(f);

    // 简单 JSON 解析：找 "page":N 和 "name":"..."
    const char* p = buf;
    while (p && *p) {
        // 找 "page":
        p = strstr(p, "\"page\":");
        if (!p) break;
        int page_num = atoi(p + 7);

        // 找 "name":
        p = strstr(p, "\"name\":");
        if (!p) break;
        p += 7;
        // 跳过空白
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '"') continue;
        p++; // 跳过开头的 "
        const char* name_start = p;
        // 找结束的 "（处理转义）
        while (*p && *p != '"') {
            if (*p == '\\' && *(p+1)) p += 2;
            else p++;
        }
        std::string name(name_start, p - name_start);

        pages_.push_back({page_num, name, ""});
        if (*p) p++;
    }

    free(buf);
    ESP_LOGI(TAG, "Loaded %d custom pages from registry", (int)pages_.size());
    loaded_ = true;
}

void CustomPageManager::SaveRegistry() {
    FILE* f = fopen(REGISTRY_FILE, "w");
    if (!f) {
        ESP_LOGW(TAG, "Failed to write registry file");
        return;
    }

    fprintf(f, "{\"pages\":[");
    for (size_t i = 0; i < pages_.size(); i++) {
        if (i > 0) fprintf(f, ",");
        fprintf(f, "{\"page\":%d,\"name\":\"%s\"}",
                pages_[i].page, EscapeJson(pages_[i].name).c_str());
    }
    fprintf(f, "]}");
    fclose(f);
    ESP_LOGI(TAG, "Registry saved with %d pages", (int)pages_.size());
}

int CustomPageManager::AllocatePage() {
    for (int p = MIN_CUSTOM_PAGE; p <= MAX_CUSTOM_PAGE; p++) {
        bool used = false;
        for (const auto& pi : pages_) {
            if (pi.page == p) { used = true; break; }
        }
        if (!used) return p;
    }
    return -1;  // 已满
}

// ==================== 页面管理 ====================

int CustomPageManager::CreatePage(const std::string& name) {
    LoadRegistry();

    int page = AllocatePage();
    if (page < 0) {
        ESP_LOGW(TAG, "Cannot create page: limit reached (%d pages)", MAX_CUSTOM_PAGE - MIN_CUSTOM_PAGE + 1);
        return -1;
    }

    // 获取当前时间作为创建时间
    time_t now;
    time(&now);
    char time_buf[32];
    struct tm* t = localtime(&now);
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%dT%H:%M:%S", t);

    pages_.push_back({page, name, time_buf});
    SaveRegistry();

    ESP_LOGI(TAG, "Created custom page %d: %s", page, name.c_str());
    return page;
}

bool CustomPageManager::DeletePage(int page) {
    LoadRegistry();
    if (!IsCustomPage(page)) return false;

    // 从注册表删除
    auto it = std::remove_if(pages_.begin(), pages_.end(),
        [page](const PageInfo& pi) { return pi.page == page; });
    if (it == pages_.end()) {
        ESP_LOGW(TAG, "Page %d not found in registry", page);
        return false;
    }
    pages_.erase(it, pages_.end());
    SaveRegistry();

    // 删除布局文件
    std::string path = GetLayoutPath(page);
    unlink(path.c_str());

    // 从屏幕删除该页的 label
    auto* epaper = Board::GetInstance().GetEpaperDisplay();
    if (epaper) {
        std::string prefix = GetPagePrefix(page);
        // 收集要删除的 label id
        std::vector<String> to_remove;
        auto* labels = epaper->GetAllLabels();
        if (labels) {
            for (const auto& pair : *labels) {
                if (strncmp(pair.first.c_str(), prefix.c_str(), prefix.size()) == 0) {
                    to_remove.push_back(pair.first);
                }
            }
        }
        for (const auto& id : to_remove) {
            epaper->RemoveLabel(id);
        }
        ESP_LOGI(TAG, "Removed %d labels from page %d", (int)to_remove.size(), page);
    }

    ESP_LOGI(TAG, "Deleted custom page %d", page);
    return true;
}

std::string CustomPageManager::ListPages() {
    LoadRegistry();

    std::string json = "[";
    for (size_t i = 0; i < pages_.size(); i++) {
        if (i > 0) json += ",";
        json += "{\"page\":" + std::to_string(pages_[i].page);
        json += ",\"name\":\"" + EscapeJson(pages_[i].name) + "\"";
        if (!pages_[i].created.empty()) {
            json += ",\"created\":\"" + EscapeJson(pages_[i].created) + "\"";
        }
        json += "}";
    }
    json += "]";
    return json;
}

bool CustomPageManager::RenamePage(int page, const std::string& name) {
    LoadRegistry();
    for (auto& pi : pages_) {
        if (pi.page == page) {
            pi.name = name;
            SaveRegistry();
            ESP_LOGI(TAG, "Renamed page %d to %s", page, name.c_str());
            return true;
        }
    }
    return false;
}

// ==================== 元素管理 ====================

bool CustomPageManager::AddElement(int page, const std::string& id, const std::string& type,
                    const std::string& text, int x, int y,
                    int font_size, const std::string& align,
                    int w, int h, bool filled,
                    int x1, int y1, int x2, int y2, int width,
                    const std::string& image_name,
                    bool dynamic, const std::string& dynamic_type,
                    const std::string& fmt, int update_interval) {
    (void)update_interval;  // 暂不使用，保留给未来设备端定时器
    (void)fmt;
    (void)dynamic;

    LoadRegistry();
    if (!IsCustomPage(page)) return false;

    auto* epaper = Board::GetInstance().GetEpaperDisplay();
    if (!epaper) return false;

    std::string prefix = GetPagePrefix(page);
    std::string full_id = prefix + id;

    // 检查元素数量上限
    int count = 0;
    auto* labels = epaper->GetAllLabels();
    if (labels) {
        for (const auto& pair : *labels) {
            if (strncmp(pair.first.c_str(), prefix.c_str(), prefix.size()) == 0) count++;
        }
    }
    // 如果不是替换已有元素
    if (epaper->GetLabel(String(full_id.c_str())) == nullptr && count >= MAX_ELEMENTS_PER_PAGE) {
        ESP_LOGW(TAG, "Page %d element limit reached (%d)", page, MAX_ELEMENTS_PER_PAGE);
        return false;
    }

    // 获取字体
    const uint8_t* font = nullptr;
    if (font_size <= 12) {
        font = u8g2_font_wqy12_t_gb2312;
    } else {
        font = u8g2_font_wqy16_t_gb2312;
    }

    EpaperTextAlign ealign = EpaperTextAlign::LEFT;
    if (align == "center") ealign = EpaperTextAlign::CENTER;
    else if (align == "right") ealign = EpaperTextAlign::RIGHT;

    EpaperLabel* label = nullptr;

    if (type == "text") {
        int text_h = font_size + 4;
        if (!dynamic_type.empty()) {
            // 动态元素: 创建带 lambda 的 TextValue，文本由 FormatDynamicValue 实时生成
            std::string dtype = dynamic_type;
            label = new EpaperLabel(EpaperLabel::Text(
                [dtype]() -> String { return String(CustomPageManager::FormatDynamicValue(dtype).c_str()); },
                x, y, 276, text_h, font_size, font, GxEPD_BLACK, ealign, 1, true, false, page));
            strncpy(label->dynamic_type, dynamic_type.c_str(), sizeof(label->dynamic_type) - 1);
        } else {
            label = new EpaperLabel(
                EpaperLabel::Text(text.c_str(), x, y, 276, text_h, font_size,
                                 font, GxEPD_BLACK, ealign, 1, true, false, page));
        }
    } else if (type == "rect") {
        label = new EpaperLabel(
            EpaperLabel::Rect(x, y, w, h, filled, GxEPD_BLACK, 1, true, page));
    } else if (type == "line") {
        label = new EpaperLabel(
            EpaperLabel::Line(x1, y1, x2, y2, width, GxEPD_BLACK, 1, true, page));
    } else if (type == "image" && !image_name.empty()) {
        // 从 LittleFS 加载图片
        std::string img_path = std::string(CANVAS_MOUNT_POINT) + "/" + image_name;
        FILE* imgf = fopen(img_path.c_str(), "rb");
        if (!imgf) {
            ESP_LOGW(TAG, "Image file not found: %s", img_path.c_str());
            return false;
        }
        size_t total_bytes = w * h / 8;
        uint8_t* bitmap = (uint8_t*)malloc(total_bytes);
        if (!bitmap) { fclose(imgf); return false; }
        size_t rd = fread(bitmap, 1, total_bytes, imgf);
        if (rd < total_bytes) memset(bitmap + rd, 0, total_bytes - rd);
        fclose(imgf);
        label = new EpaperLabel(
            EpaperLabel::Bitmap(x, y, bitmap, w, h, 1, 1, false, false, false, true, page, image_name.c_str()));
    }

    if (!label) return false;

    epaper->AddLabel(String(full_id.c_str()), label);
    SavePageLayout(page);
    ESP_LOGI(TAG, "Added element '%s' (type=%s) to page %d", id.c_str(), type.c_str(), page);
    return true;
}

bool CustomPageManager::UpdateElementText(int page, const std::string& id, const std::string& text) {
    LoadRegistry();
    if (!IsCustomPage(page)) return false;

    auto* epaper = Board::GetInstance().GetEpaperDisplay();
    if (!epaper) return false;

    std::string full_id = GetPagePrefix(page) + id;
    EpaperLabel* label = epaper->GetLabel(String(full_id.c_str()));
    if (!label) {
        ESP_LOGW(TAG, "Element '%s' not found on page %d", id.c_str(), page);
        return false;
    }

    // 更新文本值（创建静态 TextValue）
    label->text = text.c_str();

    // 持久化到 LittleFS，确保重启后文字不丢失
    SavePageLayout(page);

    ESP_LOGI(TAG, "Updated element '%s' on page %d: %s", id.c_str(), page, text.c_str());
    return true;
}

bool CustomPageManager::RemoveElement(int page, const std::string& id) {
    LoadRegistry();
    if (!IsCustomPage(page)) return false;

    auto* epaper = Board::GetInstance().GetEpaperDisplay();
    if (!epaper) return false;

    std::string full_id = GetPagePrefix(page) + id;
    epaper->RemoveLabel(String(full_id.c_str()));

    SavePageLayout(page);
    ESP_LOGI(TAG, "Removed element '%s' from page %d", id.c_str(), page);
    return true;
}

std::string CustomPageManager::ListElements(int page) {
    LoadRegistry();
    if (!IsCustomPage(page)) return "[]";

    auto* epaper = Board::GetInstance().GetEpaperDisplay();
    if (!epaper) return "[]";

    std::string prefix = GetPagePrefix(page);
    auto* labels = epaper->GetAllLabels();
    if (!labels) return "[]";

    std::string json = "[";
    bool first = true;
    for (const auto& pair : *labels) {
        if (strncmp(pair.first.c_str(), prefix.c_str(), prefix.size()) != 0) continue;
        EpaperLabel* label = pair.second;
        if (label->page != page) continue;

        // 提取不含前缀的 id
        std::string short_id = pair.first.c_str() + prefix.size();

        if (!first) json += ",";
        first = false;

        switch (label->type) {
            case EpaperObjectType::TEXT:
                json += "{\"type\":\"text\",\"id\":\"" + EscapeJson(short_id) +
                       "\",\"text\":\"" + EscapeJson(label->text().c_str()) +
                       "\",\"x\":" + std::to_string((int)label->x) +
                       ",\"y\":" + std::to_string((int)(label->y - label->h + 4)) +
                       ",\"font_size\":" + std::to_string((int)label->h - 4) +
                       ",\"align\":\"" +
                       (label->align == EpaperTextAlign::CENTER ? "center" :
                        label->align == EpaperTextAlign::RIGHT ? "right" : "left") +
                       "\"";
                // 如果是动态元素，追加 dtype 字段
                if (label->dynamic_type[0] != '\0') {
                    json += ",\"dtype\":\"" + std::string(label->dynamic_type) + "\"";
                }
                json += "}";
                break;
            case EpaperObjectType::RECT:
                json += "{\"type\":\"rect\",\"id\":\"" + EscapeJson(short_id) +
                       "\",\"x\":" + std::to_string((int)label->x) +
                       ",\"y\":" + std::to_string((int)label->y) +
                       ",\"w\":" + std::to_string((int)label->w) +
                       ",\"h\":" + std::to_string((int)label->h) +
                       ",\"filled\":" + std::string(label->filled ? "true" : "false") +
                       "}";
                break;
            case EpaperObjectType::LINE:
                json += "{\"type\":\"line\",\"id\":\"" + EscapeJson(short_id) +
                       "\",\"x1\":" + std::to_string((int)label->x) +
                       ",\"y1\":" + std::to_string((int)label->y) +
                       ",\"x2\":" + std::to_string((int)label->x1) +
                       ",\"y2\":" + std::to_string((int)label->y1) +
                       ",\"width\":" + std::to_string((int)label->width) +
                       "}";
                break;
            case EpaperObjectType::BITMAP:
                if (label->image_name[0] != '\0') {
                    json += "{\"type\":\"image\",\"id\":\"" + EscapeJson(short_id) +
                           "\",\"name\":\"" + EscapeJson(label->image_name) +
                           "\",\"x\":" + std::to_string((int)label->x) +
                           ",\"y\":" + std::to_string((int)label->y) +
                           ",\"w\":" + std::to_string((int)label->w) +
                           ",\"h\":" + std::to_string((int)label->h) +
                           "}";
                }
                break;
            default:
                break;
        }
    }
    json += "]";
    return json;
}

bool CustomPageManager::ClearPage(int page) {
    LoadRegistry();
    if (!IsCustomPage(page)) return false;

    auto* epaper = Board::GetInstance().GetEpaperDisplay();
    if (!epaper) return false;

    std::string prefix = GetPagePrefix(page);
    std::vector<String> to_remove;
    auto* labels = epaper->GetAllLabels();
    if (labels) {
        for (const auto& pair : *labels) {
            if (strncmp(pair.first.c_str(), prefix.c_str(), prefix.size()) == 0) {
                to_remove.push_back(pair.first);
            }
        }
    }
    for (const auto& id : to_remove) {
        epaper->RemoveLabel(id);
    }

    // 删除布局文件
    std::string path = GetLayoutPath(page);
    unlink(path.c_str());

    ESP_LOGI(TAG, "Cleared page %d (%d elements removed)", page, (int)to_remove.size());
    return true;
}

// ==================== 布局持久化 ====================

void CustomPageManager::SavePageLayout(int page) {
    if (!IsCustomPage(page)) return;

    auto* epaper = Board::GetInstance().GetEpaperDisplay();
    if (!epaper) return;

    std::string prefix = GetPagePrefix(page);
    std::string path = GetLayoutPath(page);

    // 统计该页的元素数量
    int count = 0;
    auto* labels = epaper->GetAllLabels();
    if (labels) {
        for (const auto& pair : *labels) {
            if (strncmp(pair.first.c_str(), prefix.c_str(), prefix.size()) == 0 &&
                pair.second->page == page) count++;
        }
    }

    if (count == 0) {
        unlink(path.c_str());
        ESP_LOGI(TAG, "No elements on page %d, layout file deleted", page);
        return;
    }

    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        ESP_LOGW(TAG, "Failed to open %s for writing", path.c_str());
        return;
    }

    fprintf(f, "{\"page\":%d,\"elements\":[", page);
    bool first = true;

    if (labels) {
        for (const auto& pair : *labels) {
            if (strncmp(pair.first.c_str(), prefix.c_str(), prefix.size()) != 0) continue;
            EpaperLabel* label = pair.second;
            if (label->page != page) continue;

            std::string short_id = pair.first.c_str() + prefix.size();

            if (!first) fprintf(f, ",");
            first = false;

            switch (label->type) {
                case EpaperObjectType::TEXT:
                    fprintf(f, "{\"type\":\"text\",\"id\":\"%s\",\"text\":\"%s\",\"x\":%d,\"y\":%d,\"font_size\":%d,\"align\":\"%s\"",
                            short_id.c_str(),
                            EscapeJson(label->text().c_str()).c_str(),
                            (int)label->x, (int)(label->y - label->h + 4),
                            (int)label->h - 4,
                            label->align == EpaperTextAlign::CENTER ? "center" :
                            label->align == EpaperTextAlign::RIGHT ? "right" : "left");
                    // 如果是动态元素，追加 dtype 字段
                    if (label->dynamic_type[0] != '\0') {
                        fprintf(f, ",\"dtype\":\"%s\"", label->dynamic_type);
                    }
                    fprintf(f, "}");
                    break;
                case EpaperObjectType::RECT:
                    fprintf(f, "{\"type\":\"rect\",\"id\":\"%s\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d,\"filled\":%s}",
                            short_id.c_str(),
                            (int)label->x, (int)label->y, (int)label->w, (int)label->h,
                            label->filled ? "true" : "false");
                    break;
                case EpaperObjectType::LINE:
                    fprintf(f, "{\"type\":\"line\",\"id\":\"%s\",\"x1\":%d,\"y1\":%d,\"x2\":%d,\"y2\":%d,\"width\":%d}",
                            short_id.c_str(),
                            (int)label->x, (int)label->y, (int)label->x1, (int)label->y1,
                            (int)label->width);
                    break;
                case EpaperObjectType::BITMAP:
                    if (label->image_name[0] != '\0') {
                        fprintf(f, "{\"type\":\"image\",\"id\":\"%s\",\"name\":\"%s\",\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}",
                                short_id.c_str(),
                                label->image_name,
                                (int)label->x, (int)label->y, (int)label->w, (int)label->h);
                    }
                    break;
                default:
                    break;
            }
        }
    }

    fprintf(f, "]}");
    fclose(f);
    ESP_LOGI(TAG, "Page %d layout saved (%d elements)", page, count);
}

void CustomPageManager::LoadPageLayout(int page) {
    if (!IsCustomPage(page)) return;

    std::string path = GetLayoutPath(page);
    FILE* f = fopen(path.c_str(), "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize <= 0 || fsize > 16384) {
        fclose(f);
        return;
    }
    fseek(f, 0, SEEK_SET);

    char* buf = (char*)malloc(fsize + 1);
    if (!buf) { fclose(f); return; }
    size_t rd = fread(buf, 1, fsize, f);
    buf[rd] = '\0';
    fclose(f);

    auto* epaper = Board::GetInstance().GetEpaperDisplay();
    if (!epaper) { free(buf); return; }

    std::string prefix = GetPagePrefix(page);
    const char* p = strstr(buf, "[");  // 跳过外层 {"page":N,"elements": 直接到数组
    if (!p) { free(buf); return; }
    int count = 0;

    // 解析 JSON 数组中的每个元素对象
    while (*p) {
        while (*p && *p != '{') p++;
        if (!*p) break;
        const char* obj_start = p;
        int depth = 0;
        while (*p) {
            if (*p == '{') depth++;
            if (*p == '}') depth--;
            p++;
            if (depth == 0) break;
        }

        std::string obj(obj_start, p - obj_start);

        // 提取字段
        std::string type, id, text, align = "left", img_name;
        int x = 0, y = 0, w = 0, h = 0, font_size = 16;
        int x1 = 0, y1_ = 0, x2 = 0, y2 = 0, width = 1;
        bool filled = false;

        size_t pos;
        pos = obj.find("\"type\":\"");
        if (pos != std::string::npos) type = obj.substr(pos + 8, obj.find("\"", pos + 8) - (pos + 8));
        pos = obj.find("\"id\":\"");
        if (pos != std::string::npos) id = obj.substr(pos + 6, obj.find("\"", pos + 6) - (pos + 6));
        pos = obj.find("\"text\":\"");
        if (pos != std::string::npos) {
            size_t s = pos + 8, e = s;
            while (e < obj.size() && obj[e] != '"') { if (obj[e] == '\\') e++; e++; }
            text = obj.substr(s, e - s);
        }
        pos = obj.find("\"x\":");
        if (pos != std::string::npos) x = atoi(obj.c_str() + pos + 4);
        pos = obj.find("\"y\":");
        if (pos != std::string::npos) y = atoi(obj.c_str() + pos + 4);
        pos = obj.find("\"w\":");
        if (pos != std::string::npos) w = atoi(obj.c_str() + pos + 4);
        pos = obj.find("\"h\":");
        if (pos != std::string::npos) h = atoi(obj.c_str() + pos + 4);
        pos = obj.find("\"font_size\":");
        if (pos != std::string::npos) font_size = atoi(obj.c_str() + pos + 12);
        pos = obj.find("\"align\":\"");
        if (pos != std::string::npos) align = obj.substr(pos + 9, obj.find("\"", pos + 9) - (pos + 9));
        pos = obj.find("\"filled\":");
        if (pos != std::string::npos) filled = (obj.substr(pos + 9, 4) == "true");
        pos = obj.find("\"x1\":");
        if (pos != std::string::npos) x1 = atoi(obj.c_str() + pos + 5);
        pos = obj.find("\"y1\":");
        if (pos != std::string::npos) y1_ = atoi(obj.c_str() + pos + 5);
        pos = obj.find("\"x2\":");
        if (pos != std::string::npos) x2 = atoi(obj.c_str() + pos + 5);
        pos = obj.find("\"y2\":");
        if (pos != std::string::npos) y2 = atoi(obj.c_str() + pos + 5);
        pos = obj.find("\"width\":");
        if (pos != std::string::npos) width = atoi(obj.c_str() + pos + 8);
        pos = obj.find("\"name\":\"");
        if (pos != std::string::npos) img_name = obj.substr(pos + 8, obj.find("\"", pos + 8) - (pos + 8));

        // 解析 dtype 字段（动态元素类型）
        std::string dtype;
        pos = obj.find("\"dtype\":\"");
        if (pos != std::string::npos) {
            size_t s = obj.find("\"", pos + 9);
            if (s != std::string::npos) {
                size_t e = obj.find("\"", s + 1);
                if (e != std::string::npos) {
                    dtype = obj.substr(s + 1, e - s - 1);
                }
            }
        }

        if (type.empty() || id.empty()) continue;

        std::string full_id = prefix + id;
        const uint8_t* font = (font_size <= 12) ? u8g2_font_wqy12_t_gb2312 : u8g2_font_wqy16_t_gb2312;
        EpaperTextAlign ealign = EpaperTextAlign::LEFT;
        if (align == "center") ealign = EpaperTextAlign::CENTER;
        else if (align == "right") ealign = EpaperTextAlign::RIGHT;

        if (type == "text") {
            int text_h = font_size + 4;
            if (!dtype.empty()) {
                // 动态元素: 创建带 lambda 的 TextValue
                std::string dt = dtype;
                epaper->AddLabel(String(full_id.c_str()), new EpaperLabel(
                    EpaperLabel::Text([dt]() -> String { return String(CustomPageManager::FormatDynamicValue(dt).c_str()); },
                                     x, y, 276, text_h, font_size, font, GxEPD_BLACK, ealign, 1, true, false, page)));
                // 设置 dynamic_type 字段
                auto* lbl = epaper->GetLabel(String(full_id.c_str()));
                if (lbl) strncpy(lbl->dynamic_type, dt.c_str(), sizeof(lbl->dynamic_type) - 1);
            } else {
                epaper->AddLabel(String(full_id.c_str()), new EpaperLabel(
                    EpaperLabel::Text(text.c_str(), x, y, 276, text_h, font_size,
                                     font, GxEPD_BLACK, ealign, 1, true, false, page)));
            }
            count++;
        } else if (type == "rect") {
            epaper->AddLabel(String(full_id.c_str()), new EpaperLabel(
                EpaperLabel::Rect(x, y, w, h, filled, GxEPD_BLACK, 1, true, page)));
            count++;
        } else if (type == "line") {
            epaper->AddLabel(String(full_id.c_str()), new EpaperLabel(
                EpaperLabel::Line(x1, y1_, x2, y2, width, GxEPD_BLACK, 1, true, page)));
            count++;
        } else if (type == "image" && !img_name.empty()) {
            std::string img_path = std::string(CANVAS_MOUNT_POINT) + "/" + img_name;
            FILE* imgf = fopen(img_path.c_str(), "rb");
            if (imgf) {
                size_t total_bytes = w * h / 8;
                uint8_t* bitmap = (uint8_t*)malloc(total_bytes);
                if (bitmap) {
                    size_t rdb = fread(bitmap, 1, total_bytes, imgf);
                    if (rdb < total_bytes) memset(bitmap + rdb, 0, total_bytes - rdb);
                    epaper->AddLabel(String(full_id.c_str()), new EpaperLabel(
                        EpaperLabel::Bitmap(x, y, bitmap, w, h, 1, 1, false, false, false, true, page, img_name.c_str())));
                    count++;
                }
                fclose(imgf);
            }
        }
    }

    free(buf);
    ESP_LOGI(TAG, "Page %d layout restored: %d elements", page, count);
}

void CustomPageManager::LoadAllPages() {
    LoadRegistry();

    for (const auto& pi : pages_) {
        LoadPageLayout(pi.page);
    }
    ESP_LOGI(TAG, "All custom pages loaded (%d pages)", (int)pages_.size());

    // 启动动态元素更新定时器
    StartDynamicTimer();
}

// ==================== 动态元素更新 ====================

// 静态温度传感器句柄（懒初始化）
static temperature_sensor_handle_t s_temp_sensor = nullptr;
static bool s_temp_sensor_init_done = false;

static float ReadChipTemperature() {
    // 懒初始化温度传感器
    if (!s_temp_sensor_init_done) {
        s_temp_sensor_init_done = true;
        temperature_sensor_config_t temp_cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
        if (temperature_sensor_install(&temp_cfg, &s_temp_sensor) == ESP_OK) {
            temperature_sensor_enable(s_temp_sensor);
            ESP_LOGI("CustomPageMgr", "Temperature sensor installed");
        } else {
            ESP_LOGW("CustomPageMgr", "Failed to install temperature sensor");
            s_temp_sensor = nullptr;
        }
    }
    float celsius = 0.0f;
    if (s_temp_sensor) {
        if (temperature_sensor_get_celsius(s_temp_sensor, &celsius) != ESP_OK) {
            celsius = 0.0f;
        }
    }
    return celsius;
}

std::string CustomPageManager::FormatDynamicValue(const std::string& dtype) {
    if (dtype == "clock") {
        // 当前时间 HH:MM
        time_t now = time(nullptr);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        char buf[16];
        strftime(buf, sizeof(buf), "%H:%M", &tm_now);
        return std::string(buf);
    } else if (dtype == "date") {
        // 日期 YYYY-MM-DD 周X
        time_t now = time(nullptr);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_now);
        const char* weekdays[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
        int wday = tm_now.tm_wday;
        if (wday < 0 || wday > 6) wday = 0;
        size_t cur = strlen(buf);
        snprintf(buf + cur, sizeof(buf) - cur, " %s", weekdays[wday]);
        return std::string(buf);
    } else if (dtype == "datetime") {
        // 日期时间 MM-DD HH:MM
        time_t now = time(nullptr);
        struct tm tm_now;
        localtime_r(&now, &tm_now);
        char buf[24];
        strftime(buf, sizeof(buf), "%m-%d %H:%M", &tm_now);
        return std::string(buf);
    } else if (dtype == "cpu_temp") {
        // 芯片温度 XX.X°C
        float temp = ReadChipTemperature();
        char buf[16];
        snprintf(buf, sizeof(buf), "%.1f°C", temp);
        return std::string(buf);
    } else if (dtype == "heap") {
        // 空闲堆内存
        char buf[20];
        snprintf(buf, sizeof(buf), "Heap: %luKB", (unsigned long)(esp_get_free_heap_size() / 1024));
        return std::string(buf);
    } else if (dtype == "uptime") {
        // 运行时间
        uint64_t secs = (uint64_t)(esp_timer_get_time() / 1000000ULL);
        int d = (int)(secs / 86400);
        int h = (int)((secs % 86400) / 3600);
        int m = (int)((secs % 3600) / 60);
        char buf[24];
        snprintf(buf, sizeof(buf), "Up: %dd %dh %dm", d, h, m);
        return std::string(buf);
    }
    return std::string("");
}

void CustomPageManager::StartDynamicTimer() {
    if (dynamic_timer_ != nullptr) return;

    esp_timer_init();
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = [](void* arg) {
        CustomPageManager::GetInstance().OnDynamicTick();
    };
    timer_args.name = "cpm_dyn";
    esp_err_t err = esp_timer_create(&timer_args, &dynamic_timer_);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to create dynamic timer: %s", esp_err_to_name(err));
        return;
    }
    // 每 1 秒触发一次，实际刷新频率由 TickDynamicUpdate 内部节流
    esp_timer_start_periodic(dynamic_timer_, 1000000);
    ESP_LOGI(TAG, "Dynamic timer started (1s period)");
}

void CustomPageManager::OnDynamicTick() {
    // esp_timer 回调在 timer task 中执行，直接调用 TickDynamicUpdate
    // TickDynamicUpdate 内部会加锁并做节流
    TickDynamicUpdate();
}

void CustomPageManager::TickDynamicUpdate() {
    auto* epaper = Board::GetInstance().GetEpaperDisplay();
    if (!epaper) return;

    uint16_t current_page = epaper->GetCurrentPage();
    // 只在自定义页面 (7-15) 更新
    if (current_page < MIN_CUSTOM_PAGE || current_page > MAX_CUSTOM_PAGE) return;

    // 检查当前页是否有动态元素
    std::string prefix = GetPagePrefix(current_page);
    auto* labels = epaper->GetAllLabels();
    if (!labels) return;

    bool has_dynamic = false;
    for (const auto& pair : *labels) {
        if (strncmp(pair.first.c_str(), prefix.c_str(), prefix.size()) != 0) continue;
        if (pair.second->type == EpaperObjectType::TEXT &&
            pair.second->dynamic_type[0] != '\0') {
            has_dynamic = true;
            break;
        }
    }

    if (!has_dynamic) return;

    // 只做局部刷新（false = partial）
    DisplayLockGuard lock(epaper);
    epaper->UpdateUI(false);
}
