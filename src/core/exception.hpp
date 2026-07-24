#pragma once
#include "error_code.hpp"
#include "logger.hpp"
#include <exception>
#include <string>
#include <string_view>
#include <source_location>
#include <sstream>
#include <utility>

namespace agent {

class BaseException : public std::exception {
public:
    BaseException(ErrorCode code,
                  std::string message,
                  std::string_view module = "",
                  std::source_location loc = std::source_location::current())
        : m_code(code)
        , m_message(std::move(message))
        , m_module(module)
        , m_file(loc.file_name())
        , m_line(loc.line())
        , m_function(loc.function_name()) {
        build_full_message();
        log_exception();
    }

    const char* what() const noexcept override { return m_full_message.c_str(); }

    ErrorCode code() const noexcept { return m_code; }
    const std::string& message() const noexcept { return m_message; }
    const std::string& module() const noexcept { return m_module; }
    const char* file() const noexcept { return m_file; }
    int line() const noexcept { return m_line; }
    const char* function() const noexcept { return m_function; }

    std::string_view error_code_name() const noexcept { return error_code_string(m_code); }
    std::string_view category() const noexcept { return error_category(m_code); }

    BaseException& with_context(std::string_view key, std::string_view value) {
        m_context += "  " + std::string(key) + "=" + std::string(value) + "\n";
        rebuild_full_message();
        return *this;
    }

protected:
    void log_exception() const {
        std::ostringstream oss;
        oss << m_message << " (" << m_file << ":" << m_line << ")";
        if (!m_context.empty()) {
            oss << "\n" << m_context;
        }
        Logger::instance().log(LogLevel::Error, m_module, oss.str(), m_file, m_line);
    }

private:
    void build_full_message() {
        m_full_message = "[" + std::string(error_code_string(m_code)) + "] " + m_message;
    }

    void rebuild_full_message() {
        build_full_message();
    }

    ErrorCode m_code;
    std::string m_message;
    std::string m_module;
    const char* m_file;
    int m_line;
    const char* m_function;
    std::string m_context;
    std::string m_full_message;
};

// ── Subsystem exceptions ───────────────────────────────────────────

class ConfigException : public BaseException {
public:
    ConfigException(ErrorCode code, std::string message,
                    std::source_location loc = std::source_location::current())
        : BaseException(code, std::move(message), "Config", loc) {}
};

class ModuleException : public BaseException {
public:
    ModuleException(ErrorCode code, std::string message,
                    std::source_location loc = std::source_location::current())
        : BaseException(code, std::move(message), "Module", loc) {}
};

class ProviderException : public BaseException {
public:
    ProviderException(ErrorCode code, std::string message,
                      std::source_location loc = std::source_location::current())
        : BaseException(code, std::move(message), "Provider", loc) {}
};

class ApiException : public ProviderException {
public:
    ApiException(std::string message, int http_status = 0,
                 std::source_location loc = std::source_location::current())
        : ProviderException(ErrorCode::ApiError, std::move(message), loc)
        , m_http_status(http_status) {}

    int http_status() const noexcept { return m_http_status; }

private:
    int m_http_status;
};

class AuthException : public ProviderException {
public:
    AuthException(std::string message,
                  std::source_location loc = std::source_location::current())
        : ProviderException(ErrorCode::ApiAuthError, std::move(message), loc) {}
};

class TimeoutException : public BaseException {
public:
    TimeoutException(std::string message,
                     std::source_location loc = std::source_location::current())
        : BaseException(ErrorCode::Timeout, std::move(message), "Network", loc) {}
};

class ToolException : public BaseException {
public:
    ToolException(ErrorCode code, std::string message,
                  std::source_location loc = std::source_location::current())
        : BaseException(code, std::move(message), "Tools", loc) {}
};

class FileSystemException : public ToolException {
public:
    FileSystemException(ErrorCode code, std::string message,
                        std::source_location loc = std::source_location::current())
        : ToolException(code, std::move(message), loc) {}
};

class CommandException : public ToolException {
public:
    CommandException(ErrorCode code, std::string message,
                     std::source_location loc = std::source_location::current())
        : ToolException(code, std::move(message), loc) {}
};

class SessionException : public BaseException {
public:
    SessionException(ErrorCode code, std::string message,
                     std::source_location loc = std::source_location::current())
        : BaseException(code, std::move(message), "Session", loc) {}
};

class MemoryException : public BaseException {
public:
    MemoryException(ErrorCode code, std::string message,
                    std::source_location loc = std::source_location::current())
        : BaseException(code, std::move(message), "Memory", loc) {}
};

class NetworkException : public BaseException {
public:
    NetworkException(ErrorCode code, std::string message,
                     std::source_location loc = std::source_location::current())
        : BaseException(code, std::move(message), "Network", loc) {}
};

class PermissionException : public BaseException {
public:
    PermissionException(ErrorCode code, std::string message,
                        std::source_location loc = std::source_location::current())
        : BaseException(code, std::move(message), "Permission", loc) {}
};

class McpException : public BaseException {
public:
    McpException(ErrorCode code, std::string message,
                 std::source_location loc = std::source_location::current())
        : BaseException(code, std::move(message), "MCP", loc) {}
};

class LspException : public BaseException {
public:
    LspException(ErrorCode code, std::string message,
                 std::source_location loc = std::source_location::current())
        : BaseException(code, std::move(message), "LSP", loc) {}
};

class CryptoException : public BaseException {
public:
    CryptoException(ErrorCode code, std::string message,
                    std::source_location loc = std::source_location::current())
        : BaseException(code, std::move(message), "Crypto", loc) {}
};

// ── Helper macros for throwing exceptions ──────────────────────────

#define THROW_CONFIG(msg) \
    throw ConfigException(msg, std::source_location::current())

#define THROW_CONFIG_CODE(code, msg) \
    throw ConfigException(code, msg, std::source_location::current())

#define THROW_MODULE(msg) \
    throw ModuleException(msg, std::source_location::current())

#define THROW_MODULE_CODE(code, msg) \
    throw ModuleException(code, msg, std::source_location::current())

#define THROW_PROVIDER(msg) \
    throw ProviderException(msg, std::source_location::current())

#define THROW_PROVIDER_CODE(code, msg) \
    throw ProviderException(code, msg, std::source_location::current())

#define THROW_TOOL(msg) \
    throw ToolException(msg, std::source_location::current())

#define THROW_TOOL_CODE(code, msg) \
    throw ToolException(code, msg, std::source_location::current())

#define THROW_FILESYSTEM(code, msg) \
    throw FileSystemException(code, msg, std::source_location::current())

#define THROW_COMMAND(code, msg) \
    throw CommandException(code, msg, std::source_location::current())

#define THROW_SESSION(code, msg) \
    throw SessionException(code, msg, std::source_location::current())

#define THROW_NETWORK(code, msg) \
    throw NetworkException(code, msg, std::source_location::current())

#define THROW_PERMISSION(code, msg) \
    throw PermissionException(code, msg, std::source_location::current())

#define THROW_MCP(code, msg) \
    throw McpException(code, msg, std::source_location::current())

#define THROW_LSP(code, msg) \
    throw LspException(code, msg, std::source_location::current())

#define THROW_CRYPTO(code, msg) \
    throw CryptoException(code, msg, std::source_location::current())

// ── Tool result error macro ────────────────────────────────────────
// Note: Use TOOL_CATCH_BEGIN and TOOL_CATCH_END to avoid macro comma issues
#define TOOL_CATCH_BEGIN(call) \
    try {

#define TOOL_CATCH_END(call) \
    } catch (const agent::BaseException& e) { \
        std::ostringstream _err_oss; \
        _err_oss << "[" << e.error_code_name() << "] " << e.message(); \
        if (e.file()) _err_oss << " (" << e.file() << ":" << e.line() << ")"; \
        return {(call).id, "", true, _err_oss.str()}; \
    } catch (const std::exception& e) { \
        LOG_ERROR("Tools", std::string("Unexpected exception: ") + e.what()); \
        return {(call).id, "", true, e.what()}; \
    } catch (...) { \
        LOG_ERROR("Tools", "Unknown exception caught"); \
        return {(call).id, "", true, "Unknown error occurred"}; \
    }

} // namespace agent
