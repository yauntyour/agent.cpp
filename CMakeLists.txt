cmake_minimum_required(VERSION 3.20)
project(agent.cpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 输出目录统一
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 查找依赖
find_package(CURL REQUIRED)

# 查找 Boost，优先使用 Config 模式，macOS 上通过 Boost_ROOT 指定路径
find_package(Boost REQUIRED COMPONENTS system)

# nlohmann_json 通过 FetchContent 自动获取
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

# Windows 下需要链接 Winsock 库
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

# 创建工作目录的空子目录（运行时需要）
install(DIRECTORY DESTINATION ./sessions)
install(DIRECTORY DESTINATION ./memorys)
install(DIRECTORY DESTINATION ./assets)

# ---------- CPack 配置 ----------
set(CPACK_PACKAGE_NAME ${PROJECT_NAME})
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "C++ Agent with web UI and tools")
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_CURRENT_SOURCE_DIR}/LICENSE)
set(CPACK_GENERATOR "ZIP;TGZ")
if(WIN32)
    set(CPACK_GENERATOR "ZIP")
endif()
include(CPack)
