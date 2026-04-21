import subprocess

cmd = [
    "g++",
    "-fdiagnostics-color=always",
    "-finput-charset=UTF-8",
    "-fexec-charset=UTF-8",
    "-g",
    "webui.cpp",
    "-o",
    "webui",
    "-I",
    "webui/include/",
    "-L",
    "webui/",
    "-lwebui-2",
    "-lcurl",
    "-lws2_32",
    "-lwsock32",
    "-std=c++26",
]
subprocess.run(cmd, check=True, capture_output=True, text=True)
