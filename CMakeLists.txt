cmake_minimum_required(VERSION 3.22)
project(agent.cpp VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -O0 -fsanitize=address,undefined")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -DNDEBUG")

option(AGENT_ENABLE_TUI "Enable Notcurses TUI interface" ON)
option(AGENT_ENABLE_ROUTER "Enable servic.cpp HTTP router component" ON)
option(AGENT_ENABLE_LSP "Enable LSP server support" ON)
option(AGENT_ENABLE_TESTS "Build tests" OFF)
option(AGENT_STATIC_LINK "Static link runtime and dependencies" ON)

# ── Dependencies ──────────────────────────────────────────────────
find_package(CURL REQUIRED)
find_package(libsodium REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Threads REQUIRED)

if(AGENT_ENABLE_TUI)
    find_package(Notcurses QUIET)
endif()

if(AGENT_ENABLE_ROUTER)
    if(NOT EXISTS "${CMAKE_SOURCE_DIR}/external/servic.cpp/CMakeLists.txt")
        execute_process(
            COMMAND git submodule update --init --recursive
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE GIT_SUBMODULE_RESULT
        )
        if(NOT GIT_SUBMODULE_RESULT EQUAL 0)
            message(WARNING "Failed to init servic.cpp submodule. Router disabled.")
            set(AGENT_ENABLE_ROUTER OFF)
        endif()
    endif()
    if(AGENT_ENABLE_ROUTER AND EXISTS "${CMAKE_SOURCE_DIR}/external/servic.cpp/CMakeLists.txt")
        add_subdirectory(external/servic.cpp EXCLUDE_FROM_ALL)
        find_package(OpenSSL QUIET)
        find_package(Boost REQUIRED COMPONENTS system)
    endif()
endif()

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET libavcodec libavformat libavutil libswscale)
    pkg_check_modules(OPENIMAGEIO QUIET OpenImageIO)
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

if(Notcurses_FOUND)
    set(AGENT_ENABLE_TUI ON)
    list(APPEND COMPONENT_SOURCES src/components/tui/tui.cpp)
else()
    set(AGENT_ENABLE_TUI OFF)
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
)

target_link_libraries(agent PRIVATE
    CURL::libcurl
    sodium
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(AGENT_ENABLE_ROUTER)
    target_compile_definitions(agent PRIVATE AGENT_ENABLE_ROUTER)
    target_link_libraries(agent PRIVATE
        servic
        Boost::system
    )
    if(OPENSSL_FOUND)
        target_link_libraries(agent PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()
endif()

if(AGENT_ENABLE_LSP)
    target_compile_definitions(agent PRIVATE AGENT_ENABLE_LSP)
endif()

if(AGENT_ENABLE_TUI)
    target_compile_definitions(agent PRIVATE AGENT_ENABLE_TUI)
    target_link_libraries(agent PRIVATE Notcurses::Notcurses)
    if(FFMPEG_FOUND)
        target_compile_definitions(agent PRIVATE AGENT_HAS_FFMPEG)
        target_link_libraries(agent PRIVATE ${FFMPEG_LIBRARIES})
    endif()
    if(OPENIMAGEIO_FOUND)
        target_compile_definitions(agent PRIVATE AGENT_HAS_OIIO)
        target_link_libraries(agent PRIVATE ${OPENIMAGEIO_LIBRARIES})
    endif()
endif()

if(AGENT_STATIC_LINK)
    if(MSVC)
        set_target_properties(agent PROPERTIES
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        )
    elseif(MINGW)
        target_link_options(agent PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

target_compile_definitions(agent PRIVATE
    AGENT_VERSION="${PROJECT_VERSION}"
)

if(WIN32)
    target_compile_definitions(agent PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(agent PRIVATE ws2_32)
endif()

# ── Install ────────────────────────────────────────────────────────
install(TARGETS agent RUNTIME DESTINATION bin)

# ── CPack ──────────────────────────────────────────────────────────
set(CPACK_GENERATOR "ZIP;TGZ")
set(CPACK_PACKAGE_NAME "agent.cpp")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
include(CPack)
