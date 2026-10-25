# agent.cpp — 陪伴型智能体（Cosplay Agent）

一个住在桌面里的角色。不是「助手」，是「人」：有名字、有情绪、有声音、会记得你说过的话，安静太久会先开口。

内核是 [cell.cpp](https://github.com/yauntyour/cell.cpp) 单文件智能体库，外壳沿用 [cell-app](https://github.com/yauntyour/cell-app) 的组织与打包方式：一个 WebView2 窗口 + 一个只绑回环的本机服务，界面、API、脚本驱动的是同一套协议。

![对话](assets/shots/chat.png)

| 空角色 | 角色设定 | 记忆 | 设置 |
| --- | --- | --- | --- |
| ![欢迎](assets/shots/welcome.png) | ![角色](assets/shots/persona.png) | ![记忆](assets/shots/memory.png) | ![设置](assets/shots/settings.png) |

---

## 它是什么，不是什么

**是**：一张角色卡 = 一个独立的 cell::engine 上下文（独立会话、独立工作线程、独立人设与记忆）。切角色等于换一个人，不是换一个 prompt。

**不是**：工具型助手。`tools` 永久关闭、沙箱 `read-only`、审批通道直接拒绝 —— 陪伴型智能体不需要在你机器上执行命令，它需要的是记得住、说得像、有反应。

由此带来的三个设计约束，贯穿整份代码：

1. **演出优先**。模型输出不是最终产物，是「剧本」：情绪标记驱动表情与语音音色，星号里的动作只演不念，正文按句切给 TTS。接了 Live2D 之后，`[[motion:]]` / `[[expr:]]` 还能直接指挥身体。
2. **服务可插拔**。TTS / ASR 都是可选服务，默认关闭（`off`），配了才生效；各是一份配置（`openai` / `http` / `command` / `off`），界面里改完即时生效，不重新编译。
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
| `src/agent-live2d.hpp` | 自带的默认模型 | `build_live2d.py` 用 `#embed` 打进去的字节表 |
| `build_live2d.py` | 装配器 | `assets/live2d/**` → `src/agent-live2d.hpp` |
| `src/ui/src/js/07-live2d.js` | 舞台 | 运行时加载、模型、动作 / 表情 / 口型 |
| `src/ui/src/js/08-triggers.js` | 触发器引擎 | 事件 → 动作序列，含冷却与内置默认规则 |
| `src/ui/src/js/09-stage-panel.js` | 舞台面板 | 换模型、情绪映射、规则编辑器 |
| `build_ui.py` | 装配器 | 骨架 + css/* + js/* → 单页 HTML + C++ 字符串；`@asset(路径)` 换成 data URI |

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
| 图标 | `windres` 编 `assets/icon.rc`（`icon.ico` → 资源 id 1） | 同（`assets/icon.png` 是母版，`make_icon.py` 派生） |
| 服务 | 只绑 `127.0.0.1`，校验 Host / Origin | 同（另加 `Sec-Fetch-Site`） |
| 冒烟 | `--smoke` 隔离数据根，自动退出 | 同（多一档 `--smoke-engine`） |

几个值得单独说的点：

- **WebView2 运行时不用打包**。它随系统 Edge 存在；`webview.h` 的构造只做一次性初始化。所以产物是一个 20MB 左右的 exe，没有第三方 DLL。
- **静态 libcurl 的 CA 问题**。静态 curl 自带的 CA 路径是 POSIX 路径，Windows 上必然 `curl 60`。`cell::net::ca_bundle()` 走系统 ROOT 证书库导出，链接时需要 `crypt32`。
- **页面内嵌 vs 磁盘**。`build_ui.py` 只在内容变化时写盘，`src/agent-ui.hpp` 被声明成 CMake `OUTPUT`，所以改一行 css 再 `cmake --build` 就会重新编译；想更快就用 `--ui` 直接从磁盘读页面，改完刷新，不重新编译。
- **图标只有一张母版**。`assets/icon.png` 是母版，`python assets/make_icon.py` 派生四份：`icon.ico`（16–256，exe / 窗口 / 托盘）、`icon-512.png`（发布用）、`ui-logo.png`（深色界面里的白描版）、`favicon.png`（浏览器标签）。界面里的图不是走路由，而是 `@asset(assets/ui-logo.png)` 在装配时内联成 data URI —— 单文件 exe 里也带着 logo。
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
agent.exe --debug            开发模式：空数据根时自动生成一个测试角色
```

**默认是空角色**：第一次打开没有角色卡，欢迎页上有一个「创建第一个角色」，
建角色时自动挂上自带的模型。`--debug`（以及两个冒烟模式）会在空数据根里生成一个
测试角色（不主动开口，附带两个示例触发器），免得每次调界面都要先建卡。

回合进行中，输入框右边的发送键会变成红色的「停止」（Esc 同效），点一下打断她。
语音合成与语音识别默认关闭，界面里不会出现报错刷屏；接上之后才会自动朗读、才会亮起麦克风。

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
├── live2d/
│   ├── runtime/          运行时库（点「下载运行时」取回，不随 exe 分发）
│   └── models/<任意>/     模型（自带一份默认的，第一次运行自动解出来）
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
| 舞台 | `live2d.scan` `live2d.open_dir` `live2d.fetch_runtime` |
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
| `GET /api/live2d/<…>` | 舞台资源（运行时库 + 模型文件，逐段白名单） |
| `POST /api/cmd/<name>` | 命令总线 |

事件：`hello` `turn_begin` `answer_begin` `answer` `reason` `status` `progress` `error` `emo` `act` `motion` `expr` `round` `turn_end` `voice` `voice_error` `voice_stop` `memory` `asr` `voice_state` `quit`。除 `hello` 外每条都带 `persona`，页面按角色分流。

---

## 演出协议

系统提示里追加的约定（`compose_prompt`）：

| 标记 | 含义 | 去向 |
| --- | --- | --- |
| `[[emo:happy]]` | 情绪：`neutral / happy / sad / angry / shy / surprised / thinking / affection` | 头像表情 + 语音音色 + 徽标 |
| `[[motion:TapBody]]` | 模型动作组（装了 Live2D 才有） | 画布 |
| `[[expr:f01]]` | 模型表情（装了 Live2D 才有） | 画布 |
| `*轻轻抱住你*` | 动作 / 神态 | 舞台字幕，**不朗读** |
| `[[remember]]一句话事实[[/remember]]` | 值得长期记住的事 | `memory.jsonl`，下一轮进入 system prompt |
| `[[idle]]`（用户侧注入） | 主动开口的时刻 | 触发一次「她先说话」 |

流式输出会把标记切碎，所以 `marker_stream` 做增量解析：遇到可能的标记前缀就按住不放，确定不是标记再原样吐出来。回合结束时正文会被改写成「干净版」，标记不进入下一轮上下文，刷新页面也看不到。

---

## 舞台：Live2D 与触发器

舞台右侧就是一块 Live2D 画布，情绪、动作、触发器都归它。**没有光球兜底**：模型或运行时不在时画布上给一句能点的提示，点一下直接打开舞台面板。面板入口是情绪徽标右边那个旋钮图标。

![舞台](assets/shots/stage.png)

第一次运行会把 `assets/live2d/models/` 里的默认模型解到 `.agent/live2d/models/`
（`build_live2d.py` 用 C++26 `#embed` 把它打进 exe，所以不需要额外文件）；
已经有别的模型就一个字节都不动。**运行时核心不在 exe 里**（Cubism Core 的许可不允许再分发），
点面板里的「下载运行时」取回本地即可，也可以打开「运行时走 CDN」先看效果。

```text
.agent/live2d/
├── runtime/
│   ├── manifest.json            可选：{"scripts":["pixi.min.js","live2dcubismcore.min.js","cubism4.min.js"]}
│   ├── pixi.min.js
│   ├── live2dcubismcore.min.js
│   └── cubism4.min.js
└── models/
    └── hiyori/
        ├── Hiyori.model3.json
        ├── Hiyori.moc3
        ├── Hiyori.2048/texture_00.png …
        └── motions/*.motion3.json
```

- 换模型：旋钮图标 →「扫描资源」→ 选一个 → 底部「保存并生效」。模型加载完会把动作组 / 表情名写回角色卡（`live2d.groups` / `live2d.expressions`），拼提示词时会列出来，模型才知道自己有哪些动作可用。
- 缩放 / 偏移改了即时预览，点「保存并生效」才写进角色卡；面板顶部那条一直贴底的保存条会告诉你「有未保存的改动」。
- **自定义动作模组**：`motion3.json` 放进模型目录，在 `model3.json` 的 `FileReferences.Motions` 里给它一个组名（Cubism 的标准做法）。组名就是面板里能选、触发器里能挂、提示词里能写的那个名字 —— 换一套动作不需要改一行代码。
- 资源走 `GET /api/live2d/<…>`，逐段白名单 + 规范化路径校验，只认 `.agent/live2d/` 里面的文件。

角色卡里的字段：

| 字段 | 作用 |
| --- | --- |
| `live2d.model` | 相对 `.agent/live2d/` 的路径；空 = 舞台留白 |
| `live2d.scale` `x` `y` | 缩放与偏移（偏移是画布宽/高的比例，`0.1` = 10%） |
| `live2d.emo` | 情绪 → `{expr, motion}`；`expr` 可以是序号也可以是表情名 |
| `live2d.groups` `expressions` | 加载后自动写回，供提示词引用 |

### 模型直接演

装了模型以后 `compose_prompt` 会多一段「舞台」约定，于是模型可以自己指挥身体：

| 标记 | 含义 | 去向 |
| --- | --- | --- |
| `[[motion:TapBody]]` | 播放动作组 | `motion` 事件 → 画布 |
| `[[motion:TapBody/2]]` | 指定第几个 | 同上 |
| `[[expr:f01]]` | 切表情 | `expr` 事件 → 画布 |

`[[emo:…]]` 仍然管情绪：它同时驱动语气徽标、语音音色，以及 `live2d.emo` 里的表情 / 动作映射。

### 触发器：事件 → 动作

一条规则（存在角色卡 `triggers` 数组里）：

```jsonc
{
  "id": "poke-head",          // 唯一名，冷却按它算
  "on": "click",              // 什么时候触发
  "when": { "zone": "head" }, // 条件，全部满足才算命中；留空 = 不限制
  "do": [                     // 按顺序执行
    { "a": "motion", "group": "TapHead" },
    { "a": "say", "text": "别、别摸头…" },
    { "a": "emo", "name": "shy" }
  ],
  "cd": 5,                    // 冷却秒数
  "once": false,              // 本次页面会话只触发一次
  "off": false
}
```

**事件**（`on`）：`click` 舞台点击 · `emotion` 情绪变化 · `act` 星号动作 · `motion` 模型动作 · `expr` 模型表情 · `reply` 回复正文命中文字 · `user` 你发出的消息 · `idle` 安静太久 · `turn_begin` / `turn_end` · `boot` 舞台就绪。

**条件**（`when`）：`zone`（点击区域，模型声明了 HitAreas 时是区域名，否则 `head` / `body` / `hand`）· `emotion` · `act` · `group` · `expr` · `text`（子串或 `/正则/`）· `not_text` · `min_idle`（安静多少秒才触发）。字符串或数组都行。

**动作**（`do`）：

| `a` | 参数 | 作用 |
| --- | --- | --- |
| `motion` | `group` `index` | 播放动作组 |
| `expr` | `id` | 切表情（序号或名字） |
| `param` | `id` `to` `ms` | 补间一个模型参数，例如 `ParamAngleZ` |
| `emo` | `name` | 走一遍情绪链路（语气徽标 + 语音音色 + 映射表情） |
| `say` | `text` `ms` | 舞台台词气泡，不朗读 |
| `speak` | `text` | 交给 TTS 念出来 |
| `send` | `text` | **交回回合管线，她真的会回你** |
| `idle` | — | 让她主动开口一次 |
| `fx` | `name` `ms` | 画面效果：`pulse` / `shake` / `flash` / `dim` |
| `sound` | `url` `volume` | 放一个音效 |
| `toast` | `text` | 右下角提示 |
| `wait` | `ms` | 等一会儿（串起多段演出） |

两个方向都是通的：

```text
AI 回复 ──[[emo:]] / [[act:]] / [[motion:]]──► SSE ──► 触发器 ──► 动作 / 表情 / 台词
你戳一下 ──► 画布命中区域 ──► 触发器 ──► send ──► /api/cmd/send ──► 她回你一句
```

- 触发器跑在页面里，因为点击、口型、冷却本来就是界面状态；规则存在角色卡里，**换角色 = 换一套演出**。
- 角色卡里还没有触发器时用内置默认（摸头会躲、戳手臂会回话、开心换姿势、安静久了动一下、说到晚安就困）。改任意一条就会写进角色卡；面板里可以「恢复内置默认」。
- 同一次触发里最多只放行一个 `send` / `idle`，避免一条规则组把好几轮对话同时点着。
- 面板的规则编辑器是表单式的（事件 → 条件 → 动作列表），右上角「JSON 编辑」是逃生舱口：整份数组直接改，保存前做语法检查。

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
