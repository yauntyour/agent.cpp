cmake_minimum_required(VERSION 3.28)
project(agentcpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ============================================================
# 0 依赖构建开关（默认开启，面向 CI 云编译发布产物）
#   ON  : 静态链接所有第三方库，产出无需任何额外运行库的可执行文件
#         - Linux   : 全静态 musl 二进制（CI 内使用 Alpine 容器构建）
#         - Windows : 全静态 MinGW exe（无任何 DLL 依赖）
#         - macOS   : 静态链接 curl/libsodium，仅依赖苹果系统自带库
#   OFF : 动态链接系统库（本地开发调试用）
# ============================================================
option(STATIC_BUILD "Build a fully static, zero-runtime-dependency executable" ON)

# 统一输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include(FetchContent)

# ---------- libcurl ----------
if(STATIC_BUILD)
    # 只搜索静态库，避免误链到动态库导致运行期依赖
    if(NOT APPLE)
        set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    endif()

    # 从源码静态构建 libcurl（curl 8.12.1），按平台选择原生 TLS 后端：
    #   macOS  -> SecureTransport（系统钥匙串，零额外依赖）
    #   Windows-> Schannel（Windows 证书库，零额外依赖）
    #   Linux  -> OpenSSL（CI 中为 Alpine 静态 musl 构建）
    # 注意：curl 子项目的选项都是 CACHE 变量，必须用 CACHE+FORCE 传入才会生效
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

    # curl 8.12 的静态库目标把依赖以 PRIVATE 方式链接，
    # 这里为最终可执行文件补充各平台所需的传递依赖库
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
    # libsodium 官方仓库无 CMake 构建文件（纯 autotools），
    # 使用官方认可的 CMake 包装 robinlinden/libsodium-cmake
    # （以 submodule 打包完整 libsodium 源码），三平台统一从源码静态构建，
    # 产出零外部依赖的二进制
    set(SODIUM_DISABLE_TESTS ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        libsodium
        GIT_REPOSITORY https://github.com/robinlinden/libsodium-cmake.git
        GIT_TAG 9b2848dfc1b917a9410f0de9d81059b26cbfaa8d
        GIT_SUBMODULES libsodium
    )
    FetchContent_MakeAvailable(libsodium)
    set(SODIUM_LIBRARIES "")
    message(STATUS "libsodium: building static from source via FetchContent")
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SODIUM QUIET libsodium)
    endif()
    if(NOT SODIUM_FOUND)
        find_package(Sodium CONFIG QUIET)
    endif()

    # 始终通过 find_library 获取完整路径，避免 macOS 上 -lsodium 找不到库的问题
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
        message(FATAL_ERROR "libsodium not found on system, install it via package manager (brew/apt/vcpkg)")
    endif()
endif()

# ---------- Boost (header-only) ----------
find_path(BOOST_INCLUDE_DIR boost/config.hpp)

# ---------- nlohmann_json (header-only) ----------
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# 主可执行文件
add_executable(agentcpp
    app.cpp
)

# 链接库
target_link_libraries(agentcpp PRIVATE
    nlohmann_json::nlohmann_json
)

target_include_directories(agentcpp PRIVATE ${BOOST_INCLUDE_DIR})

if(STATIC_BUILD)
    target_link_libraries(agentcpp PRIVATE CURL::libcurl ${CURL_EXTRA_LIBS})

    if(SODIUM_LIBRARIES)
        target_link_libraries(agentcpp PRIVATE ${SODIUM_LIBRARIES})
        target_include_directories(agentcpp PRIVATE ${SODIUM_INCLUDE_DIRS})
    elseif(TARGET sodium)
        target_link_libraries(agentcpp PRIVATE sodium)
    endif()

    # 完全静态链接（Linux musl / Windows MinGW），产出零依赖二进制
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

# ========== 安装规则 ==========
# 1. agentcpp 可执行文件放在根目录
install(TARGETS agentcpp RUNTIME DESTINATION .)

# 2. settings.json 单独放在根目录
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/settings.json DESTINATION .)

# 3. 打包workspace
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/workspace/ DESTINATION workspace/)

# ---------- CPack ----------
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VENDOR "release")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++ Agent with web UI and tools")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_GENERATOR "ZIP;TGZ")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
endif()

# 去掉版本号，只保留 name-系统名
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${CMAKE_SYSTEM_NAME}")

include(CPack)
