# agent.cpp

> Modular AI Coding Agent — v2.0.0

agent.cpp 是一个基于 C++20 构建的模块化 AI 编程代理，运行在终端环境中。它通过多轮对话与工具调用，帮助开发者完成代码读写、搜索重构、命令执行、Web 搜索、子代理调度等软件工程任务。

## 特性

- **多 LLM 提供商** — 支持 OpenAI、Anthropic、Ollama、llama.cpp server 等多种后端
- **流式响应** — 所有提供商均支持实时 token 流式输出
- **17+ 内置工具** — read、write、edit、search、exec、websearch、webfetch 等
- **子代理系统** — 支持前台/后台/浏览型子代理并行执行
- **会话管理** — 多项目、多命名会话持久化
- **长期记忆** — 基于关键词和文本的记忆检索与自动生成
- **权限控制** — 工具级权限（自动/询问/拒绝）及危险命令检测
- **文件编辑历史** — 文件变更追踪与回滚
- **MCP 集成** — Model Context Protocol，可动态扩展工具
- **LSP 集成** — Language Server Protocol，支持悬停、补全、诊断、格式化、跳转定义、重命名、代码动作
- **HTTP API 服务器** — RESTful 接口，支持流式响应、认证与 CORS
- **TUI 界面** — ANSI 终端界面，含状态栏、命令补全、编辑展示
- **Telegram 机器人** — 通过 Telegram 与代理交互
- **加密密钥链** — Argon2id 密钥派生 + XSalsa20-Poly1305 加密存储 API 密钥
- **跨平台** — 支持 Windows (MinGW)、Linux、macOS

## 快速开始

### 依赖

- CMake ≥ 3.22
- C++20 编译器（GCC、Clang、MSVC）
- [libcurl](https://curl.se/libcurl/)
- [libsodium](https://doc.libsodium.org/)
- [nlohmann_json](https://github.com/nlohmann/json)（header-only）
- (可选) [Boost.Asio](https://www.boost.org/) — HTTP Router 模式需要
- (可选) [OpenSSL](https://www.openssl.org/) — Router TLS 支持需要

### 构建

```bash
git clone https://github.com/yauntyour/agent.cpp.git
cd agent.cpp
git submodule update --init
cmake -B build -G Ninja
cmake --build build
```

构建产物为 `build/agent`（Windows 下为 `build/agent.exe`）。

### CMake 选项

| 选项 | 默认值 | 说明 |
|---|---|---|
| `AGENT_ENABLE_TUI` | `ON` | ANSI 终端界面 |
| `AGENT_ENABLE_ROUTER` | `ON` | HTTP API 服务器（需 Boost.Asio） |
| `AGENT_ENABLE_LSP` | `ON` | LSP 语言服务集成 |
| `AGENT_ENABLE_TESTS` | `OFF` | 单元测试 |
| `AGENT_STATIC_LINK` | `ON` | 静态链接（MinGW） |

## 使用方式

### CLI 模式（默认）

```bash
./agent
```

进入交互式 REPL，直接输入问题或 `/help` 查看命令列表。

### 单次执行

```bash
./agent --command "找出所有使用 printf 的地方并替换为 std::print"
```

### TUI 模式

```bash
./agent --tui
```

启动 ANSI 终端界面，提供状态栏、命令自动补全、编辑内容展示等功能。

### HTTP 服务器模式

```bash
./agent --router
```

启动 REST API 服务器，支持流式聊天补全、工具调用、会话管理。默认监听 `127.0.0.1:8080`。

### 选项

```
--tui, -t          启动 TUI 界面
--router, -r       启动 HTTP API 服务器
--daemon, -d       以守护进程运行
--project, -p DIR  设置项目目录
--session, -s ID   使用指定会话
--command, -c CMD  执行单条命令后退出
--reset-config     重置所有配置
--version, -v      显示版本号
--help, -h         显示帮助
```

## 命令

CLI 模式下使用 `/` 前缀执行内置命令：

| 命令 | 说明 |
|---|---|
| `/help` | 显示帮助 |
| `/model` | 查看/切换当前模型 |
| `/provider` | 管理 LLM 提供商 |
| `/session` | 切换会话 |
| `/sessions` | 列出所有会话 |
| `/new` | 创建新会话 |
| `/delete` | 删除会话 |
| `/history` | 查看对话历史 |
| `/memory` | 管理长期记忆 |
| `/mcp` | 管理 MCP 服务器 |
| `/lsp` | 管理 LSP 服务器 |
| `/config` | 查看/修改配置 |
| `/reset` | 重置当前会话 |
| `/quit` | 退出 |

## 配置

配置文件位于 `<可执行文件目录>/.agent/config.json`，为 JSON 格式，支持：

- 多个命名提供商（Provider）与模型
- 工具权限规则（自动/询问/拒绝）
- 危险命令模式匹配
- LSP 服务器配置
- MCP 服务器配置
- 电报机器人（Channel/Telegram）配置
- HTTP 服务器端口与绑定地址
- 最近项目追踪

## 架构

```
┌─────────────────────────────────────────────────────┐
│                     main.cpp                        │
├─────────────────────────────────────────────────────┤
│  ModuleRegistry (模块注册表，单例)                    │
├──────────┬──────────┬──────────┬────────────────────┤
│ Provider  │  Tools   │  Agent   │  System            │
│ (LLM 抽象)│ (工具系统)│ (MPC循环)│ (CLI/命令/通道)    │
├──────────┼──────────┼──────────┼────────────────────┤
│ Session   │ Memory   │ Permission │ EditHistory      │
│ (会话管理)│ (长期记忆)│ (权限控制) │ (编辑历史)         │
├──────────┼──────────┼──────────┼────────────────────┤
│ MCP       │ LSP*     │ Service  │ Notice             │
│ (MCP协议) │ (语言服务)│ (子进程)  │ (事件通知)          │
├──────────┼──────────┴──────────┼────────────────────┤
│ Router*  │     TUI*            │  (可选组件)         │
│ (HTTP API)│   (终端界面)        │                    │
└──────────┴─────────────────────┴────────────────────┘
```

### Agent MPC 循环

1. 用户输入加入消息列表
2. 检索相关记忆注入上下文
3. 工具定义发送至 LLM
4. LLM 流式/非流式生成响应
5. 解析工具调用（XML `<tool>` 标签或 JSON `tool_calls`）
6. 权限校验
7. 执行工具，结果追加至消息列表
8. 重复直至无工具调用或达到最大迭代次数（默认 30）

### 模块系统

基于 CRTP 的模块生命周期管理：

```
IModule (接口) → Module<T> (CRTP基类) → 具体模块
```

状态机：`Uninitialized → Initializing → Active → ShuttingDown → Terminated`

`ModuleRegistry` 提供类型安全的单例注册、依赖获取、批量初始化和安全关闭。

## 工具列表

| 工具 | 说明 |
|---|---|
| `read` | 读取文件内容（支持行偏移/限制） |
| `write` | 写入/覆盖文件 |
| `edit` | 按行范围或字符串替换编辑文件 |
| `search` | 正则/通配符/文本搜索（遵循 `.gitignore`） |
| `exec` | 执行 shell 命令（含危险命令检测） |
| `task` | 启动前台/后台服务进程 |
| `question` | 向用户提问 |
| `websearch` | Bing/Google Web 搜索 |
| `webfetch` | URL 内容获取（支持断点续传） |
| `mind-map` | ASCII 思维导图 |
| `todolist` | 结构化任务列表 |
| `memory` | 记忆保存/搜索/列表 |
| `image` | 图片读取/编码（需视觉模型） |
| `fs` | 文件系统操作（ls/mkdir/rm/cp/mv） |
| `subagent` | 启动子代理（浏览/前台/后台） |
| `git-saved` | Git 保存点（自动 commit） |
| `git-restore` | 恢复到之前保存点（git reset） |

MCP 服务器可动态添加更多工具。

## 贡献

欢迎提交 Issue 和 Pull Request。

## 许可

本项目代码仅供学习参考，未明确许可协议时保留所有权利。
