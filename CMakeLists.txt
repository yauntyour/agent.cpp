cmake_minimum_required(VERSION 3.22)
project(agent.cpp VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AGENT_ENABLE_TUI "Enable Notcurses TUI interface" ON)
option(AGENT_ENABLE_ROUTER "Enable servic.cpp HTTP router component" ON)
option(AGENT_ENABLE_LSP "Enable LSP server support" ON)
option(AGENT_ENABLE_TESTS "Build tests" OFF)
option(AGENT_STATIC_LINK "Static link runtime and dependencies" ON)

# ── Dependencies ──────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)

pkg_check_modules(CURL_REQUIRED REQUIRED libcurl IMPORTED_TARGET)
pkg_check_modules(SODIUM REQUIRED libsodium IMPORTED_TARGET)
pkg_check_modules(NLOHMANN_JSON REQUIRED nlohmann_json IMPORTED_TARGET)

find_package(Threads REQUIRED)

if(AGENT_ENABLE_TUI)
    message(STATUS "TUI enabled (ANSI-based, no external dependencies)")
endif()

if(AGENT_ENABLE_ROUTER)
    if(EXISTS "${CMAKE_SOURCE_DIR}/external/servic.cpp/servic.hpp")
        find_path(BOOST_INCLUDE_DIRS boost/asio.hpp PATHS ${MINGW_PREFIX}/include $ENV{MINGW_PREFIX}/include)
        if(BOOST_INCLUDE_DIRS)
            message(STATUS "Boost headers found: ${BOOST_INCLUDE_DIRS}")
        else()
            message(WARNING "Boost.Asio headers not found, Router disabled")
            set(AGENT_ENABLE_ROUTER OFF)
        endif()
    else()
        message(WARNING "servic.cpp not found (run: git submodule update --init), Router disabled")
        set(AGENT_ENABLE_ROUTER OFF)
    endif()
    pkg_check_modules(OPENSSL QUIET openssl)
endif()



# ── Source Files ───────────────────────────────────────────────────
set(CORE_SOURCES
    src/core/module.cpp
    src/core/config.cpp
    src/core/lifecycle.cpp
)

set(UTIL_SOURCES
    src/utils/crypto.cpp
    src/utils/http.cpp
    src/utils/fs.cpp
    src/utils/async.cpp
)

set(COMPONENT_SOURCES
    src/components/provider/provider.cpp
    src/components/provider/openai.cpp
    src/components/provider/anthropic.cpp
    src/components/provider/ollama.cpp
    src/components/provider/llama_server.cpp
    src/components/tools/tools.cpp
    src/components/tools/read.cpp
    src/components/tools/write.cpp
    src/components/tools/edit.cpp
    src/components/tools/search.cpp
    src/components/tools/exec.cpp
    src/components/tools/task.cpp
    src/components/tools/question.cpp
    src/components/tools/websearch.cpp
    src/components/tools/webfetch.cpp
    src/components/tools/mind_map.cpp
    src/components/tools/todolist.cpp
    src/components/tools/memory_tool.cpp
    src/components/tools/image.cpp
    src/components/tools/fs_tool.cpp
    src/components/tools/subagent.cpp
    src/components/tools/git_tool.cpp
    src/components/session/session.cpp
    src/components/memory/memory.cpp
    src/components/permission/permission.cpp
    src/components/notice/notice.cpp
    src/components/edit_history/edit_history.cpp
    src/components/service/service.cpp
    src/components/mcp/mcp.cpp
    src/components/system/system.cpp
    src/components/system/channel.cpp
    src/components/agent/agent.cpp
)

if(AGENT_ENABLE_LSP)
    list(APPEND COMPONENT_SOURCES src/components/lsp/lsp.cpp)
endif()

if(AGENT_ENABLE_ROUTER)
    list(APPEND COMPONENT_SOURCES src/components/router/router.cpp)
endif()

if(AGENT_ENABLE_TUI)
    list(APPEND COMPONENT_SOURCES src/components/tui/tui.cpp)
endif()

set(ALL_SOURCES
    src/main.cpp
    ${CORE_SOURCES}
    ${UTIL_SOURCES}
    ${COMPONENT_SOURCES}
)

# ── Executable ─────────────────────────────────────────────────────
add_executable(agent ${ALL_SOURCES})

target_include_directories(agent PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${NLOHMANN_JSON_INCLUDE_DIRS}
)

target_link_libraries(agent PRIVATE
    PkgConfig::CURL_REQUIRED
    PkgConfig::SODIUM
    Threads::Threads
)

if(AGENT_ENABLE_ROUTER)
    target_compile_definitions(agent PRIVATE AGENT_ENABLE_ROUTER)
    target_include_directories(agent PRIVATE
        ${CMAKE_SOURCE_DIR}/external/servic.cpp
    )
    if(BOOST_INCLUDE_DIRS)
        target_include_directories(agent PRIVATE ${BOOST_INCLUDE_DIRS})
    endif()
    if(OPENSSL_FOUND)
        target_link_libraries(agent PRIVATE ${OPENSSL_LIBRARIES})
    endif()
endif()

if(AGENT_ENABLE_LSP)
    target_compile_definitions(agent PRIVATE AGENT_ENABLE_LSP)
endif()

if(AGENT_ENABLE_TUI)
    target_compile_definitions(agent PRIVATE AGENT_ENABLE_TUI)
endif()

if(AGENT_STATIC_LINK AND MINGW)
    target_link_options(agent PRIVATE -static -static-libgcc -static-libstdc++)
endif()

target_compile_definitions(agent PRIVATE
    AGENT_VERSION="${PROJECT_VERSION}"
)

if(WIN32)
    target_compile_definitions(agent PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(agent PRIVATE ws2_32 mswsock)
endif()

# ── Install ────────────────────────────────────────────────────────
install(TARGETS agent RUNTIME DESTINATION bin)

# ── CPack ──────────────────────────────────────────────────────────
set(CPACK_GENERATOR "ZIP;TGZ")
set(CPACK_PACKAGE_NAME "agent.cpp")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
include(CPack)
