cmake_minimum_required(VERSION 3.20)
project(agentcpp LANGUAGES CXX VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# 依赖策略
#   STATIC_BUILD=ON （默认，发布产物）
#       - Linux   : 全静态 musl 二进制（CI 用 Alpine 容器）
#       - Windows : 全静态 MinGW exe（无任何 DLL）
#       - macOS   : 静态链接 curl / libsodium，仅用系统框架
#   STATIC_BUILD=OFF（本地开发）
#       - 链接系统已安装的 curl / libsodium
#
#   第三方只剩三个：libcurl、libsodium、nlohmann_json。
#   原先的 Boost.Asio 与 servic.cpp 子模块已由自带的 src/server/http_server
#   取代，因此不再需要 Boost。
# ============================================================
option(STATIC_BUILD "Build a fully static, zero-runtime-dependency executable" ON)

# 与 build.py 保持一致：可执行文件统一输出到源码树的 bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# MinGW 的 GNU ld 在目标文件节数较多时会静默失败（ld returned 5，无任何报错、
# 产出截断的 exe），有 lld 就优先用它。
if(MINGW)
    find_program(LD_LLD ld.lld)
    if(LD_LLD)
        add_link_options(-fuse-ld=lld)
    endif()
endif()

# ============================================================
# WebView2（Windows 桌面外壳）
#   头文件随仓库在 third_party/webview2/include，运行期动态加载
#   third_party/webview2/x64/WebView2Loader.dll，不需要导入库。
# ============================================================
set(WEBVIEW2_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/webview2)

# ============================================================
# 内嵌资源：模板目录 + WebView2Loader.dll → C++ 头文件
#   src/templates/webui      → src/server/webui_assets.hpp   （HTTP 静态资源）
#   src/templates/workspace  → src/core/seed_assets.hpp      （首次启动写盘）
#   third_party/webview2/x64 → src/desktop/loader_assets.hpp （免同目录 DLL）
# 改前端 / 模板后重新 build 即可；也可手动运行：
#   python tools/embed_assets.py --src ... --out ... --ns ... --guard ...
# ============================================================
find_package(Python3 QUIET COMPONENTS Interpreter)
set(EMBED_TOOL ${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_assets.py)
set(WEBUI_ASSETS_HPP ${CMAKE_CURRENT_SOURCE_DIR}/src/server/webui_assets.hpp)
set(SEED_ASSETS_HPP ${CMAKE_CURRENT_SOURCE_DIR}/src/core/seed_assets.hpp)
set(LOADER_ASSETS_HPP ${CMAKE_CURRENT_SOURCE_DIR}/src/desktop/loader_assets.hpp)
if(Python3_Interpreter_FOUND AND EXISTS ${EMBED_TOOL})
    file(GLOB_RECURSE EMBED_SOURCES CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/src/templates/webui/*
        ${CMAKE_CURRENT_SOURCE_DIR}/src/templates/workspace/*)
    add_custom_command(
        OUTPUT ${WEBUI_ASSETS_HPP} ${SEED_ASSETS_HPP} ${LOADER_ASSETS_HPP}
        COMMAND ${Python3_EXECUTABLE} ${EMBED_TOOL}
                --src ${CMAKE_CURRENT_SOURCE_DIR}/src/templates/webui
                --out ${WEBUI_ASSETS_HPP}
                --ns webui::embedded --guard AGENTCPP_SERVER_WEBUI_ASSETS_HPP --index index.html
        COMMAND ${Python3_EXECUTABLE} ${EMBED_TOOL}
                --src ${CMAKE_CURRENT_SOURCE_DIR}/src/templates/workspace
                --out ${SEED_ASSETS_HPP} --prefix workspace/
                --ns config::seed --guard AGENTCPP_CORE_SEED_ASSETS_HPP
        COMMAND ${Python3_EXECUTABLE} ${EMBED_TOOL}
                --src ${WEBVIEW2_DIR}/x64
                --out ${LOADER_ASSETS_HPP}
                --ns desktop::embedded --guard AGENTCPP_DESKTOP_LOADER_ASSETS_HPP
        DEPENDS ${EMBED_TOOL} ${EMBED_SOURCES} ${WEBVIEW2_DIR}/x64/WebView2Loader.dll
        COMMENT "embedding webui / workspace templates / WebView2 loader"
        VERBATIM)
    add_custom_target(embed_assets DEPENDS ${WEBUI_ASSETS_HPP} ${SEED_ASSETS_HPP} ${LOADER_ASSETS_HPP})
else()
    add_custom_target(embed_assets)
    message(WARNING "python3 not found: 请手动运行 python tools/embed_assets.py 生成内嵌资源")
endif()

include(FetchContent)

# ---------- nlohmann_json (header-only) ----------
# 优先用系统包，找不到再从源码拉（离线环境可先装 nlohmann-json3-dev）
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# ---------- libcurl ----------
if(STATIC_BUILD)
    if(NOT APPLE)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    endif()

    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(CURL_ENABLE_EXPORT_TARGET OFF CACHE BOOL "" FORCE)
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(CURL_BROTLI OFF CACHE STRING "" FORCE)
    set(CURL_ZSTD OFF CACHE STRING "" FORCE)
    set(CURL_ZLIB OFF CACHE STRING "" FORCE)
    set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH OFF CACHE BOOL "" FORCE)
    set(CURL_USE_GSASL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_GSSAPI OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBUV OFF CACHE BOOL "" FORCE)
    set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
    set(ENABLE_ARES OFF CACHE BOOL "" FORCE)
    if(APPLE)
        set(CURL_USE_SECTRANSP ON CACHE BOOL "" FORCE)
    elseif(WIN32)
        set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    else()
        set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
        set(CURL_ZLIB ON CACHE STRING "" FORCE)
    endif()
    FetchContent_Declare(
        curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG curl-8_12_1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(curl)

    set(CURL_EXTRA_LIBS "")
    if(APPLE)
        set(CURL_EXTRA_LIBS
            "-framework Security"
            "-framework CoreFoundation"
            "-framework CoreServices"
            "-framework SystemConfiguration"
        )
    elseif(WIN32)
        set(CURL_EXTRA_LIBS bcrypt crypt32 advapi32 secur32 iphlpapi)
    else()
        set(CURL_EXTRA_LIBS ssl crypto z)
    endif()
else()
    find_package(CURL REQUIRED)
endif()

# ---------- libsodium ----------
if(STATIC_BUILD)
    set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        libsodium
        GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
        GIT_TAG 9b2848dfc1b917a9410f0de9d81059b26cbfaa8d
        GIT_SUBMODULES libsodium
    )
    FetchContent_MakeAvailable(libsodium)
    set(SODIUM_LIBRARIES "")
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SODIUM QUIET libsodium)
    endif()
    if(NOT SODIUM_FOUND)
        find_package(Sodium CONFIG QUIET)
    endif()

    set(_sodium_hints
        ${SODIUM_LIBRARY_DIRS}
        ${MSYS2_DIR}/mingw64/lib
        /mingw64/lib
        /opt/homebrew/lib
        /usr/local/lib
        /usr/lib
        /usr/lib/x86_64-linux-gnu
    )
    find_library(SODIUM_LIBRARY sodium HINTS ${_sodium_hints})
    set(_sodium_include_hints
        ${SODIUM_INCLUDE_DIRS}
        ${MSYS2_DIR}/mingw64/include
        /mingw64/include
        /opt/homebrew/include
        /usr/local/include
        /usr/include
    )
    find_path(SODIUM_INCLUDE_DIR sodium.h HINTS ${_sodium_include_hints})

    if(SODIUM_LIBRARY AND SODIUM_INCLUDE_DIR)
        set(SODIUM_INCLUDE_DIRS ${SODIUM_INCLUDE_DIR})
        set(SODIUM_LIBRARIES ${SODIUM_LIBRARY})
        message(STATUS "Found libsodium: ${SODIUM_LIBRARY}")
    else()
        message(FATAL_ERROR "libsodium not found, install it via your package manager (brew/apt/pacman)")
    endif()
endif()

# ============================================================
# 源文件（按模块划分）
# ============================================================
set(AGENT_SOURCES
    src/main.cpp

    # core：路径 / 配置 / 日志 / 加密 / 文本 / 系统信息
    src/core/paths.cpp
    src/core/config.cpp
    src/core/log.cpp

    # net：HTTP 客户端与三家 LLM 供应商客户端
    # （net/http_client.hpp、net/llm.hpp 为 header-only）

    # agent：工具 / 权限 / 指令 / 会话 / 记忆 / 主循环 / 供应商
    src/agent/providers.cpp
    src/agent/memory.cpp
    src/agent/runtime.cpp

    # channels：频道驱动注册表（telegram / wechat / qq 为 header-only 驱动）
    src/channels/manager.cpp

    # desktop：Windows WebView2 桌面外壳
    src/desktop/webview2.cpp

    # server：HTTP 服务端 / 静态资源 / 路由
    src/server/http_server.cpp
    src/server/webui.cpp
    src/server/routes.cpp
)

add_executable(agentcpp ${AGENT_SOURCES})

target_include_directories(agentcpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(agentcpp PRIVATE nlohmann_json::nlohmann_json)
add_dependencies(agentcpp embed_assets)

if(WIN32)
    target_include_directories(agentcpp PRIVATE ${WEBVIEW2_DIR}/include)
    target_link_libraries(agentcpp PRIVATE ole32 uuid shell32 user32 gdi32)
endif()

if(STATIC_BUILD)
    target_link_libraries(agentcpp PRIVATE CURL::libcurl ${CURL_EXTRA_LIBS})
    if(SODIUM_LIBRARIES)
        target_link_libraries(agentcpp PRIVATE ${SODIUM_LIBRARIES})
        target_include_directories(agentcpp PRIVATE ${SODIUM_INCLUDE_DIRS})
    elseif(TARGET sodium)
        target_link_libraries(agentcpp PRIVATE sodium)
    endif()
    if(WIN32 OR (UNIX AND NOT APPLE))
        target_link_options(agentcpp PRIVATE -static)
    endif()
else()
    target_link_libraries(agentcpp PRIVATE CURL::libcurl)
    if(SODIUM_LIBRARIES)
        target_link_libraries(agentcpp PRIVATE ${SODIUM_LIBRARIES})
        if(SODIUM_INCLUDE_DIRS)
            target_include_directories(agentcpp PRIVATE ${SODIUM_INCLUDE_DIRS})
        endif()
    elseif(TARGET sodium)
        target_link_libraries(agentcpp PRIVATE sodium)
    endif()
endif()

if(WIN32)
    target_link_libraries(agentcpp PRIVATE ws2_32 wsock32)
endif()

if(MSVC)
    target_compile_options(agentcpp PRIVATE /utf-8 /EHsc)
else()
    target_compile_options(agentcpp PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# ============================================================
# 安装：exe 放根目录，模板资源放 .agent/
# ============================================================
install(TARGETS agentcpp RUNTIME DESTINATION .)


# ---------- CPack ----------
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VENDOR "release")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++ AI companion runtime with multi-platform channels")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_GENERATOR "ZIP;TGZ")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
endif()
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${CMAKE_SYSTEM_NAME}")

include(CPack)
