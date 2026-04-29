# 更新内容

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
