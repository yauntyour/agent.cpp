# 更新内容

0. 记忆系统全面优化：
    - 合并摘要与关键词 LLM 调用为一次（JSON 格式返回），节省 50% Token 消耗
    - 增量更新：仅处理自上次保存后的新消息，避免重复处理历史
    - 自动触发：每次对话结束后检查上下文大小，超过 `max_context` 阈值时自动压缩
    - 内存索引缓存：`mem-keys` / `mem-get` 命令改为缓存读取，避免每次遍历磁盘
    - 并发安全：SessionManager 全部公共方法增加 `std::mutex` 保护
    - `"role": "memory"` 改为标准 `"role": "system"`，提升兼容性
    - 新增 `last_saved_index` 字段持久化到记忆文件，重启后恢复增量位置
1. 新增模型供应商选择功能：
    - 支持 OpenAI（标准接口）、Ollama、Llama.cpp 三种供应商
    - WebUI 聊天头部新增供应商选择器，支持运行时动态切换
    - 设置表单新增供应商下拉选择
    - 新增 `POST /api/provider` 接口，支持 API 级别供应商切换
    - `settings.json` 新增 `provider` 字段（默认 `openai`）
    - 所有供应商统一使用 `set_base_url()` / `set_api_key()` 接口
    - Ollama/Llama 不支持流式时自动回退到非流式模式
2. 新增系统状态监控（CPU/内存），支持 Windows/Linux/macOS 三平台
3. 新增 webui.log 错误日志系统，所有 HTTP 处理加入异常捕获与日志记录
4. 修复 HTTP 响应中文乱码（添加 charset=utf-8）
5. 修复用户上传图片未传入 LLM 的 Bug
6. 重构图片资产存储系统：
   - 改为 JSON 数组 + `"#n"` 位置索引
   - 使用 `std::swap` 零拷贝交换 base64 数据
   - 彻底解决资产文件随每次保存无限膨胀的问题
7. 保存会话消息时剔除系统提示词，仅保留对话消息
8. 重构工具调用输出格式：每个工具独立 `<system_output>` 块，独立 OK/ERR 状态
9. 流式 SSE 事件拆分：tool_start / tool_output / tool_end 每工具独立发送
10. WebUI 界面大改：
   - 消息气泡样式重做（半透明用户气泡、无背景 AI 气泡）
   - 工具系统输出改为可折叠块，带箭头动画与状态标签
   - 新增加载覆盖层（blur 遮罩）
   - CSS 兼容性修复与布局优化
11. 清理已废弃的 `sys_state.py`、`sys_tools.py`
12. 修复 macOS CPU 监控 `host_cpu_load_data` 返回一维数组时的索引越界
13. 会话记忆系统增强：
    - 记忆新增 `created_at` 时间戳字段，记录记忆创建时间
    - `memory_created_at` 随 session/memory 接口返回
    - 使用 `json::value()` 替代 `get<>()` 防止访问空字段崩溃
14. API 路由拆分与重构：
    - 原 `/api/input`（流式）更名为非流式打包接口 `handle_input_packed`，返回统一 JSON（含 content/thinking/tools/usage）
    - 原 `/api/input/stream` 流式路由更名为 `handle_input_streaming`，支持 token-by-token SSE + 每工具独立 tool_start/tool_output/tool_end 事件
15. image-drawer 工具输出路径修复：`./assets/` → `./workspace/assets/`
16. CI 构建频率下调：从每天构建改为每周一构建
17. WebUI 继续完善：布局、样式、交互细节持续优化（~2300 行）

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
