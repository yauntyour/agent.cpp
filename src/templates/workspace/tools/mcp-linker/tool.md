# mcp-linker：调用 MCP (Model Context Protocol) 服务器的命令行工具
## Tool Command
```
<tool>mcp-linker:--server-url <URL> --call <tool-name> --args "<JSON>"</tool>
```
## 连接方式（二选一）
- `--server-url <url>`: 连接远程 MCP 服务器（Streamable HTTP / SSE 协议），如 `http://localhost:8000/mcp`
- `--server-command "<cmd>"`: 以 stdio 方式启动本地 MCP 服务器进程，如 `npx -y @modelcontextprotocol/server-everything`

## Parameters
- `--list`: 列出 MCP 服务器上所有可用的工具（名称、描述、参数 schema）。
- `--call <tool-name>`: 调用指定名称的 MCP 工具。
- `--args "<json>"`: 调用参数，JSON 对象字符串，如 `{"query": "hello"}`。无参数时省略即可。

## 使用示例
```
<tool>mcp-linker:--server-url http://localhost:8000/mcp --list</tool>
<tool>mcp-linker:--server-url http://localhost:8000/mcp --call fetch --args "{\"url\": \"https://example.com\"}"</tool>
<tool>mcp-linker:--server-command "npx -y @modelcontextprotocol/server-everything" --call echo --args "{\"text\": \"hello\"}"</tool>
```
## 注意事项
- 返回的文本结果直接输出到标准输出；返回的图像（image 类型）会保存到当前目录下的 `workspace/assets/` 文件夹。
- 调用前可先用 `--list` 查看该服务器暴露了哪些工具及其参数要求。
- `--server-url` 与 `--server-command` 二选一，不可同时使用。
