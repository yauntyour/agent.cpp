# agent.cpp HTTP API 文档

通过 `agent --router` 启动的 RESTful HTTP 服务，默认监听 `127.0.0.1:18080`。

- **基础 URL:** `http://<bind>:<port>`（默认 `http://127.0.0.1:18080`）
- **请求体格式:** `application/json`
- **认证方式:** `Authorization: Bearer <token>`（通过 `/api/login` 获取）
- **支持的 CORS 方法:** `GET, POST, PUT, DELETE, OPTIONS`

---

## 响应格式

### 成功响应

```json
{
  "success": true,
  "data": { ... }
}
```

### 错误响应

```json
{
  "success": false,
  "error": "<error message>",
  "code": 400
}
```

HTTP 状态码: `200`, `201`, `204`, `400`, `401`, `403`, `404`, `405`, `500`

---

## 端点列表

### `GET /`

服务信息。

**认证:** 否

**响应:**

```json
{
  "success": true,
  "data": {
    "service": "agent.cpp",
    "version": "1.0.0"
  }
}
```

---

### `GET /api/health`

系统健康状态与模块信息。

**认证:** 否

**响应:**

```json
{
  "success": true,
  "data": {
    "version": "1.0.0",
    "agent_dir": "/path/to/agent",
    "modules": [
      { "name": "router", "active": true }
    ]
  }
}
```

---

### `POST /api/login`

密码认证，获取 Bearer Token。

**认证:** 否

**请求体:**

```json
{
  "password": "your_password"
}
```

**响应 (200):**

```json
{
  "success": true,
  "data": {
    "authenticated": true,
    "token": "base64_32byte_token_string"
  }
}
```

**响应 (401):**

```json
{
  "success": false,
  "error": "Invalid password",
  "code": 401
}
```

---

### `POST /api/logout`

使当前 Bearer Token 失效。

**认证:** 是

**请求体:** 无

**响应:**

```json
{
  "success": true,
  "data": {
    "logged_out": true
  }
}
```

---

### `POST /api/input`

非流式执行一次 agent 调用。

**认证:** 是

**请求体:**

```json
{
  "text": "用户的输入文本"
}
```

**响应:**

```json
{
  "success": true,
  "data": {
    "output": "agent 的输出内容",
    "iterations": 3,
    "success": true
  }
}
```

---

### `POST /api/input/stream`

流式执行一次 agent 调用，使用 Server-Sent Events (SSE)。

**认证:** 是

**请求体:**

```json
{
  "text": "用户的输入文本"
}
```

**响应:** `Content-Type: text/event-stream`

```
data: <thinking 内容>

data: <下一步输出>

data: [DONE]

```

---

### `GET /api/session`

获取所有会话列表。

**认证:** 是

**响应:**

```json
{
  "success": true,
  "data": [
    {
      "id": "session_id",
      "name": "会话名称",
      "message_count": 10,
      "created_at": 1712345678
    }
  ]
}
```

---

### `POST /api/session/new`

创建新会话。

**认证:** 是

**请求体:**

```json
{
  "name": "可选会话名称"
}
```

**响应:**

```json
{
  "success": true,
  "data": {
    "id": "new_session_id",
    "name": "会话名称"
  }
}
```

---

### `POST /api/session/delete`

删除指定会话。

**认证:** 是

**请求体:**

```json
{
  "id": "session_id"
}
```

**响应:**

```json
{
  "success": true,
  "data": {}
}
```

---

### `POST /api/session/switch`

切换到指定会话。

**认证:** 是

**请求体:**

```json
{
  "id": "session_id"
}
```

**响应:**

```json
{
  "success": true,
  "data": {
    "current": "session_id"
  }
}
```

---

### `GET /api/models`

获取可用模型列表。

**认证:** 是

**响应:**

```json
{
  "success": true,
  "data": [
    {
      "id": "gpt-4",
      "name": "GPT-4",
      "context_length": 8192,
      "supports_vision": true,
      "supports_tools": true
    }
  ]
}
```

---

### `POST /api/models/switch`

切换当前使用的 Provider 或模型。

**认证:** 是

**请求体:** 至少提供一项

```json
{
  "provider": "provider_name",
  "model": "model_id"
}
```

**响应:**

```json
{
  "success": true,
  "data": {
    "current_model": "gpt-4"
  }
}
```

---

### `GET /api/tools`

获取注册的工具列表。

**认证:** 是

**响应:**

```json
{
  "success": true,
  "data": [
    {
      "name": "tool_name",
      "description": "工具描述",
      "categories": ["category1", "category2"]
    }
  ]
}
```

---

### `GET /api/memory`

获取所有记忆条目。

**认证:** 是

**响应:**

```json
{
  "success": true,
  "data": [
    {
      "id": "mem_id",
      "title": "记忆标题",
      "category": "category",
      "importance": 0.85
    }
  ]
}
```

---

### `POST /api/memory/search`

搜索记忆条目。

**认证:** 是

**请求体:**

```json
{
  "query": "搜索关键词",
  "max_results": 5
}
```

两个字段均可选（默认 `""` 和 `5`）。

**响应:**

```json
{
  "success": true,
  "data": [
    {
      "id": "mem_id",
      "title": "记忆标题",
      "content": "记忆内容",
      "category": "category"
    }
  ]
}
```

---

### `GET /api/config`

获取当前配置（不包含 API Key 等敏感信息）。

**认证:** 是

**响应:**

```json
{
  "success": true,
  "data": {
    "default_provider": "openai",
    "default_model": "gpt-4",
    "providers": [
      {
        "name": "openai",
        "type": "openai",
        "api_base": "https://api.openai.com",
        "model": "gpt-4"
      }
    ],
    "max_context_tokens": 128000,
    "stream_output": true
  }
}
```

> **注意:** API Keys **不会**出现在此响应中。

---

## 配置项

| 配置项 | 默认值 | 说明 |
|--------|--------|------|
| `router_port` | `18080` | 监听端口 |
| `router_bind` | `127.0.0.1` | 绑定地址 |
| `router_tls` | `false` | 是否启用 TLS |
| `router_cert_path` | `""` | TLS 证书路径 |
| `router_key_path` | `""` | TLS 密钥路径 |
| `router_password_hash` | `""` | 密码哈希（为空时跳过认证） |
