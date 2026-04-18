import os
import sys
import json
import subprocess


def main():
    """加载 tools/tools.json，返回启用的工具名集合"""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    tools_dir = os.path.join(os.path.dirname(script_dir), "tools")
    config_path = os.path.join(tools_dir, "tools.json")

    if not os.path.exists(config_path):
        print(json.dumps({"error": "tools.json not found"}))
        sys.exit(1)
    print(
        "Before using any additional tools, please search for information about them\n"
    )

    try:
        with open(config_path, "r", encoding="utf-8") as f:
            tools_list = json.load(f)
    except Exception as e:
        print(json.dumps({"error": f"failed to parse tools.json: {str(e)}"}))
        sys.exit(1)
    if len(sys.argv) < 2:
        print(tools_list)
        sys.exit(0)
    tool_name = sys.argv[1]
    enabled_tools = {tool["name"] for tool in tools_list if tool.get("enabled", False)}
    if tool_name not in enabled_tools:
        print(
            json.dumps(
                {"error": f"tool '{tool_name}' is not enabled or not registered"}
            )
        )
        sys.exit(1)
    for tool in tools_list:
        if tool["name"] == tool_name:
            with open(
                tools_dir + "/" + tool_name + "/tool.md", mode="r", encoding="utf-8"
            ) as f:
                print(f.read())
                sys.exit(0)


if __name__ == "__main__":
    main()
