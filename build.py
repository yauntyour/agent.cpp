import subprocess

cmd = [
    "g++",
    "-fdiagnostics-color=always",
    "-finput-charset=UTF-8",
    "-fexec-charset=UTF-8",
    "-g",
    "app.cpp",
    "-o",
    "app",
    "-lcurl",
    "-lws2_32",
    "-lwsock32",
    "-std=c++26",
]
subprocess.run(cmd, check=True, capture_output=True, text=True)
