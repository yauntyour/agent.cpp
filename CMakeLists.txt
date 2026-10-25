# agent.cpp — 陪伴型智能体（Cosplay Agent）的桌面外壳 + 本机服务
# =============================================================================
# 打包方式沿用 cell-app：
#
#   cmake -B build            # 配置 (Ninja / Makefiles)
#   cmake --build build       # 编译   → build/agent[.exe]
#
# Windows 需要 MSYS2 UCRT64 工具链（g++ / windres），默认静态链接：
# 产物只导入系统 DLL，WebView2 运行时由系统 Edge 提供。
#
# 选项：
#   -DAGENT_STATIC=OFF         Windows：链接 MSYS2 运行时 DLL（默认静态）
#   -DAGENT_CONSOLE=ON         Windows：保留控制台（调试）
#   -DAGENT_BUILD_UI=OFF       沿用已提交的 src/agent-ui.hpp（不需要 Python）
#   -DAGENT_CXX_STANDARD=23    不支持 -std=c++26 的编译器
# =============================================================================

cmake_minimum_required(VERSION 3.25)

project(agent LANGUAGES CXX)

# 智能体内核：cell.cpp（单文件库，作为子模块/副本放在源码树里）
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/cell.cpp/cell.cpp")
    message(FATAL_ERROR
        "cell.cpp/cell.cpp not found — initialize the submodule first:\n"
        "  git submodule update --init --recursive")
endif()

# ---- options ----------------------------------------------------------------

option(AGENT_STATIC "Windows only: link the runtime + libcurl/libsodium statically" ON)
option(AGENT_CONSOLE "Windows only: keep a console window (debugging)" OFF)
option(AGENT_BUILD_UI "Regenerate the embedded UI from src/ui/src before building" ON)

set(AGENT_CXX_STANDARD "26" CACHE STRING
    "C++ standard (26 by default; set 23 on compilers without -std=c++26)")

# ---- artifacts land in <source>/build ---------------------------------------

set(AGENT_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/build" CACHE PATH
    "Directory the built executable lands in")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${AGENT_OUTPUT_DIR}")
foreach(_agent_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${_agent_cfg} "${AGENT_OUTPUT_DIR}")
endforeach()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

set(CMAKE_CXX_STANDARD "${AGENT_CXX_STANDARD}")
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- Windows: MSYS2 (MinGW) + windres for the icon --------------------------

if(WIN32)
    if(NOT MINGW)
        message(FATAL_ERROR
            "Windows builds use the MSYS2 UCRT64 (MinGW) toolchain. Run CMake from the "
            "MSYS2 UCRT64 shell, or point it at the toolchain explicitly, e.g.\n"
            "  cmake -B build -G Ninja -DCMAKE_CXX_COMPILER=D:/x64/msys2/ucrt64/bin/g++.exe")
    endif()
    if(NOT CMAKE_RC_COMPILER)
        get_filename_component(_agent_bindir "${CMAKE_CXX_COMPILER}" DIRECTORY)
        if(EXISTS "${_agent_bindir}/windres.exe")
            set(CMAKE_RC_COMPILER "${_agent_bindir}/windres.exe"
                CACHE FILEPATH "Windows resource compiler")
        endif()
    endif()
    enable_language(RC)
endif()

# ---- the application --------------------------------------------------------

add_executable(agent src/agent.cpp)

target_include_directories(agent PRIVATE
    "${CMAKE_SOURCE_DIR}/src"                  # agent-core.hpp, agent-voice.hpp, agent-ui.hpp
    "${CMAKE_SOURCE_DIR}/third_party/include"  # webview/, servic.hpp, router/
    "${CMAKE_SOURCE_DIR}")                     # "cell.cpp/cell.cpp"

# ---- platform wiring --------------------------------------------------------

if(NOT WIN32)
    find_package(Threads REQUIRED)
endif()

if(WIN32)   # MSYS2 UCRT64 — WebView2 window

    find_package(PkgConfig REQUIRED)

    # libcurl via pkg-config；静态模式下把传递依赖（zlib/brotli/ssl/ssh2…）一并拉进来，
    # 并用 -DCURL_STATICLIB 让 curl 头文件声明普通符号（否则链接报 __imp_curl_*）。
    pkg_check_modules(CURL REQUIRED libcurl)
    target_include_directories(agent PRIVATE ${CURL_INCLUDE_DIRS})
    if(AGENT_STATIC)
        target_compile_definitions(agent PRIVATE ${CURL_STATIC_CFLAGS_OTHER})
        target_link_options(agent PRIVATE -static)
        set(_agent_curl_libs ${CURL_STATIC_LIBRARIES})
    else()
        set(_agent_curl_libs ${CURL_LIBRARIES})
    endif()

    # crypt32: cell::net 的 CA 解析走 Windows ROOT 证书库（静态 curl 自带的
    # CA 路径是 POSIX 路径，Windows 上必然 curl 60）。
    # winmm: 原生录音（MCI waveaudio）。
    # ole32/shell32/uuid: 托盘 + 原生文件夹选择器。
    target_link_libraries(agent PRIVATE
        ${_agent_curl_libs}
        sodium
        ole32 shell32 shlwapi uuid user32 version advapi32
        gdi32 ws2_32 mswsock bcrypt crypt32 winmm)

    if(AGENT_CONSOLE)
        set_target_properties(agent PROPERTIES WIN32_EXECUTABLE OFF)
    else()
        set_target_properties(agent PROPERTIES WIN32_EXECUTABLE ON)
    endif()

    set(_agent_icon_rc "${CMAKE_SOURCE_DIR}/assets/icon.rc")
    if(EXISTS "${_agent_icon_rc}")
        target_sources(agent PRIVATE "${_agent_icon_rc}")
        set_source_files_properties("${_agent_icon_rc}" PROPERTIES
            INCLUDE_DIRECTORIES "${CMAKE_SOURCE_DIR}/assets")
    else()
        message(WARNING "assets/icon.rc not found — the exe keeps no embedded icon")
    endif()

elseif(APPLE)   # Cocoa + WKWebView

    find_library(SODIUM_LIBRARY NAMES sodium HINTS /opt/homebrew/lib /usr/local/lib REQUIRED)
    find_path(SODIUM_INCLUDE_DIR sodium.h HINTS /opt/homebrew/include /usr/local/include)
    if(SODIUM_INCLUDE_DIR)
        target_include_directories(agent PRIVATE "${SODIUM_INCLUDE_DIR}")
    endif()
    find_path(AGENT_BREW_INCLUDE_DIR boost/asio.hpp HINTS /opt/homebrew/include /usr/local/include)
    if(AGENT_BREW_INCLUDE_DIR)
        target_include_directories(agent PRIVATE "${AGENT_BREW_INCLUDE_DIR}")
    endif()

    target_link_libraries(agent PRIVATE
        "-framework Cocoa" "-framework WebKit" "-framework AudioToolbox"
        curl ${SODIUM_LIBRARY} Threads::Threads)

else()          # Linux — GTK3 + WebKitGTK

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK REQUIRED IMPORTED_TARGET gtk+-3.0)
    pkg_check_modules(WEBKIT IMPORTED_TARGET webkit2gtk-4.1)
    if(NOT WEBKIT_FOUND)
        pkg_check_modules(WEBKIT IMPORTED_TARGET webkit2gtk-4.0)
    endif()
    if(NOT WEBKIT_FOUND)
        message(FATAL_ERROR
            "WebKitGTK development package not found. Install:\n"
            "  webkit2gtk-4.1-dev + libgtk-3-dev  (Debian/Ubuntu)\n"
            "  webkit2gtk4.1-devel + gtk3-devel   (Fedora)")
    endif()

    find_package(CURL REQUIRED)
    find_library(SODIUM_LIBRARY NAMES sodium REQUIRED)

    target_link_libraries(agent PRIVATE
        PkgConfig::GTK PkgConfig::WEBKIT
        CURL::libcurl ${SODIUM_LIBRARY} Threads::Threads)
endif()

# ---- embedded UI ------------------------------------------------------------
# src/ui/src/{index.html,css/*.css,js/*.js}  →  src/agent-ui.hpp（C++ 唯一需要的产物）
#                                              + src/ui/index.html（--ui 开发模式直接服务）
# 声明 agent-ui.hpp 为 OUTPUT，让编译依赖跟着重建；build_ui.py 只在内容变化时写盘。

if(AGENT_BUILD_UI)
    if(DEFINED ENV{PYTHON} AND NOT Python3_EXECUTABLE)
        set(Python3_EXECUTABLE "$ENV{PYTHON}")
    endif()
    find_package(Python3 COMPONENTS Interpreter QUIET)
endif()

if(AGENT_BUILD_UI AND Python3_Interpreter_FOUND)
    file(GLOB_RECURSE AGENT_UI_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/ui/src/*")
    set(AGENT_UI_HPP "${CMAKE_SOURCE_DIR}/src/agent-ui.hpp")
    add_custom_command(
        OUTPUT "${AGENT_UI_HPP}"
        BYPRODUCTS "${CMAKE_SOURCE_DIR}/src/ui/index.html"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/build_ui.py"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS "${CMAKE_SOURCE_DIR}/build_ui.py" ${AGENT_UI_SOURCES}
        COMMENT "Embedding the UI (src/ui/src → src/agent-ui.hpp)"
        VERBATIM)
    add_custom_target(agent-ui DEPENDS "${AGENT_UI_HPP}")
    add_dependencies(agent agent-ui)
elseif(AGENT_BUILD_UI)
    message(WARNING
        "Python 3 not found — keeping the committed src/agent-ui.hpp. "
        "Pass -DPython3_EXECUTABLE=... or set the PYTHON environment variable.")
endif()

# ---- summary ----------------------------------------------------------------

message(STATUS "agent: C++${AGENT_CXX_STANDARD}, executable lands in ${AGENT_OUTPUT_DIR}")
