#!/usr/bin/env bash
# 给 macOS job 挑一个真能 #include <generator> 的 C++ 工具链。
#
# 为什么不能「换 Xcode」或「换 brew llvm」了事（两条都实测失败过）：
#
#   1. 镜像里 Xcode 26.6 的 clang 21 直接报 `'generator' file not found`
#      —— 它默认那份 libc++ 里就是没有这个头，换 -std 也没用。
#   2. `brew install llvm` 只装 libc++ 的**库**（lib/c++、lib/unwind），
#      **不装头文件**：homebrew 的 llvm.rb 里只有 LIBCXX_INSTALL_LIBRARY_DIR，
#      它自己的测试也是拿 `$(xcrun --show-sdk-path)/usr/include/c++/v1` 当头的
#      （见 formula 的 "These tests should ignore the usual SDK includes" 那段）。
#      所以 brew 的 clang++ 默认还是读系统 SDK 那份头；而 `-nostdinc++` 加一个
#      不存在的 include/c++/v1，等于把头文件全掐了 —— 报错一模一样。
#
# 于是这里换个思路：先在机器上**找**哪份 libc++ 真的带 generator，找到就用；
# 一个都没有才退到 brew gcc（libstdc++ 从 14 起就有 <generator>，稳）。
# 全都不行就把诊断信息全打出来再失败，别再让人猜。
#
# 输出（写进 $GITHUB_OUTPUT）：cxx / cxxflags / ldflags

set -uo pipefail

say()   { printf '%s\n' "$*"; }
oerr()  { printf '::error::%s\n' "$*"; }
owarn() { printf '::warning::%s\n' "$*"; }

PROBE_SRC='#include <generator>
#include <expected>
#include <format>
int main(){}
'

# probe <cxx> [flags...] —— 成功返回 0；失败把编译器原话留在 /tmp/probe.log
probe() {
    local cxx="$1"; shift
    printf '%s\n' "$PROBE_SRC" \
        | "$cxx" -std=c++26 -x c++ -fsyntax-only "$@" - >/tmp/probe.log 2>&1
}

show_probe() {
    say "---- $1 的原话 ----"
    sed -e 's/^/  | /' /tmp/probe.log | head -20
    say "-------------------"
}

# clang 的 -v 搜索列表（走 stderr），用来证明「到底读的哪份头」
search_list() {
    local cxx="$1"; shift
    printf '\n' | "$cxx" -std=c++26 -x c++ -E -v "$@" - 2>&1 >/dev/null \
        | sed -n '/^#include <...> search starts here:/,/^End of search list/p'
}

# 收集所有「真的含 generator」的 libc++ 头目录，顺序即优先级
CANDIDATES=()
add_cand() {
    [ -n "${1:-}" ] && [ -f "$1/generator" ] && CANDIDATES+=("$1")
    return 0
}

collect_candidates() {
    local llvm="${1:-}" tc sdk f d out seen
    tc="$(xcode-select -p 2>/dev/null)"
    sdk="$(xcrun --show-sdk-path 2>/dev/null)"
    CANDIDATES=()

    # brew llvm：新版本可能带、也可能不带头文件，按名字找一遍最省事
    if [ -n "$llvm" ]; then
        add_cand "$llvm/include/c++/v1"
        while IFS= read -r f; do add_cand "$(dirname "$f")"; done \
            < <(find "$llvm" -maxdepth 6 -type f -name generator -path '*/c++/v1/*' 2>/dev/null)
    fi

    # 系统侧：Xcode 工具链 / SDK / CLT（这几种布局历史上都出现过）
    add_cand "$tc/Toolchains/XcodeDefault.xctoolchain/usr/include/c++/v1"
    add_cand "$sdk/usr/include/c++/v1"
    add_cand "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1"
    add_cand "/Library/Developer/CommandLineTools/usr/include/c++/v1"
    add_cand "/opt/homebrew/include/c++/v1"
    add_cand "/usr/local/include/c++/v1"

    # 去重
    out=(); seen=" "
    for d in "${CANDIDATES[@]:-}"; do
        [ -n "$d" ] || continue
        case "$seen" in *" $d "*) continue ;; esac
        seen="$seen$d "
        out+=("$d")
    done
    CANDIDATES=("${out[@]:-}")
}

diagnose() {
    local tc sdk d n
    tc="$(xcode-select -p 2>/dev/null)"
    sdk="$(xcrun --show-sdk-path 2>/dev/null)"
    say "================ 诊断 ================"
    say "xcode-select -p : ${tc:-<无>}"
    say "xcodebuild      : $(xcodebuild -version 2>/dev/null | tr '\n' ' ')"
    say "show-sdk-path   : ${sdk:-<无>}"
    say "-- 关键头目录 --"
    for d in \
        "$tc/Toolchains/XcodeDefault.xctoolchain/usr/include/c++/v1" \
        "$sdk/usr/include/c++/v1" \
        "/Library/Developer/CommandLineTools/usr/include/c++/v1" \
        "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/c++/v1"; do
        if [ -d "$d" ]; then
            n="$(ls -1 "$d" 2>/dev/null | wc -l | tr -d ' ')"
            say "  [有] $d  ($n 项, generator: $([ -f "$d/generator" ] && echo yes || echo no))"
        else
            say "  [无] $d"
        fi
    done
    say "-- 全盘找 generator（限深） --"
    for d in "$tc" "$sdk" "/Library/Developer/CommandLineTools" /opt/homebrew /usr/local; do
        [ -n "$d" ] && [ -d "$d" ] || continue
        find "$d" -maxdepth 7 -type f -name generator -path '*/c++/v1/*' 2>/dev/null | head -5
    done
    say "-- 默认 clang++ 的 C++ 头搜索路径 --"
    search_list "$(xcrun -f clang++ 2>/dev/null || echo clang++)"
    say "====================================="
}

CHOSEN_CXX=""; CHOSEN_FLAGS=""; CHOSEN_LD=""

# try_with <cxx> <llvm-prefix 或空> —— 拿本机找到的头目录挨个试
try_with() {
    local cxx="$1" llvm="${2:-}" d flags ld
    collect_candidates "$llvm"
    say "本机含 <generator> 的 libc++ 头目录：${CANDIDATES[*]:-<无>}"
    for d in "${CANDIDATES[@]:-}"; do
        [ -n "$d" ] || continue
        flags="-nostdinc++ -isystem $d"
        ld=""
        if [ -n "$llvm" ]; then
            case "$d" in
                "$llvm"/*)
                    # 用 brew 自己的头，就必须配 brew 自己的库（版本要一致）
                    flags="$flags -D_LIBCPP_DISABLE_AVAILABILITY"
                    ld="-L$llvm/lib/c++ -L$llvm/lib/unwind -lunwind"
                    ld="$ld -Wl,-rpath,$llvm/lib/c++ -Wl,-rpath,$llvm/lib/unwind"
                    ;;
            esac
        fi
        if probe "$cxx" $flags; then
            say "✓ $cxx + 头目录 $d"
            CHOSEN_CXX="$cxx"; CHOSEN_FLAGS="$flags"; CHOSEN_LD="$ld"
            return 0
        fi
    done
    return 1
}

say "Xcode: $(xcodebuild -version 2>/dev/null | tr '\n' ' ')"
xcxx="$(xcrun -f clang++ 2>/dev/null || true)"
if [ -n "$xcxx" ]; then
    say "default: $("$xcxx" --version 2>/dev/null | head -1)"
fi

# 0) Xcode 默认就能过 —— 最好的情况
if [ -n "$xcxx" ] && probe "$xcxx"; then
    say "✓ Xcode 的 clang++ 直接可用"
    CHOSEN_CXX="$xcxx"
fi

# 1) Xcode 的 clang + 本机任意一份带 generator 的 libc++ 头
if [ -z "$CHOSEN_CXX" ] && [ -n "$xcxx" ]; then
    show_probe "Xcode 的 clang++"
    say "… 找一份带 <generator> 的 libc++ 头配 Xcode 的 clang++"
    try_with "$xcxx" "" || true
fi

# 2) brew llvm（它没头文件，所以仍然要靠上一步那种头目录）
if [ -z "$CHOSEN_CXX" ]; then
    say "… 换 brew llvm"
    brew install llvm >/dev/null 2>&1 || owarn "brew install llvm 失败"
    llvm="$(brew --prefix llvm 2>/dev/null || true)"
    lcxx="$llvm/bin/clang++"
    if [ -x "$lcxx" ]; then
        say "brew clang++: $("$lcxx" --version 2>/dev/null | head -1)"
        try_with "$lcxx" "$llvm" || true
    fi
fi

# 3) brew gcc —— libstdc++ 14+ 一定有 <generator>，且完全不碰 libc++
if [ -z "$CHOSEN_CXX" ]; then
    say "… 换 brew gcc（libstdc++ 一定有 <generator>）"
    brew install gcc >/dev/null 2>&1 || owarn "brew install gcc 失败"
    gprefix="$(brew --prefix gcc 2>/dev/null || true)"
    gxx="$(ls -1 "$gprefix"/bin/g++-* 2>/dev/null | sort | tail -1)"
    if [ -n "$gxx" ] && probe "$gxx"; then
        say "✓ $("$gxx" --version 2>/dev/null | head -1)"
        CHOSEN_CXX="$gxx"
        # brew gcc 的 libstdc++ 在 lib/gcc/<ver>/ 下，不在默认搜索路径里：
        # 补一条 rpath，否则 Smoke 那步 dyld 找不到 libstdc++.6.dylib
        glib="$(dirname "$(find "$gprefix" -name 'libstdc++.6.dylib' 2>/dev/null | head -1)")"
        if [ -n "$glib" ] && [ -d "$glib" ]; then
            CHOSEN_LD="-Wl,-rpath,$glib"
        fi
    elif [ -n "$gxx" ]; then
        show_probe "$gxx"
    fi
fi

if [ -z "$CHOSEN_CXX" ]; then
    diagnose
    oerr "找不到能 #include <generator> 的 C++ 工具链（Xcode / brew llvm / brew gcc 都试过了）"
    exit 1
fi

say "libc++/libstdc++ 头搜索路径："
search_list "$CHOSEN_CXX" $CHOSEN_FLAGS
say "cxxflags: ${CHOSEN_FLAGS:-<空>}"
say "ldflags : ${CHOSEN_LD:-<空>}"

{
    echo "cxx=$CHOSEN_CXX"
    echo "cxxflags=$CHOSEN_FLAGS"
    echo "ldflags=$CHOSEN_LD"
} >> "${GITHUB_OUTPUT:?GITHUB_OUTPUT 没设}"
