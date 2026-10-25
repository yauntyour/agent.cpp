# agent.cpp REST / SSE 接口

服务默认监听 `http://0.0.0.0:8080`（可用 `--host` / `--port` 或配置 `server.*` 覆盖）。
所有响应带 CORS 头，`OPTIONS` 预检统一返回 `204`。

- 请求体：`application/json`（UTF-8）
- 响应：`application/json`，文本类自动带 `charset=utf-8`
- 连接：一请求一连接（`Connection: close`），SSE 除外
- 认证：当前版本未启用接口级鉴权，**请勿直接暴露到公网**；WebUI 口令在进程启动时校验

路由支持三种匹配：精确路径、`/api/todos/:id` 形式的路径参数、`/api/channel/qq/` 形式的前缀。
未命中的路径交给 WebUI 静态资源；`/api/*` 未命中返回 404 JSON。

---

## 1. 对话

### POST /api/input

非流式对话，返回完整回复与统计。

```json
// 请求
{
  "messages": "你好",
  "images": ["data:image/jpeg;base64,..."],   // 可选
  "model": "default",                          // 可选，"default" 用配置里的 model
  "think": false,                              // 可选
  "session_id": "1792726719",                  // 可选，不传则用当前会话
  "channel": "Telegram"                        // 可选，频道专用会话（与 session_id 互斥）
}
```

```json
// 响应
{
  "content": "你好！",
  "thinking": ["..."],
  "tools": [{ "name": "exec", "args": "ls", "output": "...", "round": 0 }],
  "rounds": 2,
  "usage": { "prompt_cost": 1555, "completion_cost": 1, "total_cost": 1556 }
}
```

### POST /api/input/stream（SSE）

请求体同上，响应为 `text/event-stream`，每帧 `data: <json>\n\n`。

| 事件 `type` | 字段 | 说明 |
| :--- | :--- | :--- |
| `token` | `content` | 正文增量 |
| `thinking` | `content` | 推理增量 |
| `tool_start` | `name`, `args`, `round` | 开始执行工具 |
| `tool_output` | `content`, `round` | 工具输出（已包裹 `<system_output>`） |
| `tool_end` | `name`, `round` | 工具结束 |
| `permission_request` | `id`, `session_id`, `tool`, `args`, `timeout` | 需要授权，前端应弹窗 |
| `permission_result` | `id`, `granted`, `remember`, `timed_out` | 授权结果 |
| `done` | `usage`, `rounds` | 本轮结束 |
| `error` | `message` | 出错 |

示例：

```
data: {"type":"token","content":"你"}

data: {"type":"token","content":"好"}

data: {"type":"done","usage":{"prompt_cost":1555,"completion_cost":1,"total_cost":1556},"rounds":1}
```

> 授权等待期间，`POST /api/permission/respond` 由其他工作线程并发处理，不会阻塞服务。

---

## 2. 会话

| 方法 | 路径 | 说明 |
| :--- | :--- | :--- |
| GET | `/api/session` | 会话列表 `{"session_list":["..."]}` |
| GET | `/api/session/new` | 新建会话，返回 `{"status":"OK","session_id":"..."}` |
| GET | `/api/session/msg` | 当前会话消息（含恢复后的图片） |
| POST | `/api/session/delete` | `{"session_id":"..."}`，频道会话不可删 |
| GET | `/api/session/memory` | 读取当前会话记忆摘要 |
| POST | `/api/session/memory` | 立即生成 / 增量更新记忆摘要 |
| GET | `/api/session/clear` | 清空当前会话上下文 |

---

## 3. 供应商

| 方法 | 路径 | 说明 |
| :--- | :--- | :--- |
| GET | `/api/models` | 当前供应商可用模型 `{"data":[{"id":"...","status":{...}}]}` |
| GET | `/api/providers` | `{"current_provider":"...","providers":[...]}` |
| POST | `/api/providers/add` | 新增供应商 |
| POST | `/api/providers/update` | 修改供应商 |
| POST | `/api/providers/delete` | 删除供应商 |
| POST | `/api/provider` | 切换供应商（按 id，或 type + 地址） |
| POST | `/api/provider/key` | 加密保存 API Key 到 `tokens/providers/<id>.enc` |
| GET | `/api/provider/key/:id` | 查询 API Key 是否已配置 |

供应商条目：

```json
{ "id": "openai-default", "name": "OpenAI", "type": "openai",
  "server_address": "http://localhost:11434", "has_key": false }
```

`type` 取值：`openai`（标准 OpenAI 兼容）、`ollama`、`llama`（llama.cpp）。

---

## 4. 频道

| 方法 | 路径 | 说明 |
| :--- | :--- | :--- |
| GET | `/api/channels` | `settings.channels` 原样返回 |
| GET | `/api/channels/drivers` | 已注册的驱动 `{"drivers":[{"type","display","hint","config","needs_token"}]}` |
| GET | `/api/channel/status` | 运行状态 `[{"name","running","state","detail","last_error"}]` |
| GET | `/api/channel/qr/:name` | 登录二维码 `{"state","qr_svg","qr_url"}`（微信扫码用） |
| POST | `/api/channels/add` | `{"name","type","status","config"}` 新建频道（频道名即身份，不可改名） |
| POST | `/api/channels/update` | `{"name","type?","status?","config?"}` 修改并即时启停 |
| POST | `/api/channels/delete` | `{"name"}` 删除频道（运行中的 worker 会被停止） |
| POST | `/api/channel/start` | `{"name":"QQ"}` 运行时拉起（要求 `status=active`） |
| POST | `/api/channel/stop` | `{"name"}` 请求停止（长轮询需等当前请求返回） |
| POST | `/api/channel/token` | `{"name":"Telegram","token":"..."}` 加密保存 |
| GET | `/api/channel/token/:name` | 查询 Token 是否已配置 |

### POST /api/channel/qq/event —— OneBot v11 事件上报

也可用 `/api/channel/qq/<频道名>/event` 明确指定频道。

```json
{
  "post_type": "message",
  "message_type": "private",     // private | group
  "user_id": 20002,
  "group_id": 30003,             // group 时必填
  "self_id": 10001,
  "message": [
    { "type": "text",  "data": { "text": "你好" } },
    { "type": "at",    "data": { "qq": "10001" } },
    { "type": "image", "data": { "url": "https://..." } }
  ],
  "sender": { "nickname": "tester" }
}
```

- `post_type=meta_event`（心跳 / 生命周期）只用于刷新状态
- 群聊默认要求 `@Bot`（`group_at_only=true`），`@` 段会从正文中剥离
- 图片段会下载后转成 `data:image/...;base64,...` 交给模型
- 处理结果通过 `POST {api_base}/send_msg` 回发，文本与图片分别成段

响应：`{"status":"ok"}`；找不到对应频道返回 404。

---

## 5. 工具与权限

| 方法 | 路径 | 说明 |
| :--- | :--- | :--- |
| GET | `/api/tools` | `tools/tools.json` 内容 |
| POST | `/api/tools/toggle` | `{"name":"image-drawer","enabled":false}` |
| GET | `/api/permissions` | `{"defaults":{...},"rules":[...],"pending":[...],"timeout_sec":120}` |
| POST | `/api/permissions/set` | `{"tool":"exec","pattern":"*","policy":"allow","note":"..."}` |
| POST | `/api/permissions/delete` | `{"tool":"exec","pattern":"*"}` |
| POST | `/api/permission/respond` | `{"id":"<request id>","granted":true,"remember":false}` |

`policy` 取值：`allow` / `deny` / `ask`。

---

## 6. 文件与任务

| 方法 | 路径 | 说明 |
| :--- | :--- | :--- |
| POST | `/api/fs/list` | `{"path":"."}` 列出工作区内目录；也支持 `GET /api/fs/list?path=.` |
| GET | `/api/fs/used` | 本次运行中被工具访问过的文件 `{"files":[...]}` |
| GET | `/api/todos` | 待办列表 |
| POST | `/api/todos/new` | 新建待办 |
| POST | `/api/todos/:id` | 修改待办 |
| POST | `/api/todos/delete/:id` | 删除待办 |

`/api/fs/list` 只允许工作区内路径，越界返回 `403 {"error":"Path outside workspace"}`。

---

## 7. WebUI 与诊断

| 方法 | 路径 | 说明 |
| :--- | :--- | :--- |
| GET | `/` | WebUI 首页（`webui.path` 为目录时返回 `index`） |
| GET | `/<任意资源>` | 目录模式下按相对路径取静态文件（自动 MIME + ETag） |
| GET | `/api/webui` | WebUI 解析结果 `{"path","mode","index","exists","source":"embedded|disk","embedded_files":n,"files":[...]}` |
| GET | `/api/settings` | 读取当前配置 |
| POST | `/api/settings` | `{"updata":true,"settings":{...}}` 保存配置 |
保存采用**深合并**：只覆盖请求里出现的键，`channels` / `providers` / `webui` 等
未提交的子键原样保留；校验失败返回 `400 {"error":"..."}`。

| GET | `/api/data` | 用量统计（按会话累计 token） |

静态资源规则：

- `webui.source = embedded`（默认）→ 从 exe 内嵌资源返回，磁盘上不需要任何文件
- `webui.source = auto` → 磁盘 `webui.path` 存在时用磁盘，否则退回内嵌
- `webui.source = disk` → 强制用磁盘文件
- `webui.path` 指向**文件** → 所有 `GET /` 返回该文件（兼容旧单文件 WebUI）
- `webui.path` 指向**目录** → `/` → `index`；其他路径映射到目录内文件
- `..` 穿越、符号链接越界一律 404
- 静态资源带 `ETag`；命中 `If-None-Match` 返回 `304`
- `text/*`、`application/json`、`application/javascript`、`image/svg+xml` 自动补 `charset=utf-8`
- 内嵌资源与磁盘资源都带 `ETag`，命中 `If-None-Match` 返回 `304`
- 响应头 `X-WebUI-Source: embedded | disk` 标明本次实际来源

`GET /` 支持 `?theme=light|dark` 指定初始主题（否则用 `localStorage` 里的选择）。

---

## 8. 错误约定

| 状态码 | 含义 |
| :--- | :--- |
| 200 | 成功 |
| 204 | CORS 预检 |
| 400 | 请求体不是合法 JSON / 缺少必填字段 |
| 403 | 路径越界 |
| 404 | 路由或资源不存在 |
| 500 | 内部错误（`{"error":"..."}`） |
| 503 | 并发连接超过上限 |

处理函数内部抛异常时统一返回 `500 {"error":"<message>"}`，并写入 `.agent/logs/agent.log`。
