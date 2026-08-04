# -*- coding: utf-8 -*-
"""
mcp-linker.py - 调用 MCP (Model Context Protocol) 服务器的命令行工具

支持两种连接方式：
    Streamable HTTP / SSE:  --server-url http://localhost:8000/mcp
    本地 stdio 进程:         --server-command "npx -y @modelcontextprotocol/server-everything"

用法示例:
    python run.py --server-url http://localhost:8000/mcp --list
    python run.py --server-url http://localhost:8000/mcp --call fetch --args "{\"url\": \"https://example.com\"}"
    python run.py --server-command "npx -y @modelcontextprotocol/server-everything" --call echo --args "{\"text\": \"hello\"}"
"""

import argparse
import asyncio
import base64
import json
import os
import shlex
import sys
from typing import Optional
from urllib.parse import urlparse

import httpx
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp.client.streamable_http import streamable_http_client


def print_tool_help():
    print("警告⚠️：工具调用异常")
    """打印 tool.md 中的帮助信息"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    tool_md_path = os.path.join(script_dir, "tool.md")
    try:
        with open(tool_md_path, "r", encoding="utf-8") as f:
            print(f.read())
    except FileNotFoundError:
        print(f"警告: 未找到帮助文件 {tool_md_path}")


async def list_tools(session: ClientSession) -> bool:
    """列出 MCP 服务器上所有可用的工具"""
    try:
        tools = await session.list_tools()
    except Exception as e:
        print(f"❌ 获取工具列表失败: {e}")
        return False

    if not tools.tools:
        print("该 MCP 服务器未暴露任何工具")
        return True

    print(f"✅ 共发现 {len(tools.tools)} 个工具:\n")
    for t in tools.tools:
        print(f"▶ {t.name}")
        if t.description:
            print(f"  描述: {t.description}")
        if t.inputSchema:
            print(f"  参数 schema: {json.dumps(t.inputSchema, ensure_ascii=False)}")
        print()
    return True


async def call_tool(session: ClientSession, name: str, arguments: dict) -> bool:
    """调用 MCP 服务器上的指定工具并输出结果"""
    try:
        result = await session.call_tool(name, arguments)
    except Exception as e:
        print(f"❌ MCP 工具 {name} 调用失败: {e}")
        return False

    if getattr(result, "isError", False):
        print(f"❌ MCP 工具 {name} 返回错误:")
        for c in result.content:
            if c.type == "text":
                print(c.text)
        return False

    assets_dir = os.path.join(os.getcwd(), "workspace/assets")
    img_idx = 0
    for c in result.content:
        if c.type == "text":
            print(c.text)
        elif c.type == "image":
            img_idx += 1
            data = getattr(c, "data", None)
            mime = getattr(c, "mimeType", "image/png")
            ext = mime.split("/")[-1].split(";")[0] or "png"
            filename = f"mcp-{name}-{img_idx}.{ext}"
            os.makedirs(assets_dir, exist_ok=True)
            filepath = os.path.join(assets_dir, filename)
            try:
                with open(filepath, "wb") as f:
                    f.write(base64.b64decode(data))
                print(f"🖼 图像已保存到：{filepath}")
            except Exception as e:
                print(f"❌ 图像保存失败: {e}")
                return False
        else:
            print(f"[{c.type}] {getattr(c, 'data', c)}")

    if result.structuredContent is not None:
        print(json.dumps(result.structuredContent, ensure_ascii=False, indent=2))
    return True


async def dispatch(session: ClientSession, list_only: bool, call: Optional[str], kwargs: dict) -> bool:
    if list_only:
        print("🔍 列出 MCP 服务器可用工具...")
        return await list_tools(session)
    if call:
        print(f"🔧 调用 MCP 工具: {call}")
        if kwargs:
            print(f"   参数: {json.dumps(kwargs, ensure_ascii=False)}")
        return await call_tool(session, call, kwargs)
    print_tool_help()
    return False


def is_loopback(url: str) -> bool:
    """判断目标地址是否为本机回环地址（回环地址不应走系统代理）"""
    host = urlparse(url).hostname or ""
    return host in ("localhost", "127.0.0.1", "::1", "[::1]")


async def run(list_only: bool, call: Optional[str], kwargs: dict,
              server_url: Optional[str] = None, server_command: Optional[str] = None) -> bool:
    if server_url:
        # 回环地址禁用代理（避免 HTTP_PROXY 把本地 MCP 请求转发给代理导致 502）
        async with httpx.AsyncClient(
            timeout=httpx.Timeout(300.0, connect=30.0),
            trust_env=not is_loopback(server_url),
        ) as http_client:
            async with streamable_http_client(server_url, http_client=http_client) as (read, write, _):
                async with ClientSession(read, write) as session:
                    await session.initialize()
                    return await dispatch(session, list_only, call, kwargs)
    else:
        tokens = shlex.split(server_command)
        params = StdioServerParameters(command=tokens[0], args=tokens[1:])
        async with stdio_client(params) as (read, write):
            async with ClientSession(read, write) as session:
                await session.initialize()
                return await dispatch(session, list_only, call, kwargs)


def parse_json_args(raw: Optional[str]) -> dict:
    if not raw:
        return {}
    try:
        value = json.loads(raw)
    except json.JSONDecodeError:
        print_tool_help()
        print(f"❌ 参数不是合法 JSON: {raw}")
        sys.exit(2)
    if not isinstance(value, dict):
        print_tool_help()
        print("❌ 参数必须是 JSON 对象，如 {\"key\": \"value\"}")
        sys.exit(2)
    return value


def main():
    parser = argparse.ArgumentParser(
        description="调用 MCP 服务器的命令行工具",
        add_help=False,
    )
    parser.add_argument("--server-url", help="MCP 服务器地址（Streamable HTTP/SSE），如 http://localhost:8000/mcp")
    parser.add_argument("--server-command", help="本地 MCP 服务器启动命令（stdio），如 npx -y @modelcontextprotocol/server-everything")
    parser.add_argument("--list", action="store_true", help="列出服务器上所有可用工具")
    parser.add_argument("--call", help="调用指定名称的 MCP 工具")
    parser.add_argument("--args", help="调用参数，JSON 对象字符串，如 {\"query\": \"hello\"}")
    parser.add_argument("-h", "--help", action="store_true", help="显示帮助信息")

    try:
        args = parser.parse_args()
    except SystemExit as e:
        print_tool_help()
        if e.code == 0:
            sys.exit(0)
        else:
            sys.exit(2)

    if args.help:
        print_tool_help()
        sys.exit(0)

    if not args.server_url and not args.server_command:
        print_tool_help()
        sys.exit(2)

    if not args.list and not args.call:
        print_tool_help()
        sys.exit(2)

    kwargs = parse_json_args(args.args)
    success = asyncio.run(
        run(args.list, args.call, kwargs, server_url=args.server_url, server_command=args.server_command)
    )
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    if len(sys.argv) == 1:
        print_tool_help()
        sys.exit(-1)
    main()
