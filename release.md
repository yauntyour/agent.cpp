# agent.cpp v1.0.0：极致轻量的 C++ Agent 框架首次正式发布

## 🚀 简介

agent.cpp 是一个完全透明、高度可控的 C++ 原生 Agent 系统，具备完整的工具链支持与 WebUI。  
核心系统仅由数个 `.cpp`/`.hpp` 文件构成，内存占用极低，特别适合资源受限或边缘计算环境。

**设计理念**：语言是经验的载体。LLM 作为语言中枢服务于 Agent，Agent 再将语言转化为行动与记忆。  
二者的协同，是通向通用人工智能（AGI）的一条可行路径。

---

## ✨ 核心特性

- **极致轻量**：充分利用 C++ 零拷贝与栈上分配，运行开销趋近于零。
- **完整工具链**：通过 Python CLI 桥接，支持任意 Python 工具的原生链式调用。
- **100% 可控**：系统提示词完全开放自定义，无隐藏魔法指令。
- **轻量化扩展设计**：工具直接调用脚本，避免臃肿的技能包描述，最大限度节省 Token。
- **会话记忆系统**：支持自动/手动将对话历史摘要为记忆，并实时更新上下文。
- **记忆自动演进**：基于既有记忆与最新交互自动优化记忆内容，实现经验积累闭环。
- **WebUI 开箱即用**：提供简洁美观的 Web 操作界面。

---

## 📦 预编译包说明

本次发布为 **v1.0.0** 提供以下平台的二进制包，解压即可运行：

| 平台  | 备注 |
|:-----|:-----|
| 🐧 Linux x86_64 | 依赖 `libcurl` 和 `boost`（header-only） |
| 🪟 Windows x86_64  | 使用 MSYS2 MinGW64 编译，包含所有运行时 |
| 🍎 macOS (Intel/Apple Silicon)| 通过 Homebrew 安装依赖后运行 |

> 每个压缩包均包含可执行文件 `app`（Windows 下为 `app.exe`）、配置文件、系统目录及 WebUI 资源。

---

## 🔧 快速开始

1. **下载**对应平台的压缩包并解压。
2. 根据你的环境修改 `settings.json`（重点配置 `server_address` 和 `model`）。
3. 运行程序：

   ```bash
   ./app "12845678" #登录WebUI的密码，目前WebUI是禁用自动登录的，未来加入Session cookie可以支持登录状态
   ```

4. 打开浏览器访问 `http://localhost:8080` 即可使用 WebUI。

详细文档请参阅仓库中的 [readme.md](https://github.com/your-repo/agent.cpp/blob/main/readme.md)。

---

## 📁 包内容结构

```
.
├── app (app.exe)          # 主程序
├── webui.html             # WebUI 前端
├── agent.txt              # 系统提示词（可自定义）
├── settings.json          # 配置文件
├── sys/                   # 系统指令与调度核心
├── tools/                 # 自定义 Python 工具目录
├── sessions/              # 会话记录
├── memorys/               # 记忆持久化
└── assets/                # 静态资源
```

---

## 📌 注意事项

- **首次运行**会自动创建 `sessions/`、`memorys/`、`assets/` 空目录，请保持程序有写入权限。
- 若使用 **llama.cpp** 等本地模型服务，建议将 `settings.json` 中的 `stream` 设为 `false`，避免 JSON 解析异常。
- 自定义工具请参考 `tools/` 目录下的约定，详细说明见 `readme.md`。

---

## 🙏 致谢

本项目遵循 Apache-2.0 协议。若认可我们的工作，欢迎点亮 Star ⭐，这对我们是极大的鼓励

---
