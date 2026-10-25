# agent.cpp — 陪伴型智能体（Cosplay Agent）

一个住在桌面里的角色。不是「助手」，是「人」：有名字、有情绪、有声音、会记得你说过的话，安静太久会先开口。

内核是 [cell.cpp](https://github.com/yauntyour/cell.cpp) 单文件智能体库，外壳沿用 [cell-app](https://github.com/yauntyour/cell-app) 的组织与打包方式：一个 WebView2 窗口 + 一个只绑回环的本机服务，界面、API、脚本驱动的是同一套协议。

![对话](assets/shots/chat.png)

| 空会话 | 记忆 | 设置 |
| --- | --- | --- |
| ![欢迎](assets/shots/welcome.png) | ![记忆](assets/shots/memory.png) | ![设置](assets/shots/settings.png) |

---

## 它是什么，不是什么

**是**：一张角色卡 = 一个独立的 cell::engine 上下文（独立会话、独立工作线程、独立人设与记忆）。切角色等于换一个人，不是换一个 prompt。

**不是**：工具型助手。`tools` 永久关闭、沙箱 `read-only`、审批通道直接拒绝 —— 陪伴型智能体不需要在你机器上执行命令，它需要的是记得住、说得像、有反应。

由此带来的三个设计约束，贯穿整份代码：

1. **演出优先**。模型输出不是最终产物，是「剧本」：情绪标记驱动表情与语音音色，星号里的动作只演不念，正文按句切给 TTS。
2. **服务可插拔**。TTS / ASR 各是一份配置（`openai` / `http` / `command` / `off`），界面里改完即时生效，不重新编译。
3. **传输层无特权**。桌面窗口和浏览器打开的是同一个页面、同一条 HTTP + SSE 通道，没有「只有窗口能调」的私有接口。

---

## 分层

```
cell.cpp/cell.cpp        单文件智能体内核（main 改名后整份编进来）
        │
src/agent-core.hpp       角色内核：角色卡 / 回合管线 / 长期记忆 / 命令总线 / 事件总线
src/agent-voice.hpp      服务层：TTS、ASR、宿主侧录音（MCI，浏览器采集失败时的兜底）
src/agent-http.hpp       本机服务：页面、命令、SSE、音频与头像
src/agent-ui.hpp         内嵌页面（build_ui.py 生成的 raw string）
src/agent.cpp            入口：窗口、托盘、单实例、命令行
src/ui/src/**            页面源码（css / js 分层，按文件名顺序拼接）
```

| 文件 | 职责 | 关键点 |
| --- | --- | --- |
| `src/agent.cpp` | 进程外壳 | WebView2 窗口、托盘、单实例互斥、`--serve/--headless/--ui/--smoke` |
| `src/agent-core.hpp` | 角色内核 | `persona` / `persona_ctx` / `turn_pipeline` / `app_core` |
| `src/agent-voice.hpp` | 语音服务 | 四种 `kind`，全部走 libcurl + `cell::net::ca_bundle()`；宿主录音是兜底 |
| `src/agent-http.hpp` | 本机服务 | `servic.hpp` + `router`，SSE 每条事件一个 `data:` 行 |
| `src/agent-ui.hpp` | 内嵌页面 | 生成物，不进版本库手工改 |
| `build_ui.py` | 装配器 | 骨架 + css/* + js/* → 单页 HTML + C++ 字符串 |

### 一次对话怎么走完

```
页面 POST /api/cmd/send
        │
        ▼
app_core::exec_command ──► persona_ctx::queue_turn     （命令总线在 HTTP 线程上串行）
        │
        ▼  每个角色一条工作线程
run_turn ──► cell::engine::runner::run_turn
        │        │  on_answer 流式增量
        │        ▼
        │   marker_stream    [[emo:…]] / [[act:…]] / [[remember]]…[[/remember]]
        │        │
        │        ├─► answer 事件 ──► SSE ──► 页面逐字渲染
        │        ├─► emo 事件    ──► 头像表情 / 语音音色
        │        └─► act 事件    ──► 舞台上的动作字幕
        │        ▼
        │   sentence_splitter  按 。！？…；切句，洗掉 Markdown 与星号
        │        ▼
        │   TTS 队列 ──► 语音工作线程 ──► voice::tts ──► /api/audio/<file>
        ▼
    on_turn_end ──► 记忆落盘（memory.jsonl）+ 正文改写（标记不进入下一轮上下文）
```

**线程模型**：HTTP 工作线程负责命令（`cmd_mx` 串行化）；每个角色一条回合线程；一条 TTS 线程；一条主动关怀的 ticker。事件走 `event_bus`，每个 SSE 订阅者各自排队，慢客户端不会阻塞引擎。

---

## 打包方式（照 cell-app 抄的，抄得比较彻底）

| 环节 | cell-app | agent.cpp |
| --- | --- | --- |
| 构建系统 | CMake + Ninja，`build/` 落在源码树里 | 同 |
| Windows 工具链 | MSYS2 UCRT64（MinGW），非 MSVC | 同 |
| 链接 | `-static`，只留系统 DLL 导入 | 同（额外 `winmm` 给兜底的 MCI 录音） |
| 窗口 | `webview.h`（WebView2 / WKWebView / WebKitGTK） | 同 |
| 页面 | `build_ui.py` 装配 → `src/*-ui.hpp` raw string | 同 |
| 图标 | `windres` 编 `assets/icon.rc`（`icon.ico` → 资源 id 1） | 同 |
| 服务 | 只绑 `127.0.0.1`，校验 Host / Origin | 同（另加 `Sec-Fetch-Site`） |
| 冒烟 | `--smoke` 隔离数据根，自动退出 | 同（多一档 `--smoke-engine`） |

几个值得单独说的点：

- **WebView2 运行时不用打包**。它随系统 Edge 存在；`webview.h` 的构造只做一次性初始化。所以产物是一个 20MB 左右的 exe，没有第三方 DLL。
- **静态 libcurl 的 CA 问题**。静态 curl 自带的 CA 路径是 POSIX 路径，Windows 上必然 `curl 60`。`cell::net::ca_bundle()` 走系统 ROOT 证书库导出，链接时需要 `crypt32`。
- **页面内嵌 vs 磁盘**。`build_ui.py` 只在内容变化时写盘，`src/agent-ui.hpp` 被声明成 CMake `OUTPUT`，所以改一行 css 再 `cmake --build` 就会重新编译；想更快就用 `--ui` 直接从磁盘读页面，改完刷新，不重新编译。
- **窗口关闭 ≠ 退出**。`webview.h` 在 `WM_CLOSE` 时销毁窗口，所以换掉窗口过程只把窗口藏起来，图标挂在自己的隐藏窗口上（`assets/icon.rc` 的图标被 `LoadImageW` 取出来）；托盘菜单里有「显示主窗口 / 打开数据目录 / 退出」。
- **webview.h 有一处本地改动**（麦克风权限放行，见下面「麦克风」一节）。它是 vendored 的单头文件，升级时注意别覆盖掉。
- **单实例**。`Local\agent-cpp-desktop` 互斥体；重复启动只是把已有窗口拉到前台（藏在托盘里也能拉出来）。

### 构建

```bash
# Windows：MSYS2 UCRT64 shell 里
pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,ninja,pkgconf,curl,libsodium,nlohmann-json,boost}
cmake -B build -G Ninja
cmake --build build          # → build/agent.exe
```

```bash
# Linux：GTK3 + WebKitGTK
sudo apt install build-essential cmake ninja-build pkg-config \
  libgtk-3-dev libwebkit2gtk-4.1-dev libcurl4-openssl-dev libsodium-dev \
  nlohmann-json3-dev libboost-dev
cmake -B build && cmake --build build

# macOS：Cocoa + WKWebView
brew install cmake ninja libsodium boost nlohmann-json
cmake -B build && cmake --build build
```

CMake 选项：

| 选项 | 默认 | 作用 |
| --- | --- | --- |
| `AGENT_STATIC` | `ON` | Windows：静态链接运行时 + curl + libsodium |
| `AGENT_CONSOLE` | `OFF` | 保留控制台（调试用） |
| `AGENT_BUILD_UI` | `ON` | 构建前用 Python 重新装配页面 |
| `AGENT_CXX_STANDARD` | `26` | 编译器没有 `-std=c++26` 时改成 `23` |

> `cell.cpp` 是 submodule：`git submodule update --init --recursive`。

---

## 运行

```text
agent.exe                    桌面窗口（自动在 8899..8918 里找空闲端口起服务）
agent.exe --serve [PORT]     桌面窗口 + 固定端口（默认 8899）
agent.exe --headless [PORT]  只跑服务，不开窗口（脚本驱动 / 远程调试）
agent.exe --ui FILE          从磁盘读页面（开发循环）
agent.exe --smoke            冒烟：窗口 → 页面 → 第一条 state 命令
agent.exe --smoke-engine     冒烟：再加一次完整回合
```

开发页面：

```bash
python build_ui.py --html                    # 只装配 src/ui/index.html
build/agent.exe --headless 8787 --ui src/ui/index.html
# 浏览器打开 http://127.0.0.1:8787/ ，改 css/js，刷新即可
```

数据根固定在 **exe 同级的 `.agent/`**（`--smoke` 时用临时目录，互不污染）：

```text
.agent/
├── personas/<id>/
│   ├── persona.json      角色卡：名字、副标题、开场白、人设、称呼、音色、主题色
│   ├── memory.jsonl      长期记忆，一行一条 {t,text}
│   └── avatar.png        头像（没有就用界面生成的渐变脸）
├── sessions/             cell.cpp 的会话存储（每个角色一个 cwd key）
├── audio/                TTS 成品（保留两天）
├── mic/                  录音与上传的音频（保留两天）
├── config.json           模型供应商与当前模型（cell.cpp 的配置）
├── agent.json            语音服务、界面偏好、上次会话
├── .crypt / .key         加密库：所有 API Key 只在这里
└── logs/cell.log
```

---

## 接入服务

「服务」在这个项目里是一等公民。TTS 与 ASR 各自是一份可替换的配置，**四种形态**覆盖了绝大多数接法：

| `kind` | 适用 | 关键字段 |
| --- | --- | --- |
| `openai` | 任何 OpenAI 兼容端点：OpenAI、Azure 兼容层、faster-whisper-server、GPT-SoVITS 的 OpenAI 兼容层、本地 llama.cpp 系 TTS | `base`（写到 `/v1`）、`path`、`model`、`voice`、`format` |
| `http` | 自有 JSON 协议（GPT-SoVITS `api_v2` 之类） | `url` + `body` 模板，`{text}` 会被 JSON 转义后填入，另可用 `{voice} {model} {lang}` |
| `command` | 本地进程：`piper`、`bert-vits2`、`whisper-cli`、任何 Python 脚本 | `cmd` 里的 `{in} {out}` 占位符；TTS 正文走文本文件，不放命令行 |
| `off` | 关闭 | — |

界面里：**设置 → 语音合成 / 语音识别**，选形态、填字段、点「保存」，再点「试一下」—— TTS 会合成一句「你好，我在。」并直接播放，ASR 会拿最近一条录音转写给你看。

几个实际接过的例子：

```jsonc
// 本地 faster-whisper-server（OpenAI 兼容）
{ "kind": "openai", "base": "http://127.0.0.1:9899/v1", "model": "whisper-1", "language": "zh" }

// GPT-SoVITS api_v2（JSON 模板）
{ "kind": "http", "url": "http://127.0.0.1:9880/tts",
  "body": "{\"text\":\"{text}\",\"text_lang\":\"zh\",\"ref_audio_path\":\"a.wav\",\"prompt_text\":\"…\"}",
  "format": "wav" }

// whisper.cpp
{ "kind": "command", "cmd": "whisper-cli -m ggml-base.bin -f {in} -otxt -of {out}" }

// piper
{ "kind": "command", "cmd": "piper --model zh_CN-huayan-medium.onnx --output_file {out} < {in}" }
```

### 再加一种服务

`voice::service` 就是「一份 HTTP / 进程调用配置」，新增一类服务不需要动传输层：

1. 在 `agent-voice.hpp` 里加一个函数（照 `tts()` / `asr()` 的样子：`kind` 分支 → `detail::post_json` / `post_audio` / `spawn_cmd`）；
2. 在 `app_settings` 里加一个 `voice::service` 字段，`load/save` 各加一行；
3. 在 `exec_command` 里加 `service.set` / `service.test` 的分支（`which == "tts" | "asr"` 的写法可以直接照抄）；
4. 页面侧 `serviceSection(which, svc)` 是通用的，`openSettings()` 里再挂一节即可。

密钥永远不落配置文件：`service.set` 收到 `key` 就写进 `cell::encrypt::crypt`（`.agent/.crypt`），`agent.json` 里只留 `has_key`。

### 麦克风：页面自己采集

![录音中](assets/shots/mic.png)

录音不走宿主，走 Web 平台：

```
getUserMedia(16k 单声道 + 回声消除/降噪/AGC)
   → MediaRecorder（webm/opus，64kbps，每 200ms 一片）
   → stop() → Blob → base64 → POST /api/cmd/voice.transcribe {data_b64, ext}
   → 服务端 asr 事件 → 页面填框（或按偏好直接发送）
```

WebView2 就是 Chromium，`http://127.0.0.1:8899` 属于可信来源（`isSecureContext === true`），`getUserMedia` 本身没有障碍。**唯一的坑是权限**：`webview.h` 的 `PermissionRequested` 只对剪贴板读取调 `put_State(ALLOW)`，麦克风留在 `COREWEBVIEW2_PERMISSION_STATE_DEFAULT` —— 而无边框 WebView2 不会弹出可点的权限框，请求就一直 pending（实测 6 秒不 settle，既不 resolve 也不 reject）。

所以 `third_party/include/webview/webview.h` 有一处**本地改动**：麦克风请求按 `get_Uri` 判断，来源是回环地址才 `ALLOW`，其余（比如点链接跳出去之后的页面）一律 `DENY`。升级上游 webview.h 时记得保留这段。

- **实时电平**：`AudioContext` + `AnalyserNode` 算 RMS，写进 `--mic-lvl` 这个 CSS 变量，麦克风按钮的光晕跟着说话大小呼吸。
- **兜底**：浏览器通道拿不到麦克风（旧运行时、没有设备、权限被拒）时自动退回宿主的 `voice.record.start`（Windows MCI），提示会说明切换了来源；两条都不通就直说原因，并指向「上传音频转写」。
- **浏览器里打开**：`--headless 8787 --ui src/ui/index.html` 时页面跑在真浏览器里，权限框由浏览器自己弹，行为和普通网页一致。


---

## 命令总线

所有操作都是 `POST /api/cmd/<name>`，JSON 进 JSON 出，返回 `{"ok":true,...}` 或 `{"ok":false,"error":"中文原因"}`。

| 分组 | 命令 |
| --- | --- |
| 状态 | `state` |
| 角色 | `personas.list` `persona.select` `persona.save` `persona.delete` `persona.avatar` |
| 对话 | `send` `stop` |
| 会话 | `session.new` `sessions.list` `session.select` `session.delete` |
| 记忆 | `memory.list` `memory.add` `memory.forget` `memory.clear` |
| 语音 | `voice.record.start` `voice.record.stop`（宿主录音，兜底） `voice.transcribe` `voice.speak` `voice.stop` |
| 服务 | `service.set` `service.test` |
| 模型 | `providers.add` `providers.rm` `providers.select` `provider.set_key` `models.fetch` `model.set` `think.set` |
| 杂项 | `ui.pref` `open.data_dir` `app.quit` |

```bash
# 用脚本跟她说话（host 头必须是回环地址，服务只认自己）
curl -s -X POST http://127.0.0.1:8899/api/cmd/send \
     -H 'Host: 127.0.0.1:8899' -d '{"text":"我回来了"}'

curl -sN http://127.0.0.1:8899/api/events    # SSE：一条事件一行 data:
```

| 接口 | 说明 |
| --- | --- |
| `GET /` | 页面（内嵌，或 `--ui` 指定的磁盘文件） |
| `GET /api/state` | 状态快照（角色、历史、记忆、服务、模型） |
| `GET /api/events` | SSE 事件流 |
| `GET /api/audio/<file>` | TTS 成品（文件名白名单，防目录穿越） |
| `GET /api/avatar/<id>` | 角色头像 |
| `POST /api/cmd/<name>` | 命令总线 |

事件：`hello` `turn_begin` `answer_begin` `answer` `reason` `status` `progress` `error` `emo` `act` `round` `turn_end` `voice` `voice_error` `voice_stop` `memory` `asr` `voice_state` `quit`。除 `hello` 外每条都带 `persona`，页面按角色分流。

---

## 演出协议

系统提示里追加的约定（`compose_prompt`）：

| 标记 | 含义 | 去向 |
| --- | --- | --- |
| `[[emo:happy]]` | 情绪：`neutral / happy / sad / angry / shy / surprised / thinking / affection` | 头像表情 + 语音音色 + 徽标 |
| `*轻轻抱住你*` | 动作 / 神态 | 舞台字幕，**不朗读** |
| `[[remember]]一句话事实[[/remember]]` | 值得长期记住的事 | `memory.jsonl`，下一轮进入 system prompt |
| `[[idle]]`（用户侧注入） | 主动开口的时刻 | 触发一次「她先说话」 |

流式输出会把标记切碎，所以 `marker_stream` 做增量解析：遇到可能的标记前缀就按住不放，确定不是标记再原样吐出来。回合结束时正文会被改写成「干净版」，标记不进入下一轮上下文，刷新页面也看不到。

---

## 冒烟测试

不需要任何模型，数据根隔离在临时目录，跑完自动退出：

```bash
build/agent.exe --smoke          # 期望输出 SMOKE_UI_OK
build/agent.exe --smoke-engine   # 期望输出 SMOKE_UI_OK (probing engine) + SMOKE_ENGINE_OK
```

`--smoke` 验证的是「窗口起来了 → 页面加载了 → 页面发出了第一条 state 命令」；`--smoke-engine` 再多走一个完整回合（没有模型时走错误路径，`turn_end` 仍然会到）。

---

## 边界

- **录音在页面里做**（`getUserMedia` + `MediaRecorder`），三个平台一条路；宿主的 MCI 录音只是 Windows 上的兜底，正常流程不会走到它。
- **不做工具调用**。这是取舍，不是缺功能：陪伴型智能体不需要在你机器上执行命令，也就不需要审批 UI。
- **服务密钥只在本机加密库里**，`agent.json` 永远只有 `has_key`。
- **服务只绑 127.0.0.1**，并校验 `Host` / `Origin` / `Sec-Fetch-Site`。想从别的机器访问，请自己套一层反向代理并加上认证 —— 默认配置下它不是网络服务。

---

## License

MIT
