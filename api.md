# AI Assistant 后端接口文档（基于 app.cpp 实现）

**基础 URL**: `http://localhost:8080` （默认端口，可在 `main` 函数中调整）

本文档根据 `app.cpp` 的实际实现编写，准确反映当前后端 API 的行为。

---

## 1. 基础系统接口

### 1.1 获取 WebUI 页面

- **路径**: `/`
- **方法**: `GET`
- **描述**: 返回配置文件中 `webui` 字段指定的 HTML 页面。若读取失败，返回错误信息。
- **响应 (200 OK)**: `text/html`

### 1.2 获取 API 列表

- **路径**: `/api`
- **方法**: `GET`
- **描述**: 返回后端所有可用接口的硬编码列表（包括路由、方法、描述）。
- **响应 (200 OK)**: `application/json`

### 1.3 获取系统状态

- **路径**: `/api/status`
- **方法**: `GET`
- **描述**: 执行 `python.exe ./sys/sys_state.py` 获取系统综合状态。
- **响应 (200 OK)**:

```json
{
  "raw_output": "Python脚本的标准输出内容",
  "timestamp": 1710432000
}
```

- **错误响应 (500)**: 纯文本错误信息。

### 1.4 更新服务器设置

- **路径**: `/api/settings`
- **方法**: `POST`
- **描述**: 更新服务器运行时的部分设置（当前仅支持 `stream` 字段）。
- **请求 Body (JSON)**:

```json
{
  "stream": true
}
```

- **响应 (200 OK)**:

```json
{"status": "OK"}
```

- **错误响应 (500)**: 纯文本错误信息。

### 1.5 发送控制指令

- **路径**: `/api/control`
- **方法**: `POST`
- **描述**: 预留的控制指令接口，当前实现仅返回固定的成功响应，不执行任何操作。
- **请求 Body (JSON)**: 任意有效 JSON 对象。
- **响应 (200 OK)**:

```json
{
  "result": "success",
  "executed": "null"
}
```

---

## 2. AI 模型与对话接口

### 2.1 获取可用模型列表

- **路径**: `/api/models`
- **方法**: `GET`
- **描述**: 代理请求本地 Ollama 服务的 `/api/tags` 接口，返回所有已安装模型。
- **响应 (200 OK)**: Ollama 标准 JSON 结构。
- **错误响应 (500)**: `{"error":"cannot connect to Ollama"}`

### 2.2 发送用户输入并生成回复

- **路径**: `/api/input`
- **方法**: `POST`
- **描述**: 接收用户消息，调用 Ollama 生成回复，支持多轮工具调用/系统命令调用，并自动进行上下文管理（超限时生成摘要记忆）。
- **请求 Body (JSON)**:

```json
{
  "messages": "用户输入的内容",
  "model": "llama3",           // 若为 "default" 则使用配置文件中的默认模型
  "think": false,              // 是否启用思考模式（返回 reasoning_content）
  "channel": "general"         // 可选，指定频道名称（用于多频道会话隔离）
}
```

> **注意**：
>
> - 若提供 `channel`，用户消息会被识别为频道，并存入对应频道的独立会话记录中。

- **响应 (200 OK)**:

```json
{
  "model": "实际使用的模型名",
  "messages": [
    {
      "type": "response",
      "content": "最终回复内容，包含工具调用结果和上下文长度信息"
    },
    {
      "type": "think",
      "content": ["思考过程1", "思考过程2"]
    },
    {
      "type": "images",
      "content": ["工具生成的 base64 图片列表"]
    }
  ],
  "stream": false,
  "think": false
}
```

> 说明：
>
> - 后端实际使用非流式生成（`"stream": false`），一次请求即返回完整回复。
> - `content` 中包含工具调用过程的输出标记，并以 `[Number of characters (including im-tokens, excluding media file size): xxx]` 结尾。

---

## 3. 会话 (Session) 管理

会话基于内存管理器 `SessionManager`，每个会话具有唯一 ID，支持多频道会话（通过 `channel` 参数区分）。

### 3.1 获取会话列表

- **路径**: `/api/session`
- **方法**: `GET`
- **描述**: 返回当前所有会话的 ID 列表。
- **响应 (200 OK)**:

```json
{
  "session_list": ["1710432000", "1710432055"]
}
```

### 3.2 新建会话

- **路径**: `/api/new`
- **方法**: `POST`
- **描述**: 创建新会话，同时重置全局 Agent 上下文（清空 `Agent_session_context` 并重新注入 system prompt 与工具列表），新会话自动设为当前会话。
- **响应 (200 OK)**:

```json
{
  "status": "OK",
  "session_id": "1710432500"
}
```

### 3.3 切换当前会话

- **路径**: `/api/session/set`
- **方法**: `POST`
- **描述**: 将指定会话设为当前激活会话，并返回该会话的历史消息。同时会重建全局 Agent 上下文（若会话包含记忆摘要则一并加载）。
- **请求 Body (JSON)**:

```json
{
  "session_id": "1710432000"
}
```

- **响应 (200 OK)**:

```json
{
  "status": "done",
  "messages": ["..."]
}
```

- **错误响应 (200 OK 但 status=failed)**: `{"status": "failed"}`

### 3.4 清空当前会话

- **路径**: `/api/clear`
- **方法**: `POST`
- **描述**: 清空当前会话的消息记录，并重置全局 Agent 上下文。
- **响应 (200 OK)**:

```json
{"status": "cleared"}
```

### 3.5 删除指定会话

- **路径**: `/api/delete` （路由在代码中被注释，未实际注册）
- **方法**: `POST`
- **描述**: 从管理器中移除指定会话。**当前未启用**。

### 3.6 生成当前会话的记忆摘要

- **路径**: `/api/session/memory`
- **方法**: `POST`
- **描述**: 基于当前会话历史生成摘要（abstract）和关键词（keywords），并存入会话的 `memory` 字段，同时刷新全局 Agent 上下文以包含记忆。
- **请求 Body**: 无。
- **响应 (200 OK)**:

```json
{"status": "done"}
```

- **错误响应**: `{"status": "failed"}`

---

## 4. 身份认证 (Auth)

### 4.1 获取登录页面

- **路径**: `/api/loading`
- **方法**: `GET`
- **描述**: 返回内嵌的简易 HTML 登录表单。
- **响应 (200 OK)**: `text/html`

### 4.2 用户登录

- **路径**: `/api/login`
- **方法**: `POST`
- **描述**: 验证用户名和密码。**注意**：密码在请求中应为 **SHA3-256 哈希值的十六进制字符串**（与初始化时存储的哈希比对）。
- **请求 Body (JSON)**:

```json
{
  "username": "admin",
  "password": "sha3-256哈希的十六进制字符串"
}
```

- **响应 (200 OK)**:

```json
{
  "token": "fake-jwt-token",
  "user": "admin"
}
```

- **错误响应 (401)**: `{"error":"invalid credentials"}`

### 4.3 用户登出

- **路径**: `/api/logout`
- **方法**: `GET`
- **描述**: 登出接口，当前无实际操作，仅返回纯文本。
- **响应 (200 OK)**: `Logged out.` (Content-Type: text/plain)

---

## 5. 扩展功能（频道、工具、待办）

### 5.1 获取频道列表

- **路径**: `/api/channels`
- **方法**: `GET`
- **描述**: 返回配置文件中 `channels` 字段的内容。
- **响应 (200 OK)**: JSON 数组，具体格式由配置文件决定。

### 5.2 获取工具列表

- **路径**: `/api/tools`
- **方法**: `GET`
- **描述**: 返回 `tools/tools.json` 文件的内容，该文件定义了所有可用工具及其描述、参数等。
- **响应 (200 OK)**: JSON 数组，结构与 `tools.json` 一致。

### 5.3 获取特定工具详情

- **路径**: `/api/tools/:name`
- **方法**: `GET`
- **描述**: 根据工具名称返回对应工具的完整信息。
- **响应 (200 OK)**: 单个工具的 JSON 对象。
- **响应 (400)**: 空 JSON 对象 `{}`。

### 5.4 获取待办事项列表

- **路径**: `/api/todos`
- **方法**: `GET`
- **描述**: 读取 `todos.json` 文件并返回所有待办事项。
- **响应 (200 OK)**: JSON 数组，例如：

```json
[
  {"id": "1", "title": "完成报告", "done": false}
]
```

- **错误响应 (500)**: 返回空数组 `[]`。

### 5.5 新增待办事项

- **路径**: `/api/todos/new`
- **方法**: `POST`
- **描述**: 向 `todos.json` 添加新条目。
- **请求 Body (JSON)**: 待办事项对象（需包含 `id` 等必要字段）。
- **响应 (200 OK)**: 返回新增的待办对象。

### 5.6 更新待办事项

- **路径**: `/api/todos/:id`
- **方法**: `POST`
- **描述**: 根据 ID 更新待办事项内容。
- **请求 Body (JSON)**: 完整的待办事项新数据。
- **响应 (200 OK)**: 空 JSON `{}`。
- **响应 (404)**: 空 JSON `{}`（未找到对应 ID）。

### 5.7 删除待办事项

- **路径**: `/api/todos/delete/:id`
- **方法**: `POST`
- **描述**: 根据 ID 删除待办事项。
- **响应 (200 OK)**: 空 JSON `{}`。

---

## 6. 未实现或占位接口

以下路由在代码中已注册但处理函数仅返回 `0` 或未实现完整逻辑：

| 路由 | 方法 | 说明 |
|------|------|------|
| `/api/channels/:name` | GET/POST | 处理函数 `handle_channels_setting` 直接返回 0，未实现 |
| `/api/tools/:name` | POST | 仅实现了 GET，POST 未实现 |
| `/api/todos/:id` | GET | 未实现（仅实现了 POST） |

---

## 7. 关键实现细节说明

1. **配置文件**  
   后端启动时读取 `D:\Developments\CXX\Agent.cpp\setting.json`（路径硬编码），包含：
   - `server_address`: Ollama 服务地址
   - `name`: 管理员用户名
   - `webui`: WebUI 页面路径
   - `prompt_path`: system prompt 文件路径
   - `workspace`: 会话存储工作区
   - `model`: 默认模型
   - `agent_nickname`: Agent 昵称
   - `max_mpc_rounds`: 最大工具调用轮数
   - `max_context`: 上下文最大字符数（触发记忆摘要）
   - `channels`: 频道列表
   - `stream`: 是否流式输出（当前实际未使用）

2. **工具调用与系统命令**  
   - 在生成回复后，会扫描内容中的特殊标记（`tools_scan` / `cs_scan`），若触发则自动执行相应脚本，并将输出追加到对话上下文中再次调用模型，直至无更多调用或达到轮数上限。

3. **上下文记忆**  
   当上下文长度超过 `max_context` 且本次新增内容导致超限时，后端会异步生成当前会话的摘要和关键词，存入会话的 `memory` 字段，并在后续对话中将摘要作为 `memory` 角色的消息注入。

4. **会话隔离**  
   - 全局 `Agent_session_context` 维护的是**当前激活会话**的完整对话历史（用于模型调用）。
   - 切换会话时会重建该上下文。
   - 通过 `channel` 参数可在同一会话管理器下创建不同频道的会话记录，但全局上下文始终只对应一个激活会话。

5. **密码验证**  
   登录时前端传入的密码应为原始密码的 SHA3-256 哈希十六进制字符串，后端直接与配置中存储的哈希字符串比对，不进行额外哈希计算。

6. **CORS**  
   所有响应均添加 CORS 头，允许任意来源访问。

---

文档最后更新：2026-4-18
