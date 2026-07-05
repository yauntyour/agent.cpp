# 更新内容

0. 新增模型供应商选择功能：
    - 支持 OpenAI（标准接口）、Ollama、Llama.cpp 三种供应商
    - WebUI 聊天头部新增供应商选择器，支持运行时动态切换
    - 设置表单新增供应商下拉选择
    - 新增 `POST /api/provider` 接口，支持 API 级别供应商切换
    - `settings.json` 新增 `provider` 字段（默认 `openai`）
    - 所有供应商统一使用 `set_base_url()` / `set_api_key()` 接口
    - Ollama/Llama 不支持流式时自动回退到非流式模式
1. 新增系统状态监控（CPU/内存），支持 Windows/Linux/macOS 三平台
2. 新增 webui.log 错误日志系统，所有 HTTP 处理加入异常捕获与日志记录
3. 修复 HTTP 响应中文乱码（添加 charset=utf-8）
4. 修复用户上传图片未传入 LLM 的 Bug
5. 重构图片资产存储系统：
   - 改为 JSON 数组 + `"#n"` 位置索引
   - 使用 `std::swap` 零拷贝交换 base64 数据
   - 彻底解决资产文件随每次保存无限膨胀的问题
6. 保存会话消息时剔除系统提示词，仅保留对话消息
7. 重构工具调用输出格式：每个工具独立 `<system_output>` 块，独立 OK/ERR 状态
8. 流式 SSE 事件拆分：tool_start / tool_output / tool_end 每工具独立发送
9. WebUI 界面大改：
   - 消息气泡样式重做（半透明用户气泡、无背景 AI 气泡）
   - 工具系统输出改为可折叠块，带箭头动画与状态标签
   - 新增加载覆盖层（blur 遮罩）
   - CSS 兼容性修复与布局优化
10. 清理已废弃的 `sys_state.py`、`sys_tools.py`
11. 修复 macOS CPU 监控 `host_cpu_load_data` 返回一维数组时的索引越界
12. 会话记忆系统增强：
    - 记忆新增 `created_at` 时间戳字段，记录记忆创建时间
    - `memory_created_at` 随 session/memory 接口返回
    - 使用 `json::value()` 替代 `get<>()` 防止访问空字段崩溃
13. API 路由拆分与重构：
    - 原 `/api/input`（流式）更名为非流式打包接口 `handle_input_packed`，返回统一 JSON（含 content/thinking/tools/usage）
    - 原 `/api/input/stream` 流式路由更名为 `handle_input_streaming`，支持 token-by-token SSE + 每工具独立 tool_start/tool_output/tool_end 事件
14. image-drawer 工具输出路径修复：`./assets/` → `./workspace/assets/`
15. CI 构建频率下调：从每天构建改为每周一构建
16. WebUI 继续完善：布局、样式、交互细节持续优化（~2300 行）

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
