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

private:
    // MCP 工具回调函数
    ReturnValue HandleGetItem(const PropertyList& properties);
    ReturnValue HandleAddItem(const PropertyList& properties);
};

#endif // FRIDGE_MCP_H
