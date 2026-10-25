// ============================================================================
//  server/routes.hpp —— 路由注册入口
// ============================================================================
#pragma once
#ifndef AGENTCPP_SERVER_ROUTES_HPP
#define AGENTCPP_SERVER_ROUTES_HPP

#include "server/http_server.hpp"

namespace server
{
    // 注册全部 REST / SSE 路由，并挂上 WebUI 静态资源兜底
    void register_routes(http::Router &router);
} // namespace server

#endif // AGENTCPP_SERVER_ROUTES_HPP
