cmake_minimum_required(VERSION 3.20)
project(agent.cpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 统一输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 查找 CURL
find_package(CURL REQUIRED)

# 解决 macOS Homebrew Boost 1.90+ 的 Config 模式查找失败问题
# 强制使用旧版 FindBoost 模块
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
endif()
find_package(Boost REQUIRED)

# 通过 FetchContent 获取 nlohmann_json
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# 主可执行文件
add_executable(app
    app.cpp
)

# 链接库
target_link_libraries(app PRIVATE
    CURL::libcurl
    Boost::system
    nlohmann_json::nlohmann_json
)

# Windows 需要 Winsock
if(WIN32)
    target_link_libraries(app PRIVATE ws2_32 wsock32)
endif()

# 编译选项
if(MSVC)
    target_compile_options(app PRIVATE /utf-8 /W4)
else()
    target_compile_options(app PRIVATE -Wall -Wextra -pedantic)
endif()

# 需要打包的资产文件（相对于源码根目录）
set(ASSET_FILES
    webui.html
    agent.txt
    settings.json
)

set(ASSET_DIRS
    sys
    tools
)

# 安装规则
install(TARGETS app RUNTIME DESTINATION .)

foreach(file ${ASSET_FILES})
    install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/${file} DESTINATION .)
endforeach()

foreach(dir ${ASSET_DIRS})
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${dir}/ DESTINATION ./${dir})
endforeach()

# 运行时需要的空目录
install(DIRECTORY DESTINATION ./sessions)
install(DIRECTORY DESTINATION ./memorys)
install(DIRECTORY DESTINATION ./assets)

# ---------- CPack ----------
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++ Agent with web UI and tools")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_GENERATOR "ZIP;TGZ")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
endif()
include(CPack)
