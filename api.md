# AI Assistant 后端接口文档

**基础 URL**: `http://localhost:8080` (基于 `main` 函数中的端口配置)

## 1. 基础系统接口

### 1.1 获取 WebUI 页面

* **路径**: `/`
* **方法**: `GET`
* **描述**: 返回系统配置中指定的 WebUI HTML 页面。如果读取失败，会返回错误提示信息。
* **响应 (200 OK)**: HTML 文档。

### 1.2 获取 API 列表

* **路径**: `/api`
* **方法**: `GET`
* **描述**: 返回当前后端所有可用接口的简要说明列表。
* **响应 (200 OK)**: `application/json`

### 1.3 获取系统状态

* **路径**: `/api/status`
* **方法**: `GET`
* **描述**: 通过执行 Python 脚本 `./sys/sys_state.py` 获取系统综合状态。
* **响应 (200 OK)**:

```json
{
  "raw_output": "python脚本执行的输出结果",
  "timestamp": 1710432000
}

```

### 1.4 更新服务器设置

* **路径**: `/api/settings`
* **方法**: `POST`
* **描述**: 更新服务器的流式输出等设置。
* **请求 Body (JSON)**:

```json
{
  "stream": true
}

```

* **响应 (200 OK)**: `{"status": "OK"}`

### 1.5 切换模型供应商

* **路径**: `/api/provider`
* **方法**: `POST`
* **描述**: 运行时动态切换 LLM 供应商（OpenAI/Ollama/Llama.cpp），并更新配置文件。
* **请求 Body (JSON)**:

```json
{
  "provider": "openai",
  "server_address": "http://localhost:11434",
  "api_key": ""
}

```

| 字段 | 类型 | 必填 | 说明 |
| :--- | :--- | :--- | :--- |
| `provider` | string | 否 | 供应商类型：`openai`（默认）、`ollama`、`llama` |
| `server_address` | string | 否 | LLM 服务地址，不传则使用当前配置 |
| `api_key` | string | 否 | API 密钥，不传则使用当前密钥 |

* **响应 (200 OK)**:

```json
{
  "status": "OK",
  "provider": "openai",
  "server_address": "http://localhost:11434"
}

```

### 1.6 发送控制指令

* **路径**: `/api/control`
* **方法**: `POST`
* **描述**: 接收前端发送的控制指令（预留接口，当前固定返回 success）。
* **请求 Body (JSON)**: 任意有效的 JSON 对象。
* **响应 (200 OK)**: `{"result": "success", "executed": "null"}`

---

## 2. AI 模型与对话接口

### 2.1 获取可用模型列表

* **路径**: `/api/models`
* **方法**: `GET`
* **描述**: 根据当前供应商（openai / ollama / llama）请求其模型列表接口，统一返回 OpenAI 格式。
  * `openai` / `llama`：`GET /v1/models`（OpenAI 兼容接口）
  * `ollama`：`GET /api/tags`（本地模型）+ `GET /api/ps`（加载状态）
* **响应 (200 OK)**:

```json
{
  "data": [
    {
      "id": "llama3.2:latest",
      "status": { "value": "loaded" }
    }
  ]
}
```

### 2.2 接收用户输入并生成回复

* **路径**: `/api/input`
* **方法**: `POST`
* **描述**: 接收用户的对话信息，调用本地 Ollama 生成回复，并在遇到工具调用 (`tools_scan`) 或系统命令 (`cs_scan`) 时触发二次生成。
* **请求 Body (JSON)**:

```json
{
  "messages": "你好，请自我介绍",
  "model": "llama3",
  "think": false
}

```

* **响应 (200 OK)**:

```json
{
  "model": "llama3",
  "messages": [
    {
      "type": "response",
      "content": "\r\n思考过程\r\n回答内容\r\n"
    }
  ],
  "stream": true,
  "think": false
}

```

---

## 3. 会话 (Session) 管理

### 3.1 获取会话列表

* **路径**: `/api/session`
* **方法**: `GET`
* **描述**: 返回当前所有保存在内存中的会话 ID 列表。
* **响应 (200 OK)**:

```json
{
  "session_list": ["1710432000", "1710432055"]
}

```

### 3.2 新建会话

* **路径**: `/api/new`
* **方法**: `POST`
* **描述**: 创建一个新的会话，并将系统上下文 (`Agent_session_context`) 重置为初始 system prompt。
* **响应 (200 OK)**:

```json
{
  "status": "OK",
  "session_id": "1710432500"
}

```

### 3.3 切换当前会话

* **路径**: `/api/session/set`
* **方法**: `POST`
* **描述**: 将指定的 session_id 设置为当前激活的会话，并返回该会话的历史消息。
* **请求 Body (JSON)**:

```json
{
  "session_id": "1710432000"
}

```

* **响应 (200 OK)**:

```json
{
  "status": "done",
  "messages": ["User: ...", "context:..."]
}

```

### 3.4 清空当前会话

* **路径**: `/api/clear`
* **方法**: `POST`
* **描述**: 清空当前会话的消息记录，并重置系统上下文。
* **响应 (200 OK)**: `{"status": "cleared"}`

### 3.5 删除指定会话

* **路径**: `/api/delete`
* **方法**: `POST`
* **描述**: 从管理器中彻底移除指定的会话。
* **请求 Body (JSON)**:

```json
{
  "session_id": "1710432000"
}

```

* **响应 (200 OK)**: `{"status": "deleted"}`

---

## 4. 身份认证 (Auth)

### 4.1 获取登录页面

* **路径**: `/api/loading`
* **方法**: `GET`
* **描述**: 返回一段内置的简易 HTML 登录表单。
* **响应 (200 OK)**: `text/html`

### 4.2 用户登录

* **路径**: `/api/login`
* **方法**: `POST`
* **描述**: 验证用户名和密码（密码验证前已使用 SHA3-256 哈希处理）。*注意：代码中使用的是简单的字符串截取寻找 `"username":"..."`。*
* **请求 Body (JSON)**:

```json
{
  "username": "admin",
  "password": "your_password"
}

```

* **响应 (200 OK / 401 Unauthorized)**:

```json
{
  "token": "fake-jwt-token",
  "user": "admin"
}

```

### 4.3 用户登出

* **路径**: `/api/logout`
* **方法**: `GET`
* **描述**: 登出接口，当前返回纯文本。
* **响应 (200 OK)**: `Logged out.` (Content-Type: text/plain)

---

## 5. 扩展功能 (工具、频道、待办)

### 5.1 获取工具列表

* **路径**: `/api/tools`
* **方法**: `GET`
* **描述**: 返回工具列表（本地 `tools.json` 工具描述）。
* **响应 (200 OK)**:

```json
[
  {
    "name": "tool_name",
    "description": "工具的单行描述",
    "enabled": true
  },
  {
    "name": "fetch",
    "description": "MCP 工具描述",
    "enabled": true,
    "mcp": true,
    "server": "fetch",
    "tool": "fetch",
    "schema": {}
  }
]
```

### 5.1.1 切换工具启用状态

* **路径**: `/api/tools/toggle`
* **方法**: `POST`
* **描述**: 启用/禁用本地工具（MCP 工具请使用 `/api/mcp/toggle`）。
* **请求体**:

```json
{ "name": "image-drawer", "enabled": false }
```

### 5.2 获取频道列表

* **路径**: `/api/channels`
* **方法**: `GET`
* **描述**: 返回硬编码的测试频道列表。
* **响应 (200 OK)**:

```json
[
  {"name": "general", "status": "active", "user_count": 1}
]

```

### 5.3 获取待办事项 (Todos)

* **路径**: `/api/todos`
* **方法**: `GET`
* **描述**: 返回硬编码的待办事项列表。
* **响应 (200 OK)**:

```json
[
  {"id": 1, "title": "Complete report", "done": false}
]

```

### 5.4 动态路由 (占位/未实现逻辑)

以下接口在代码中已注册路由，但对应的处理函数当前仅直接 `return 0;`，尚未实现实际业务逻辑：

* **`GET/POST`** `/api/channels/:name` (查询或设置具体频道)
* **`GET/POST`** `/api/tools/:name` (查询或设置具体工具)
* **`GET/POST`** `/api/todos/:name` (查询或设置具体待办)
* **`POST`** `/api/todos/new` (新增待办事项)
* **`POST`** `/api/todos/delete` (删除待办事项)

---

## 6. 工具权限系统 (Tool Permission System)

工具权限系统为 Agent 的工具调用提供三级策略：`allow`（放行）/ `deny`（拒绝）/ `ask`（询问用户）。

* **策略解析顺序**：用户规则（`workspace/permissions.json`）→ 默认策略（`settings.json → permissions.defaults`）→ 兜底（内置：`exec/write/edit/wget` 为 `ask`，其余 `allow`）。
* **授权等待**：命中 `ask` 的工具在 WebUI 流式会话中触发授权弹窗，等待期间由事件循环其他工作线程并发处理响应请求；超时（`permissions.timeout_sec`，默认 120 秒）自动拒绝。
* **无交互上下文**（Telegram / WeChat 频道）：`ask` 按 `permissions.channel_ask` 处理（默认 `deny`，安全优先）。

### 6.1 获取权限配置

* **路径**: `/api/permissions`
* **方法**: `GET`
* **描述**: 返回当前权限规则、默认策略、待审批队列与超时配置。
* **响应 (200 OK)**:

```json
{
  "rules": [
    {
      "tool": "exec",
      "pattern": "",
      "policy": "allow",
      "note": "user",
      "created_at": 1786678973
    }
  ],
  "defaults": {
    "exec": "ask",
    "write": "ask",
    "edit": "ask",
    "wget": "ask",
    "read": "allow",
    "Image": "allow",
    "*": "allow"
  },
  "pending": [],
  "timeout_sec": 120
}
```

### 6.2 新增 / 更新规则

* **路径**: `/api/permissions/set`
* **方法**: `POST`
* **描述**: 新增或更新一条权限规则。`tool` 为工具名（`*` 表示全部）；`pattern` 为参数匹配子串（空或 `*` 表示全部参数）；`policy` 取值 `allow` / `deny` / `ask`。
* **请求 Body (JSON)**:

```json
{
  "tool": "exec",
  "pattern": "rm -rf",
  "policy": "deny"
}
```

* **响应 (200 OK)**: `{"status": "OK", "tool": "exec", "pattern": "rm -rf", "policy": "deny"}`

### 6.3 删除规则

* **路径**: `/api/permissions/delete`
* **方法**: `POST`
* **描述**: 删除指定工具 + 参数模式对应的规则。
* **请求 Body (JSON)**: `{"tool": "exec", "pattern": "rm -rf"}`
* **响应 (200 OK)**: `{"status": "OK", "tool": "exec", "pattern": "rm -rf"}`

### 6.4 响应待审批请求

* **路径**: `/api/permission/respond`
* **方法**: `POST`
* **描述**: 用户对授权弹窗做出决定。`remember` 为 `true` 时将该决定持久化为该工具的全局规则（后续调用不再询问）。
* **请求 Body (JSON)**:

```json
{
  "id": "42f3fbd0be611b832dd5ecd23cd890f2",
  "granted": true,
  "remember": false
}
```

* **响应 (200 OK)**: `{"status": "OK", "id": "...", "granted": true, "remember": false}`

### 6.5 流式 SSE 权限事件

`/api/input/stream` 在工具调用命中 `ask` 策略时推送以下事件：

* **`permission_request`**（授权请求到达，WebUI 弹出授权窗口）:

```json
{"type": "permission_request", "id": "...", "session_id": "...", "tool": "exec", "args": "echo hello", "timeout": 120}
```

* **`permission_result`**（授权结果：用户响应或超时）:

```json
{"type": "permission_result", "id": "...", "granted": true, "remember": false, "timed_out": false}
```

### 6.6 权限配置文件

`workspace/permissions.json`（规则持久化，由 WebUI 权限面板 / API 管理）：

```json
[
  {
    "tool": "exec",
    "pattern": "",
    "policy": "allow",
    "note": "user (remembered)",
    "created_at": 1786678973
  }
]
```

`settings.json` 权限相关配置：

```json
"permissions": {
    "timeout_sec": 120,
    "channel_ask": "deny",
    "defaults": {
        "exec": "ask",
        "write": "ask",
        "edit": "ask",
        "wget": "ask",
        "read": "allow",
        "Image": "allow",
        "*": "allow"
    }
}
```
