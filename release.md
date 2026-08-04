# 更新内容

0. 新增 MCP 工具映射系统：
    - 支持将任意 MCP (Model Context Protocol) 服务器的工具映射为本地工具
    - 内置 MCP stdio / JSON-RPC 2.0 客户端（Windows 与 POSIX 双平台实现）
    - 兼容新行分隔与 Content-Length 两种帧格式，自动识别响应消息
    - 完整握手流程：`initialize` → `notifications/initialized` → `tools/list` → `tools/call`
    - Agent 可直接以 `<tool>name:{"参数":"值"}</tool>` 调用映射后的 MCP 工具，参数以 JSON 对象透传，非 JSON 参数自动回退为 `{"input": "..."}`
    - 映射工具列表与 LLM 提示词合并展示，自动附带参数 JSON schema 说明，引导模型正确传参
    - 服务器配置与工具映射持久化到 `workspace/tools/mcp_tools.json`（`servers` + `tools` 结构）
1. WebUI 工具面板大改：
    - 新增 MCP 服务器管理区：添加服务器（命令/参数/环境变量）、扫描远程工具、映射/取消映射、删除
    - 工具列表中 MCP 工具带紫色徽标标记，可一键启用/禁用
2. 后端新增 MCP 管理 API：
    - `GET /api/mcp` 查询配置；`POST /api/mcp/save` 添加/更新服务器
    - `POST /api/mcp/delete` 删除服务器（级联移除映射）
    - `POST /api/mcp/scan` 连接服务器并列出远程工具
    - `POST /api/mcp/map` / `POST /api/mcp/unmap` 映射/取消映射工具
    - `POST /api/mcp/toggle` 启用/禁用已映射的 MCP 工具
3. 工具列表整合：`GET /api/tools` 现在返回本地工具 + 已映射的 MCP 工具合并列表，`/api/tools/toggle` 自动路由到对应存储
4. 支持的环境变量配置：MCP 服务器可自定义环境变量（Windows 下自动合并继承环境 + 覆盖项）

# 从源码构建

依赖需求：

```bash
#Linux：
sudo apt-get install -y libcurl4-openssl-dev libboost-dev

#Windows （msys2）：
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja mingw-w64-x86_64-curl mingw-w64-x86_64-boost

#MacOS：
brew install cmake ninja curl boost
```

懒狗安装指令：

```bash
rm -rf ./* && git clone --recurse-submodules https://github.com/yauntyour/agent.cpp.git && cd agent.cpp && mkdir build && cd build && cmake .. && cmake --build build && cmake --install build --prefix install
```

# 从release下载
