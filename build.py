#!/usr/bin/env python3
"""直接用 g++ 编译（不依赖 CMake），方便本地快速验证。

用法：
    python build.py            # 调试构建，产物 bin/agentcpp[.exe]
    python build.py -r         # Release（-O2）
    python build.py -c         # 只做语法检查（不产出二进制）

构建产物只有 bin/ 一个可执行文件：WebUI、默认工作区、WebView2 加载器
全部在编译期打包进 exe，不需要任何同目录文件。

依赖：g++（C++20）、libcurl、libsodium、nlohmann/json
  Windows(msys2): pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-curl \
                                mingw-w64-ucrt-x86_64-libsodium mingw-w64-ucrt-x86_64-nlohmann-json
  macOS:         brew install curl libsodium nlohmann-json
  Debian/Ubuntu: sudo apt install g++ libcurl4-openssl-dev libsodium-dev nlohmann-json3-dev
"""
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(ROOT, "bin")
OUT = os.path.join(OUT_DIR, "agentcpp" + (".exe" if os.name == "nt" else ""))

SOURCES = sorted(glob.glob(os.path.join(ROOT, "src", "**", "*.cpp"), recursive=True))

# (源目录, 输出头, 命名空间, 前缀, 首页, 扩展名)
EMBEDS = [
    ("src/templates/webui", "src/server/webui_assets.hpp",
     "webui::embedded", "AGENTCPP_SERVER_WEBUI_ASSETS_HPP", "", "index.html"),
    ("src/templates/workspace", "src/core/seed_assets.hpp",
     "config::seed", "AGENTCPP_CORE_SEED_ASSETS_HPP", "workspace/", ""),
    ("third_party/webview2/x64", "src/desktop/loader_assets.hpp",
     "desktop::embedded", "AGENTCPP_DESKTOP_LOADER_ASSETS_HPP", "", ""),
]


def embed_all():
    """把界面 / 默认工作区 / WebView2 加载器打包成 C++ 头文件（与 CMake 构建一致）"""
    tool = os.path.join(ROOT, "tools", "embed_assets.py")
    if not os.path.exists(tool):
        return
    for src, out, ns, guard, prefix, index in EMBEDS:
        src_dir = os.path.join(ROOT, src)
        if not os.path.isdir(src_dir):
            continue
        cmd = [sys.executable, tool,
               "--src", src_dir,
               "--out", os.path.join(ROOT, out),
               "--ns", ns, "--guard", guard]
        if prefix:
            cmd += ["--prefix", prefix]
        if index:
            cmd += ["--index", index]
        subprocess.run(cmd, check=False, stdout=subprocess.DEVNULL)


def main():
    args = sys.argv[1:]
    release = "-r" in args or "--release" in args
    check_only = "-c" in args or "--check" in args

    if not check_only:
        embed_all()
        os.makedirs(OUT_DIR, exist_ok=True)

    cmd = [
        "g++",
        "-std=c++20",
        "-finput-charset=UTF-8",
        "-fexec-charset=UTF-8",
        "-Wall",
        "-Wextra",
        "-Wno-unused-parameter",
        "-Wno-missing-field-initializers",
        "-I", os.path.join(ROOT, "src"),
        *SOURCES,
    ]
    if release:
        cmd += ["-O2", "-DNDEBUG"]
    else:
        # 注意：-O0 会让 GCC 为每个 inline 函数都保留一份 COMDAT 节，
        # 单目标文件节数会突破 MinGW ld 的承受上限（链接静默失败）。
        # 因此调试构建用 -O1 -g。
        cmd += ["-g", "-O1"]

    if check_only:
        cmd += ["-fsyntax-only"]
    else:
        cmd += ["-o", OUT]

    if os.name == "nt":
        # MinGW 的 GNU ld 在目标文件节数较多时会静默失败（ld returned 5/1 且无输出），
        # 有 lld 就优先用它
        import shutil as _sh
        if _sh.which("ld.lld"):
            cmd += ["-fuse-ld=lld"]
        cmd += ["-I", os.path.join(ROOT, "third_party", "webview2", "include")]
        cmd += ["-lcurl", "-lsodium", "-lws2_32", "-lwsock32", "-lole32", "-luuid", "-lshell32", "-luser32", "-lgdi32"]
    else:
        cmd += ["-lcurl", "-lsodium", "-lpthread"]

    print("building %d source files..." % len(SOURCES))
    proc = subprocess.run(cmd)
    if proc.returncode != 0:
        print("build failed")
        return proc.returncode
    if not check_only:
        print("built:", OUT)
    return 0


if __name__ == "__main__":
    sys.exit(main())
