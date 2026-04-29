#!/usr/bin/env python3
"""
playwright-cli 自动化工具

直接透传参数给 playwright-cli，支持单条命令或命令链（--chain）。
用法示例：
    python run.py open https://example.com --headed
    python run.py -s my_session click e15
    python run.py --chain "open https://example.com; screenshot --filename out.png"
"""

import argparse
import os
import subprocess
import sys
import shlex
from typing import List, Optional


def print_help():
    print("playwright-cli 自动化工具")
    print("参数或格式错误，请检查参数")
    dir_path = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(dir_path, "tool.md"), "r", encoding="utf-8") as f:
        print(f.read())


def run_playwright_command(args: List[str]) -> bool:
    """执行 playwright-cli 命令，参数列表已包含可执行文件及所有选项"""
    if os.name == "nt":
        cmd = ["playwright-cli.cmd"] + args
    else:
        cmd = ["playwright-cli"] + args
    try:
        print(f"  🎬 执行命令: {' '.join(cmd)}")
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
        if result.returncode == 0:
            if result.stdout:
                print(result.stdout)
            return True
        else:
            print(f"❌ 命令执行失败 (退出码 {result.returncode})")
            print_help()
            if result.stderr:
                print(f"stderr: {result.stderr}")
            return False
    except FileNotFoundError:
        print(
            "❌ 未找到 playwright-cli 命令，请确保已安装: npm install -g @playwright/cli@latest"
        )
        return False
    except subprocess.TimeoutExpired:
        print("❌ 命令执行超时")
        return False
    except Exception as e:
        print(f"❌ 发生未知错误: {e}")
        return False


def execute_chain(chain_str: str, session: Optional[str] = None) -> bool:
    """
    执行命令链，按分号分隔多个命令，依次执行。
    如果指定了 session 且某条命令未显式包含 -s 或 --session，则自动插入。
    """
    parts = chain_str.split(";")
    commands = [p.strip() for p in parts if p.strip()]
    if not commands:
        print("❌ 命令链为空")
        return False

    print(f"🚀 开始执行命令链，共 {len(commands)} 个命令\n")
    for idx, cmd_str in enumerate(commands, 1):
        print(f"📌 命令 {idx}: {cmd_str}")
        try:
            tokens = shlex.split(cmd_str)
        except ValueError as e:
            print(f"❌ 解析命令失败: {e}\n命令: {cmd_str}")
            return False

        # 如果指定了 session 且当前命令未使用 -s 或 --session，则插入
        if session is not None:
            has_session = any(t in ("-s", "--session") for t in tokens)
            if not has_session:
                tokens = ["-s", session] + tokens

        success = run_playwright_command(tokens)
        if not success:
            print(f"❌ 命令 {idx} 执行失败，终止执行")
            return False
        print()
    print("✅ 命令链执行完成")
    return True


def main():
    parser = argparse.ArgumentParser(
        description="playwright-cli 自动化工具",
        add_help=False,
    )
    parser.add_argument("-s", "--session", help="会话名称（自动添加 -s 参数）")
    parser.add_argument("--chain", help="命令链字符串，多个命令用分号分隔")
    parser.add_argument("-h", "--help", action="store_true", help="显示帮助信息")

    # 解析已知参数，其余参数透传给 playwright-cli
    args, remaining = parser.parse_known_args()

    if args.help:
        print_help()
        sys.exit(0)

    # 命令链模式优先
    if args.chain:
        success = execute_chain(args.chain, session=args.session)
        sys.exit(0 if success else 1)

    # 单命令模式：透传剩余参数
    if not remaining:
        print_help()
        sys.exit(-1)

    # 如果指定了 session，插入到命令最前面
    if args.session:
        final_args = ["-s", args.session] + remaining
    else:
        final_args = remaining

    success = run_playwright_command(final_args)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
