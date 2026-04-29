#!/usr/bin/env python3
"""
image-drawer.py - 调用 sd-cli 服务的 /generate API 生成图像并保存到本地

用法示例:
    python image-drawer.py --prompt "一只猫" --output cat.png
    python image-drawer.py --prompt "风景画" --output landscape.jpg --steps 12 --height 768 --width 768 --negative-prompt "模糊，失真"
"""

import argparse
import os
import base64
import requests
import sys
from typing import Optional


def generate_image(
    server_url: str,
    prompt: str,
    output: str,
    steps: Optional[int] = None,
    height: Optional[int] = None,
    width: Optional[int] = None,
    negative_prompt: Optional[str] = None,
) -> bool:
    """
    调用 /generate API 获取图像数据并保存到本地

    参数:
        server_url: API 服务地址（如 http://localhost:8000）
        prompt: 提示词
        output: 本地输出图像路径（最终保存位置）
        steps: 推理步数（可选）
        height: 图像高度（可选）
        width: 图像宽度（可选）
        negative_prompt: 负提示词（可选）

    返回:
        bool: 是否成功
    """
    url = f"{server_url.rstrip('/')}/generate"

    payload = {
        "prompt": prompt,
    }
    if steps is not None:
        payload["steps"] = steps
    if height is not None:
        payload["height"] = height
    if width is not None:
        payload["width"] = width
    if negative_prompt is not None:
        payload["negative_prompt"] = negative_prompt

    try:
        print(f"正在发送请求到 {url}...")
        response = requests.post(url, json=payload, timeout=300)
        response.raise_for_status()

        result = response.json()
        if result.get("status") == "success":
            # 解码 base64 并写入文件
            image_data = base64.b64decode(result["image_base64"])
            os.makedirs(os.path.dirname(output) or ".", exist_ok=True)
            with open(output, "wb") as f:
                f.write(image_data)
            print(f"✅ 图像已保存到：{output}")
            if result.get("message"):
                print(f"详细信息: {result['message']}")
            return True
        else:
            print(f"❌ 生成失败: {result.get('detail', '未知错误')}")
            return False
    except requests.exceptions.ConnectionError:
        print(f"❌ 无法连接到服务 {server_url}，请确保服务已启动")
        return False
    except requests.exceptions.Timeout:
        print("❌ 请求超时，生成可能仍在进行，请检查服务状态")
        return False
    except requests.exceptions.HTTPError as e:
        print(f"❌ HTTP 错误 {e.response.status_code}: {e.response.text}")
        return False
    except Exception as e:
        print(f"❌ 发生未知错误: {e}")
        return False


def print_tool_help():
    """打印 tool.md 中的帮助信息"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    tool_md_path = os.path.join(script_dir, "tool.md")
    try:
        with open(tool_md_path, "r", encoding="utf-8") as f:
            print(f.read())
    except FileNotFoundError:
        print(f"警告: 未找到帮助文件 {tool_md_path}")


def main():
    parser = argparse.ArgumentParser(
        description="调用 sd-cli 服务的 /generate API 生成图像并保存到本地",
        add_help=False,
    )
    parser.add_argument("--prompt", required=True, help="提示词")
    parser.add_argument(
        "--output",
        default="output.png",
        help="输出图像路径（默认 output.png，会保存在 ./assets/ 目录下）",
    )
    parser.add_argument("--steps", type=int, help="推理步数（可选，9-12 推荐）")
    parser.add_argument("--height", type=int, help="图像高度（可选）")
    parser.add_argument("--width", type=int, help="图像宽度（可选）")
    parser.add_argument("--negative-prompt", help="负提示词（可选）")
    parser.add_argument(
        "--server-url",
        default="http://localhost:8000",
        help="服务地址（默认 http://localhost:8000）",
    )
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

    # 构造本地输出路径（保持原有行为：保存到 ./assets/ 目录）
    output_dir = os.path.join(os.getcwd(), "assets")
    output_path = os.path.join(output_dir, args.output)

    success = generate_image(
        server_url=args.server_url,
        prompt=args.prompt,
        output=output_path,
        steps=args.steps,
        height=args.height,
        width=args.width,
        negative_prompt=args.negative_prompt,
    )
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    if len(sys.argv) == 1:
        print_tool_help()
        sys.exit(-1)
    main()
