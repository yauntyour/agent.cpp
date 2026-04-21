cmake_minimum_required(VERSION 3.15)
project(webui_build)

# 设置 C++ 标准（要求支持 C++26，若编译器不支持可降级）
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 用户可指定的 webui 路径（缓存变量）
set(WEBUI_INCLUDE_DIR "" CACHE PATH "Path to webui include directory (containing webui.h)")
set(WEBUI_LIBRARY "" CACHE FILEPATH "Full path to webui library file (e.g., libwebui-2.a or webui-2.lib)")
set(WEBUI_LIB_DIR "" CACHE PATH "Path to webui library directory (used with WEBUI_LIB_NAME)")
set(WEBUI_LIB_NAME "webui-2" CACHE STRING "Name of webui library (without prefix/lib suffix)")

# 确定 webui 库的完整路径
if(WEBUI_LIBRARY)
    set(WEBUI_LIB_TARGET ${WEBUI_LIBRARY})
elseif(WEBUI_LIB_DIR)
    find_library(WEBUI_LIB_TARGET NAMES ${WEBUI_LIB_NAME} PATHS ${WEBUI_LIB_DIR} NO_DEFAULT_PATH REQUIRED)
else()
    find_library(WEBUI_LIB_TARGET NAMES ${WEBUI_LIB_NAME} REQUIRED)
endif()

# 确定 webui 头文件路径
if(WEBUI_INCLUDE_DIR)
    set(WEBUI_INC_DIR ${WEBUI_INCLUDE_DIR})
else()
    # 若用户指定了库目录，则尝试从库目录推断 include 路径
    find_path(WEBUI_INC_DIR webui.h
        PATHS ${WEBUI_LIB_DIR}/../include ${WEBUI_LIB_DIR}/include
        REQUIRED)
endif()

# 查找 CURL 库（必需）
find_package(CURL REQUIRED)

# 添加可执行文件
add_executable(webui webui.cpp)

# 包含头文件目录
target_include_directories(webui PRIVATE ${WEBUI_INC_DIR} ${CURL_INCLUDE_DIRS})

# 链接库
target_link_libraries(webui PRIVATE ${WEBUI_LIB_TARGET} ${CURL_LIBRARIES})

# Windows 下链接 Socket 库
if(WIN32)
    target_link_libraries(webui PRIVATE ws2_32 wsock32)
endif()

# 针对 GCC/Clang 添加原始命令中的编译标志
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(webui PRIVATE
        -fdiagnostics-color=always
        -finput-charset=UTF-8
        -fexec-charset=UTF-8
    )
endif()

# 可选：Debug 模式下自动添加 -g 标志（CMake 默认行为）
# 若需要强制开启，可设置 CMAKE_BUILD_TYPE 为 Debug