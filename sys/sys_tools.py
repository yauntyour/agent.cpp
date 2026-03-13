import os
import json
import sys
import re

def main():
    # 检查命令行参数
    if len(sys.argv) < 2:
        print(json.dumps({"error": "missing tool name"}))
        return
    
    tool_name = sys.argv[1]
    
    # 验证工具名称安全性（只允许字母、数字、下划线、连字符）
    if not re.match(r'^[a-zA-Z0-9_-]+$', tool_name):
        print(json.dumps({"error": "invalid tool name"}))
        return
    
    # 计算tools目录路径（与sys目录同级）
    # 假设当前脚本在sys目录中
    current_script_dir = os.path.dirname(os.path.abspath(__file__))
    sys_dir = current_script_dir
    tools_dir = os.path.join(os.path.dirname(sys_dir), 'tools')
    
    # 检查tools目录是否存在
    if not os.path.exists(tools_dir):
        print(json.dumps({"error": "tools directory not found"}))
        return
    
    # 构建工具路径
    tool_path = os.path.join(tools_dir, tool_name)
    
    # 检查工具目录是否存在
    if not os.path.isdir(tool_path):
        print(json.dumps({"error": "tool not found"}))
        return
    
    # 构建tool.md和run.py的路径
    tool_md_path = os.path.join(tool_path, 'tool.md')
    run_py_path = os.path.join(tool_path, 'run.py')
    
    # 读取tool.md内容
    docx_content = ""
    if os.path.exists(tool_md_path):
        try:
            with open(tool_md_path, 'r', encoding='utf-8') as f:
                docx_content = f.read()
        except Exception as e:
            print(json.dumps({"error": f"failed to read tool.md: {str(e)}"}))
            return
    else:
        # tool.md不存在，但工具目录存在，可以接受空文档
        docx_content = ""
    
    # 检查run.py是否存在
    if not os.path.exists(run_py_path):
        print(json.dumps({"error": "run.py not found in tool directory"}))
        return
    
    # 生成run.py相对于当前工作目录的路径
    try:
        current_working_dir = os.getcwd()
        relative_run_path = os.path.relpath(run_py_path, current_working_dir)
    except Exception as e:
        print(json.dumps({"error": f"failed to generate relative path: {str(e)}"}))
        return
    
    # 返回成功结果
    result = {
        "docx": docx_content,
        "path": relative_run_path
    }
    
    print(json.dumps(result, ensure_ascii=False))

if __name__ == "__main__":
    main()