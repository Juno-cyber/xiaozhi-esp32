#ifndef CUSTOM_PAGE_MANAGER_H
#define CUSTOM_PAGE_MANAGER_H

#include <string>
#include <vector>
#include <cstdint>
#include <esp_timer.h>

// 自定义页面管理器
// 管理用户自建页面（7-15）的注册表和布局持久化
// 页面编号 1-6 为系统内置页面，7-15 为用户自定义页面
class CustomPageManager {
public:
    static CustomPageManager& GetInstance();

    // 页面管理
    int CreatePage(const std::string& name);       // 创建页面，返回分配的页码 (7-15)，失败返回 -1
    bool DeletePage(int page);                     // 删除页面 + 布局文件
    std::string ListPages();                       // 返回 JSON 数组
    bool RenamePage(int page, const std::string& name);

    // 元素管理
    bool AddElement(int page, const std::string& id, const std::string& type,
                    const std::string& text, int x, int y,
                    int font_size, const std::string& align,
                    int w, int h, bool filled,
                    int x1, int y1, int x2, int y2, int width,
                    const std::string& image_name,
                    bool dynamic, const std::string& dynamic_type,
                    const std::string& fmt, int update_interval);
    bool UpdateElementText(int page, const std::string& id, const std::string& text);
    bool RemoveElement(int page, const std::string& id);
    std::string ListElements(int page);
    bool ClearPage(int page);

    // 持久化
    void SavePageLayout(int page);
    void LoadPageLayout(int page);
    void LoadAllPages();    // 启动时调用，加载所有自定义页面

    // 动态元素更新（供外部定时器调用，如 application.cc 的 clock_timer）
    void TickDynamicUpdate();

    // 格式化动态值（静态方法，供 lambda 调用）
    static std::string FormatDynamicValue(const std::string& dtype);

    // 工具函数
    static std::string GetLayoutPath(int page);    // 返回布局文件路径
    static std::string GetPagePrefix(int page);    // 返回 label 前缀，如 "cp_p7_"
    static bool IsCustomPage(int page);            // 是否为自定义页面 (7-15)

    // 页面信息结构
    struct PageInfo {
        int page;
        std::string name;
        std::string created;
    };

    // 获取页面列表（内部用）
    const std::vector<PageInfo>& GetPages() const { return pages_; }

private:
    CustomPageManager() = default;
    ~CustomPageManager() = default;
    CustomPageManager(const CustomPageManager&) = delete;
    CustomPageManager& operator=(const CustomPageManager&) = delete;

    std::vector<PageInfo> pages_;
    bool loaded_ = false;
    esp_timer_handle_t dynamic_timer_ = nullptr;  // 动态元素更新定时器

    void LoadRegistry();
    void SaveRegistry();
    int AllocatePage();
    std::string EscapeJson(const std::string& input);
    void StartDynamicTimer();   // 启动动态更新定时器
    void OnDynamicTick();       // 定时器回调（内部）
};

#endif // CUSTOM_PAGE_MANAGER_H
