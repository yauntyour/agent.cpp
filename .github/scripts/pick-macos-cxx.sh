#!/usr/bin/env bash
# 给 macOS job 挑一个能编 C++26 的 clang。
#
# 这里**故意不**管 <generator> / std::move_only_function —— libc++ 两个都没有
# （系统那份和 Homebrew LLVM 那份，查到 LLVM 23），由 CMakeLists.txt 里的
# check_include_file_cxx 把 third_party/compat 挂到 include 路径上，compat 里的头
# 用 #include_next 转发，有真实现就自动让位。判定跟着构建走，不跟着 CI 脚本走，
# 本机 cmake 也得到同样结果。
#
# 脚本只剩一件事：在镜像里找到一个真能跑 C++26 的编译器（协程 / <format> /
# <expected> / <span>）。找不到就打印诊断并失败。
#
# 输出（写进 $GITHUB_OUTPUT）：cxx

set -uo pipefail

say()  { printf '%s\n' "$*"; }
oerr() { printf '::error::%s\n' "$*"; }
owarn(){ printf '::warning::%s\n' "$*"; }

PROBE_SRC='#include <coroutine>
#include <expected>
#include <format>
#include <span>

struct probe_task
{
    struct promise_type
    {
        probe_task get_return_object() { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() {}
    };
};

probe_task probe_coro() { co_return; }

int main()
{
    std::expected<int, int> e = 1;
    std::span<const char> s;
    auto text = std::format("{}", e.value());
    (void)text;
    (void)s;
}
'

# probe <cxx> —— 成功返回 0；失败把编译器原话留在 /tmp/probe.log
probe() {
    printf '%s\n' "$PROBE_SRC" \
        | "$1" -std=c++26 -x c++ -fsyntax-only - >/tmp/probe.log 2>&1
}

show_probe() {
    say "---- $1 的原话 ----"
    sed -e 's/^/  | /' /tmp/probe.log | head -20
    say "-------------------"
}

diagnose() {
    say "================ 诊断 ================"
    say "xcode-select -p : $(xcode-select -p 2>/dev/null || echo '<无>')"
    say "xcodebuild      : $(xcodebuild -version 2>/dev/null | tr '\n' ' ')"
    say "show-sdk-path   : $(xcrun --show-sdk-path 2>/dev/null || echo '<无>')"
    say "clang++         : $(xcrun -f clang++ 2>/dev/null || echo '<无>')"
    say "====================================="
}

CHOSEN=""

say "Xcode: $(xcodebuild -version 2>/dev/null | tr '\n' ' ')"
xcxx="$(xcrun -f clang++ 2>/dev/null || true)"
if [ -n "$xcxx" ]; then
    say "default: $("$xcxx" --version 2>/dev/null | head -1)"
    if probe "$xcxx"; then
        say "✓ Xcode 的 clang++ 可用"
        CHOSEN="$xcxx"
    else
        show_probe "Xcode 的 clang++"
    fi
fi

# xcrun 不可用或镜像自带的 clang 太旧时，退到 brew llvm
if [ -z "$CHOSEN" ]; then
    say "… 换 brew llvm 的 clang++"
    brew install llvm >/dev/null 2>&1 || owarn "brew install llvm 失败"
    lcxx="$(brew --prefix llvm 2>/dev/null || true)/bin/clang++"
    if [ -x "$lcxx" ]; then
        say "brew clang++: $("$lcxx" --version 2>/dev/null | head -1)"
        if probe "$lcxx"; then
            say "✓ brew 的 clang++ 可用"
            CHOSEN="$lcxx"
        else
            show_probe "brew 的 clang++"
        fi
    fi
fi

if [ -z "$CHOSEN" ]; then
    diagnose
    oerr "找不到能编 C++26 的 clang（协程 / <format> / <expected> / <span>）"
    exit 1
fi

echo "cxx=$CHOSEN" >> "${GITHUB_OUTPUT:?GITHUB_OUTPUT 没设}"
