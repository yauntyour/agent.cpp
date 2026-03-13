# WebUI设计

### 1.应当具有用于接收响应内容的POST接收服务，接受数据规范如下

```json
服务端响应内容：
{
    "model": "qwen3.5:9b",
    "messages": [
        {
            "type": "response",
            "content": "Hello! How can I assist you today?"
        }
    ],
    "stream": true,//返回格式
    "think": false//思考的状态
}
```

`message.type`的值为：`response`或`request`

### 2.应当具有发送请求数据的方法，数据规范如下

```json
客户端请求内容：
{
    "model": "qwen3.5:9b",
    "messages": "Hi",
    "stream": true,
    "think": false
}
```

### 3.应当具有发送控制命令的方法，数据规范如下

```json
控制命令:
{
    "cmd": "restart"
}
```

### 4.其他要求

1. 使用HTTP/S请求访问
2. 自动刷新状态
3. 接口URL
    - `/`: 启动页面
    - `/api`: 返回接口列表
    - `/api/status`: 返回系统状态
    - `/api/control`: 控制指令
    - `/api/input`: 发送输入内容，返回服务端响应
    - `/api/syc`: 同步当前回话的上下文
    - `/api/clear`: 重启当前会话
    - `/api/models`: 返回可用的模型供应商列表
    - `/api/sessions`: 返回回话列表
    - `/api/new`: 请求服务器新建会话，服务器会自动存档当前会话
    - `/api/delete`: 请求删除指定会话（仅删除前端会话，不删除服务器会话）
    - `/api/settings`: 更新服务器设置
    - `/api/loading`: 登录页面
    - `/api/logout`: 登出
    - `/api/login`: 登录
    - `/api/channels`: 返回可用的频道列表及其状态
    - `/api/channels/<name>`: 指定频道的设置
    - `/api/skills`: 返回技能列表及其状态
    - `/api/skills/<name>`: 指定技能的设置
    - `/api/todos`: 返回待办事项列表及其状态
    - `/api/todos/<name>`: 待办事项的设置
    - `/api/todos/new`: 新增待办事项
    - `/api/todos/delete`: 删除指定待办事项