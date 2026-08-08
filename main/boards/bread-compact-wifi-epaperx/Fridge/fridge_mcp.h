#ifndef FRIDGE_MCP_H
#define FRIDGE_MCP_H

#include "mcp_server.h"
#include "fridge_manager.h"

// 冰箱管理 MCP 工具类
class FridgeMcpTools {
public:
    FridgeMcpTools() = default;
    
    // 初始化工具，注册到 MCP 服务器
    void Initialize();

public:
    // LittleFS 挂载后调用，恢复画布布局
    static void RestoreCanvasLayout();

private:
    // MCP 工具回调函数
    ReturnValue HandleGetItem(const PropertyList& properties);
    ReturnValue HandleAddItem(const PropertyList& properties);
    ReturnValue HandleRemoveItem(const PropertyList& properties);
    ReturnValue HandleClearAll(const PropertyList& properties);
    ReturnValue HandleStatsSummary(const PropertyList& properties);
    ReturnValue HandleStatsQuery(const PropertyList& properties);
    ReturnValue HandleItemList(const PropertyList& properties);
    ReturnValue HandleItemUpdate(const PropertyList& properties);
    ReturnValue HandlePageManager(const PropertyList& properties);
    ReturnValue HandleRecipeRecommend(const PropertyList& properties);
    // Canvas 工具
    ReturnValue HandleCanvasAddText(const PropertyList& properties);
    ReturnValue HandleCanvasAddRect(const PropertyList& properties);
    ReturnValue HandleCanvasAddLine(const PropertyList& properties);
    ReturnValue HandleCanvasAddImage(const PropertyList& properties);
    ReturnValue HandleCanvasList(const PropertyList& properties);
    ReturnValue HandleCanvasRemove(const PropertyList& properties);
    ReturnValue HandleCanvasClear(const PropertyList& properties);
    ReturnValue HandleCanvasRefresh(const PropertyList& properties);
    // 自定义页面工具
    ReturnValue HandlePageCreate(const PropertyList& properties);
    ReturnValue HandlePageDelete(const PropertyList& properties);
    ReturnValue HandlePageList(const PropertyList& properties);
    ReturnValue HandlePageRename(const PropertyList& properties);
    ReturnValue HandleElementAdd(const PropertyList& properties);
    ReturnValue HandleElementUpdate(const PropertyList& properties);
    ReturnValue HandleElementRemove(const PropertyList& properties);
    ReturnValue HandleElementList(const PropertyList& properties);
    ReturnValue HandlePageClear(const PropertyList& properties);
    ReturnValue HandleNetworkInfo(const PropertyList& properties);
};

#endif // FRIDGE_MCP_H
