cmake_minimum_required(VERSION 3.15)
project(webui_build)

# 设置 C++ 标准（要求 C++26，若编译器不支持可降级）
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 自动检测路径（相对于当前源码目录）
set(WEBUI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/webui")
set(WEBUI_INCLUDE_DIR "${WEBUI_ROOT}/include")
set(WEBUI_LIB_DIR "${WEBUI_ROOT}")

# 查找动态库
# 优先查找 .dll (Windows) / .so (Linux) / .dylib (macOS)
if(WIN32)
    set(WEBUI_LIB_SUFFIX "dll")
    # 注意：Windows 下链接动态库需要对应的 .lib 导入库（或使用 .dll 直接链接，但通常需要 .lib）
    # 如果 webui 提供的是 .dll + .lib，则查找 .lib；如果只有 .dll，则需使用 `-l` 名称
    # 这里假设提供的是 libwebui-2.dll 和对应的 libwebui-2.dll.a (MinGW) 或 webui-2.lib (MSVC)
    find_library(WEBUI_LIBRARY
        NAMES webui-2 libwebui-2
        PATHS ${WEBUI_LIB_DIR}
        NO_DEFAULT_PATH
    )
elseif(APPLE)
    set(WEBUI_LIB_SUFFIX "dylib")
    find_library(WEBUI_LIBRARY
        NAMES libwebui-2 webui-2
        PATHS ${WEBUI_LIB_DIR}
        NO_DEFAULT_PATH
    )
else()
    set(WEBUI_LIB_SUFFIX "so")
    find_library(WEBUI_LIBRARY
        NAMES libwebui-2 webui-2
        PATHS ${WEBUI_LIB_DIR}
        NO_DEFAULT_PATH
    )
endif()

# 如果未找到，尝试直接使用库名称让链接器搜索（通过 -L 路径 + -l 名称）
if(NOT WEBUI_LIBRARY)
    message(STATUS "Could not find full library path, will use -L${WEBUI_LIB_DIR} -lwebui-2")
    set(WEBUI_LIBRARY "webui-2")  # 用于 target_link_libraries 的名称
    set(WEBUI_USE_LINK_DIR TRUE)
endif()

# 检查头文件是否存在
if(NOT EXISTS "${WEBUI_INCLUDE_DIR}/webui.h")
    message(FATAL_ERROR "webui.h not found in ${WEBUI_INCLUDE_DIR}")
endif()

# 查找 CURL（必需）
find_package(CURL REQUIRED)

# 添加可执行文件
add_executable(webui webui.cpp)

# 包含头文件目录
target_include_directories(webui PRIVATE ${WEBUI_INCLUDE_DIR} ${CURL_INCLUDE_DIRS})

# 链接 webui 库
if(WEBUI_USE_LINK_DIR)
    # 使用 -L 指定库路径，然后链接库名
    target_link_directories(webui PRIVATE ${WEBUI_LIB_DIR})
    target_link_libraries(webui PRIVATE ${WEBUI_LIBRARY} ${CURL_LIBRARIES})
else()
    target_link_libraries(webui PRIVATE ${WEBUI_LIBRARY} ${CURL_LIBRARIES})
endif()

# Windows 下链接 Socket 库
if(WIN32)
    target_link_libraries(webui PRIVATE ws2_32 wsock32)
endif()

# 针对 GCC/Clang 添加编译标志（模拟原 g++ 命令）
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(webui PRIVATE
        -fdiagnostics-color=always
        -finput-charset=UTF-8
        -fexec-charset=UTF-8
    )
endif()

# 如果需要运行时动态库路径（例如将 dll 复制到可执行文件所在目录），可添加自定义命令
if(WIN32 AND EXISTS "${WEBUI_LIB_DIR}/webui-2.dll")
    add_custom_command(TARGET webui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WEBUI_LIB_DIR}/webui-2.dll"
            $<TARGET_FILE_DIR:webui>
    )
elseif(APPLE AND EXISTS "${WEBUI_LIB_DIR}/libwebui-2.dylib")
    add_custom_command(TARGET webui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WEBUI_LIB_DIR}/libwebui-2.dylib"
            $<TARGET_FILE_DIR:webui>
    )
elseif(EXISTS "${WEBUI_LIB_DIR}/libwebui-2.so")
    add_custom_command(TARGET webui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WEBUI_LIB_DIR}/libwebui-2.so"
            $<TARGET_FILE_DIR:webui>
    )
endif()