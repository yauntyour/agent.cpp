#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Tool Manager – Reads tools/tools.json and returns info about enabled tools.
Can be used as a script or imported as a module.
"""

import os
import json
import sys
from typing import List, Dict, Optional


class ToolManager:
    def __init__(self, tools_dir: Optional[str] = None):
        """
        :param tools_dir: Path to the directory containing tools.json.
                          Defaults to <package_root>/tools relative to this file.
        """
        if tools_dir is None:
            script_dir = os.path.dirname(os.path.abspath(__file__))
            self.tools_dir = os.path.join(os.path.dirname(script_dir), "tools")
        else:
            self.tools_dir = tools_dir

        self.config_path = os.path.join(self.tools_dir, "tools.json")
        self._tools: List[Dict] = []

    def load_config(self) -> List[Dict]:
        """Load tools.json and return list of tools. Raises exceptions on failure."""
        if not os.path.exists(self.config_path):
            raise FileNotFoundError(f"Configuration file not found: {self.config_path}")

        with open(self.config_path, "r", encoding="utf-8") as f:
            self._tools = json.load(f)
        return self._tools

    def enabled_tool_names(self) -> set:
        """Return a set of names of enabled tools."""
        if not self._tools:
            self.load_config()
        return {tool["name"] for tool in self._tools if tool.get("enabled", False)}

    def get_tool_info(self, tool_name: str) -> str:
        """Return the Markdown description of an enabled tool.
        :param tool_name: Name of the tool.
        :returns: Contents of tool.md as a string.
        :raises ValueError: if tool not found or not enabled.
        :raises FileNotFoundError: if tool.md missing.
        """
        # Ensure config loaded
        if tool_name not in self.enabled_tool_names():
            raise ValueError(f"Tool '{tool_name}' is not enabled or not registered")

        tool_md_path = os.path.join(self.tools_dir, tool_name, "tool.md")
        if not os.path.exists(tool_md_path):
            raise FileNotFoundError(f"Description file not found: {tool_md_path}")

        with open(tool_md_path, "r", encoding="utf-8") as f:
            return f.read()

    def list_tools(self, enabled_only: bool = True) -> List[Dict]:
        """Return list of tools (full dict). If enabled_only, filter."""
        if not self._tools:
            self.load_config()
        if enabled_only:
            return [t for t in self._tools if t.get("enabled", False)]
        return self._tools


def main():
    """Command-line entry point (compatible with original behaviour)."""
    manager = ToolManager()
    try:
        manager.load_config()
    except Exception as e:
        print(json.dumps({"error": str(e)}))
        sys.exit(1)

    if len(sys.argv) < 2:
        # List all tools as JSON (original behaviour)
        print(json.dumps(manager.list_tools(enabled_only=False), indent=2))
        sys.exit(0)

    tool_name = sys.argv[1]
    try:
        description = manager.get_tool_info(tool_name)
        print(description)
    except ValueError as e:
        print(json.dumps({"error": str(e)}))
        sys.exit(1)
    except FileNotFoundError as e:
        print(json.dumps({"error": f"Missing tool.md: {str(e)}"}))
        sys.exit(1)


if __name__ == "__main__":
    main()
