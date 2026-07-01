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

# 查找 libsodium
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SODIUM QUIET IMPORTED_TARGET libsodium)
endif()
if(NOT SODIUM_FOUND)
    find_package(Sodium CONFIG QUIET)
endif()
if(NOT SODIUM_FOUND)
    find_library(SODIUM_LIBRARY sodium
        HINTS
            ${MSYS2_DIR}/mingw64/lib
            /mingw64/lib
            /opt/homebrew/lib
            /usr/local/lib
    )
    find_path(SODIUM_INCLUDE_DIR sodium.h
        HINTS
            ${MSYS2_DIR}/mingw64/include
            /mingw64/include
            /opt/homebrew/include
            /usr/local/include
    )
    if(SODIUM_LIBRARY AND SODIUM_INCLUDE_DIR)
        set(SODIUM_FOUND TRUE)
        set(SODIUM_LIBRARIES ${SODIUM_LIBRARY})
        set(SODIUM_INCLUDE_DIRS ${SODIUM_INCLUDE_DIR})
    endif()
endif()
if(NOT SODIUM_FOUND)
    message(STATUS "libsodium not found on system, building from source via FetchContent")
    FetchContent_Declare(
        libsodium
        GIT_REPOSITORY https://github.com/jedisct1/libsodium.git
        GIT_TAG 1.0.20-RELEASE
    )
    set(SODIUM_FOUND TRUE)
    FetchContent_MakeAvailable(libsodium)
endif()
message(STATUS "Sodium found: LIBRARY=${SODIUM_LIBRARIES} INCLUDE=${SODIUM_INCLUDE_DIRS}")
if(TARGET PkgConfig::SODIUM)
    message(STATUS "Using PkgConfig::SODIUM imported target")
elseif(TARGET Sodium::sodium)
    message(STATUS "Using Sodium::sodium imported target")
elseif(TARGET sodium)
    message(STATUS "Using sodium imported target")
    add_library(Sodium::sodium ALIAS sodium)
endif()

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
if(TARGET PkgConfig::SODIUM)
    target_link_libraries(app PRIVATE PkgConfig::SODIUM)
elseif(TARGET Sodium::sodium)
    target_link_libraries(app PRIVATE Sodium::sodium)
elseif(SODIUM_LIBRARIES)
    target_link_libraries(app PRIVATE ${SODIUM_LIBRARIES})
endif()

if(WIN32)
    target_link_libraries(app PRIVATE ws2_32 wsock32)
endif()

# libsodium 头文件
if(SODIUM_INCLUDE_DIRS AND NOT TARGET PkgConfig::SODIUM AND NOT TARGET Sodium::sodium)
    target_include_directories(app PRIVATE ${SODIUM_INCLUDE_DIRS})
endif()

# ========== 安装规则 ==========
# 1. app 可执行文件放在根目录
install(TARGETS app RUNTIME DESTINATION .)

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
