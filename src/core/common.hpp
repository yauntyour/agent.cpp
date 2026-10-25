// ============================================================================
//  core/common.hpp —— 公共头文件聚合
//
//  所有模块共用的标准库 / 第三方头集中在这里，保证每个模块只需一行
//  #include "core/common.hpp" 即可获得一致的编译环境，避免各文件各自
//  维护一份易错的 include 列表。
//
//  第三方依赖只有三个，且都可以静态链接（见 CMakeLists.txt）：
//    - libcurl    网络传输
//    - libsodium  加密（Argon2id / SHA3 / secretbox）
//    - nlohmann_json  JSON
// ============================================================================
#pragma once
#ifndef AGENTCPP_CORE_COMMON_HPP
#define AGENTCPP_CORE_COMMON_HPP

// ---- 标准库 ----------------------------------------------------------------
#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// ---- 第三方 ----------------------------------------------------------------
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <sodium.h>

// ---- 平台层 ----------------------------------------------------------------
#ifdef _WIN32
// winsock2.h 必须先于 windows.h，否则会与 winsock.h 冲突
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <process.h>
#include <direct.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <sys/sysctl.h>
#endif
#endif

// 全局别名：与旧代码保持一致的书写习惯
namespace fs = std::filesystem;
using json = nlohmann::json;

namespace agentcpp
{
    // 版本号（--version 与日志使用）
    constexpr const char *kVersion = "2.0.0";
} // namespace agentcpp

#endif // AGENTCPP_CORE_COMMON_HPP
