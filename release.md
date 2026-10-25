# 版本更新日志

## 2.1.0

目标：**发行物只有 bin/agentcpp.exe 一个文件**，运行期才落 `.agent/`。

### 新增

- **系统托盘**：常驻通知区图标，左键显示窗口、右键菜单（显示主窗口 / 打开数据目录 / 退出）；
  关闭窗口默认收进托盘继续跑（`window.close_to_tray` 可关，或启动加 `--no-tray`），
  托盘「退出」才会结束进程；重复双击 exe 会把托盘里的窗口拉回前台。
- **首次启动自举**：提示词、`sys/cs.txt`、工具脚本（image-drawer / mcp-linker / playwright-tools）
  与初始 JSON 全部编译进 exe，`config::ensure_layout()` 按需写进 `.agent/workspace/`，
  已存在的文件不覆盖。

### 变更

- **单文件发行**：WebUI、默认工作区、`WebView2Loader.dll` 全部内嵌（`tools/embed_assets.py`
  → 三个生成头文件）；加载器首次启动释放到 `<agent_root>/runtime/` 再 `LoadLibrary`，
  exe 旁边不再需要任何 DLL。
- **界面零外链**：Font Awesome（本地 woff2 字体）、marked 内置到 `src/templates/webui/vendor/`，
  axios 换成 20 行 `fetch` 封装，Tailwind 的零散工具类改成项目自有 CSS —— 断网也能完整渲染。
- **仓库瘦身**：删除根目录的 `config/`、`settings.example.json`、`build/`、`install/`、
  `WebView2Loader.dll` 与 `agentcpp.exe`；模板移到 `src/templates/`（只作为编译输入），
  构建产物统一进 `bin/`（已 gitignore）。
- 默认配置新增 `window.close_to_tray`；`server.host` 默认 `127.0.0.1`。

## 2.0.0

本版是一次结构性重构：把「单头文件 + 单入口」拆成分层模块，运行期数据统一收敛到
`<exeDir>/.agent`，WebUI 摆脱单文件限制，频道变成可注册的驱动。

### 破坏性变更

| 项 | 1.x | 2.0 |
| :--- | :--- | :--- |
| 配置文件 | `<exeDir>/settings.json` | `<exeDir>/.agent/settings.json` |
| 工作区 | `<exeDir>/workspace/` | `<exeDir>/.agent/workspace/` |
| WebUI | 固定 `<workspace>/webui.html` | `webui.path` 可配置，支持文件或目录 |
| 日志 | `<workspace>/webui.log` | `<exeDir>/.agent/logs/agent.log`（4MB 滚动） |
| 构建依赖 | Boost.Asio + `servic.cpp` 子模块 | 仅 libcurl + libsodium + nlohmann_json |
| 源码布局 | `agent.hpp` + `app.cpp` | `src/{core,net,agent,channels,server}` |

**自动迁移**：把新 exe 放进旧目录启动即可。检测到 `.agent/` 不存在而 `settings.json` /
`workspace/` 存在于 exe 同级时，会自动复制到 `.agent/`（原文件保留），并把配置里的
`workspace` 改写为相对路径、`workspace/webui.html` 搬到 `.agent/webui/index.html`。

### 新增

- **数据目录**：`<exeDir>/.agent` 统一存放配置、WebUI、日志与工作区；`--root` 可整体改道，
  任意目录启动都能找到自己的数据。
- **自研 HTTP 服务端**（`src/server/http_server`）：路由支持精确路径 / 路径参数 / 前缀 / 兜底，
  原生支持 SSE 流式写，连接并发上限保护。移除了 Boost 与 `servic.cpp` 子模块依赖。
- **WebUI 多文件**：`webui.path` 指向目录时按静态站点服务，自动 MIME、`ETag` + `304`、
  路径穿越防护；默认资源已拆成 `index.html` / `app.css` / `app.js`。
- **频道驱动注册表**：`channels::register_driver()` 注册新平台；`type` 缺省时按旧 `path` 推断，
  老配置无需改动。
- **QQ 频道（OneBot v11）**：反向 HTTP 上报 + HTTP API 发送，适配 NapCat / Lagrange / go-cqhttp；
  支持私聊、群聊、群内仅 @ 响应、图片收发。
- **统一 Agent 主循环**：`agent::run_turn()` 一份实现同时服务 WebUI（SSE）与所有频道，
  消除了原先「流式一份、频道一份」的重复代码。
- **诊断接口**：`GET /api/webui`（WebUI 解析结果与文件清单）、`GET /api/channels/drivers`；
  `/api/logs` 已移除，日志只落 `<exeDir>/.agent/logs/agent.log`。
- **CLI**：`--root` / `--host` / `--help` / `--version`。

### 修复

- 配置里 `webui.html` 缺失会导致启动失败 → 现在目录/文件两种模式都会在首次运行自动播种。
- 记账 `data.json` 中会话键为空值时 `json::value()` 抛异常导致对话 500。
- 全局 `DataManager` 未初始化时析构会写空路径并 `std::terminate`。
- 上下文里出现多条 `system` 消息时，llama.cpp 等严格实现直接 400
  （`System message must be at the beginning`）→ 工具清单与记忆摘要合并进首条系统提示。
- `/api/fs/list` 使用相对 `workspace` 拼绝对路径，切到 `.agent` 布局后会越界误判；
  改为统一走 `config::workspace_dir()`，并用规范化的边界判断。
- `/api/settings` 无请求体时返回 `Settings saved failed.` → 现在 GET 直接返回当前配置。
- 流式请求失败时静默返回空回复 → 现在会把 curl 错误写入日志。

### 构建

```bash
# 快速本地构建（g++ 直接编译）
python build.py -r

# CMake：产出全静态、零依赖二进制 + .agent 模板
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix install
```

> MinGW 提示：`-O0` 会让 GCC 为每个 inline 函数保留一份 COMDAT 节，单个目标文件节数可达 7000+，
> 触发 `ld` 静默失败（`ld returned 5 exit status`）。本项目调试构建固定使用 `-O1 -g`。

### 移除

- `agent.hpp`、`app.cpp`（内容已按模块迁入 `src/`）
- `servic.cpp` git 子模块（HTTP 服务端改为自带实现；`tiny_sha.h` 已内联到 `src/core/`）
- 仓库内的 `workspace/` 目录（模板移到 `config/workspace/`，运行期数据在 `.agent/`）

## 2.0.1

### 新增

- **WebView2 桌面外壳**（`src/desktop/webview2`）：Windows 默认拉起原生窗口渲染 WebUI，
  双击 exe 即用；SDK 头随仓库、运行期动态加载 `WebView2Loader.dll`，不需要导入库，
  MinGW / MSVC 都能编。`--gui` / `--no-gui` / `--width` / `--height` 可控制，
  关窗口即优雅退出；没有 WebView2 运行时时自动退化为系统浏览器。
- **WebUI 内嵌为 C++ 头文件**：`tools/embed_webui.py` 把 `config/webui` 合成为
  `src/server/webui_assets.hpp`（base64 分片 + MIME + 大小），CMake 构建前自动执行，
  单个 exe 不带任何外部文件也能提供完整界面。新增 `webui.source`：
  `embedded`（默认）/ `auto` / `disk`。
- **明暗主题重设计**：统一设计令牌（四层背景、语义色 ok/warn/err/info、accent 渐变第二色、
  ring 焦点环、scrollbar、overlay），组件与 JS 内联样式只引用变量；浅色主题对比度达到
  WCAG AA；新增 `:focus-visible` 焦点环、主题切换过渡、`?theme=light|dark` 直指定主题。
- **设置面板补齐**：新增「存储与界面」分组，可改 `root`、`server.host`、`server.port`、
  `webui.path`、`webui.index`、`webui.source`。
- `GET /api/webui` 增加 `source` / `embedded_files`；静态响应增加 `X-WebUI-Source` 头。

### 修复

- **WebUI 保存配置会抹掉未展示的字段**（`webui` / `server` / `root` 直接消失）：
  后端改为深合并（`config::apply`），前端改为「深拷贝当前配置 + 覆盖表单字段」。
- 嵌套配置读取只查顶层键，导致 `webui.source` 永远取不到值：新增 `config::str_path("a.b")`。
- 内嵌资源 base64 分片逐块编码会在每块补 `=`，拼接后解码失败：改为整体编码后再切分。
