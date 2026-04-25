cmake_minimum_required(VERSION 3.20)
project(agent.cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 统一输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 查找 CURL
find_package(CURL REQUIRED)

# ---------- Boost (header-only) ----------
set(Boost_NO_BOOST_CMAKE ON)
find_package(Boost REQUIRED)

# ---------- nlohmann_json (header-only) ----------
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
    Boost::boost
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(app PRIVATE ws2_32 wsock32)
endif()

# ========== 安装规则 ==========
# 1. app 可执行文件放在根目录
install(TARGETS app RUNTIME DESTINATION .)

# 2. settings.json 单独放在根目录
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/settings.json DESTINATION .)

# 3. 其余资源文件放入 workspace/ 子目录
install(FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/webui.html
    ${CMAKE_CURRENT_SOURCE_DIR}/agent.txt
    DESTINATION workspace
)

# 4. 目录资源放入 workspace/ 下的对应子目录
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/sys/ DESTINATION workspace/sys)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tools/ DESTINATION workspace/tools)

# 5. 运行时需要的空目录也放在 workspace/ 下
install(DIRECTORY DESTINATION workspace/sessions)
install(DIRECTORY DESTINATION workspace/memorys)
install(DIRECTORY DESTINATION workspace/assets)

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
