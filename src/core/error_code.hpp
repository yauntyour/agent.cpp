#pragma once
#include <cstdint>
#include <string_view>

namespace agent {

enum class ErrorCode : uint32_t {
    // ── General (0-999) ────────────────────────────────────────────
    Ok                  = 0,
    Unknown             = 1,
    InvalidArgument     = 2,
    NotImplemented      = 3,
    InternalError       = 4,
    OperationCancelled  = 5,
    Timeout             = 6,

    // ── Config (1000-1099) ─────────────────────────────────────────
    ConfigLoadFailed    = 1000,
    ConfigSaveFailed    = 1001,
    InvalidConfig       = 1002,
    ConfigKeyNotFound   = 1003,

    // ── Module (1100-1199) ─────────────────────────────────────────
    ModuleNotFound      = 1100,
    ModuleInitFailed    = 1101,
    ModuleAlreadyReg    = 1102,
    ModuleShutdownFail  = 1103,

    // ── Provider (2000-2099) ───────────────────────────────────────
    ApiError            = 2000,
    ApiAuthError        = 2001,
    ApiRateLimited      = 2002,
    ApiTimeout          = 2003,
    ApiServerError      = 2004,
    ModelNotFound       = 2005,
    NoProviderConfig    = 2006,
    InvalidApiResponse  = 2007,
    StreamError         = 2008,

    // ── Tool (3000-3099) ───────────────────────────────────────────
    ToolNotFound        = 3000,
    ToolExecFailed      = 3001,
    ToolInvalidArgs     = 3002,

    // ── File System (3100-3199) ────────────────────────────────────
    FileNotFound        = 3100,
    FileReadFailed      = 3101,
    FileWriteFailed     = 3102,
    FileEditFailed      = 3103,
    DirectoryNotFound   = 3104,
    PathSandboxEscape   = 3105,
    FileAlreadyExists   = 3106,

    // ── Command (3200-3299) ────────────────────────────────────────
    CommandFailed       = 3200,
    CommandBlocked      = 3201,
    CommandNotFound     = 3202,

    // ── Session (4000-4099) ────────────────────────────────────────
    SessionNotFound     = 4000,
    SessionCorrupted    = 4001,
    SessionCreateFailed = 4002,

    // ── Memory (4100-4199) ─────────────────────────────────────────
    MemoryNotFound      = 4100,
    MemorySaveFailed    = 4101,
    MemorySearchFailed  = 4102,

    // ── Network (5000-5099) ────────────────────────────────────────
    ConnectionFailed    = 5000,
    RequestFailed       = 5001,
    InvalidResponse     = 5002,
    DnsResolutionFailed = 5003,
    TlsError            = 5004,

    // ── Permission (6000-6099) ─────────────────────────────────────
    AccessDenied        = 6000,
    PermissionDenied    = 6001,

    // ── MCP (7000-7099) ────────────────────────────────────────────
    McpSpawnFailed      = 7000,
    McpInitFailed       = 7001,
    McpToolCallFailed   = 7002,
    McpNotAvailable     = 7003,

    // ── LSP (7100-7199) ────────────────────────────────────────────
    LspSpawnFailed      = 7100,
    LspInitFailed       = 7101,
    LspRequestFailed    = 7102,
    LspNotAvailable     = 7103,

    // ── Crypto (8000-8099) ─────────────────────────────────────────
    EncryptionFailed    = 8000,
    DecryptionFailed    = 8001,
    KeyDerivationFailed = 8002,
    HashFailed          = 8003,
};

inline constexpr std::string_view error_code_string(ErrorCode code) {
    switch (code) {
        case ErrorCode::Ok:                 return "Ok";
        case ErrorCode::Unknown:            return "Unknown";
        case ErrorCode::InvalidArgument:    return "InvalidArgument";
        case ErrorCode::NotImplemented:     return "NotImplemented";
        case ErrorCode::InternalError:      return "InternalError";
        case ErrorCode::OperationCancelled: return "OperationCancelled";
        case ErrorCode::Timeout:            return "Timeout";

        case ErrorCode::ConfigLoadFailed:   return "ConfigLoadFailed";
        case ErrorCode::ConfigSaveFailed:   return "ConfigSaveFailed";
        case ErrorCode::InvalidConfig:      return "InvalidConfig";
        case ErrorCode::ConfigKeyNotFound:  return "ConfigKeyNotFound";

        case ErrorCode::ModuleNotFound:     return "ModuleNotFound";
        case ErrorCode::ModuleInitFailed:   return "ModuleInitFailed";
        case ErrorCode::ModuleAlreadyReg:   return "ModuleAlreadyRegistered";
        case ErrorCode::ModuleShutdownFail: return "ModuleShutdownFailed";

        case ErrorCode::ApiError:           return "ApiError";
        case ErrorCode::ApiAuthError:       return "ApiAuthError";
        case ErrorCode::ApiRateLimited:     return "ApiRateLimited";
        case ErrorCode::ApiTimeout:         return "ApiTimeout";
        case ErrorCode::ApiServerError:     return "ApiServerError";
        case ErrorCode::ModelNotFound:      return "ModelNotFound";
        case ErrorCode::NoProviderConfig:   return "NoProviderConfig";
        case ErrorCode::InvalidApiResponse: return "InvalidApiResponse";
        case ErrorCode::StreamError:        return "StreamError";

        case ErrorCode::ToolNotFound:       return "ToolNotFound";
        case ErrorCode::ToolExecFailed:     return "ToolExecFailed";
        case ErrorCode::ToolInvalidArgs:    return "ToolInvalidArgs";

        case ErrorCode::FileNotFound:       return "FileNotFound";
        case ErrorCode::FileReadFailed:     return "FileReadFailed";
        case ErrorCode::FileWriteFailed:    return "FileWriteFailed";
        case ErrorCode::FileEditFailed:     return "FileEditFailed";
        case ErrorCode::DirectoryNotFound:  return "DirectoryNotFound";
        case ErrorCode::PathSandboxEscape:  return "PathSandboxEscape";
        case ErrorCode::FileAlreadyExists:  return "FileAlreadyExists";

        case ErrorCode::CommandFailed:      return "CommandFailed";
        case ErrorCode::CommandBlocked:     return "CommandBlocked";
        case ErrorCode::CommandNotFound:    return "CommandNotFound";

        case ErrorCode::SessionNotFound:    return "SessionNotFound";
        case ErrorCode::SessionCorrupted:   return "SessionCorrupted";
        case ErrorCode::SessionCreateFailed:return "SessionCreateFailed";

        case ErrorCode::MemoryNotFound:     return "MemoryNotFound";
        case ErrorCode::MemorySaveFailed:   return "MemorySaveFailed";
        case ErrorCode::MemorySearchFailed: return "MemorySearchFailed";

        case ErrorCode::ConnectionFailed:   return "ConnectionFailed";
        case ErrorCode::RequestFailed:      return "RequestFailed";
        case ErrorCode::InvalidResponse:    return "InvalidResponse";
        case ErrorCode::DnsResolutionFailed:return "DnsResolutionFailed";
        case ErrorCode::TlsError:           return "TlsError";

        case ErrorCode::AccessDenied:       return "AccessDenied";
        case ErrorCode::PermissionDenied:   return "PermissionDenied";

        case ErrorCode::McpSpawnFailed:     return "McpSpawnFailed";
        case ErrorCode::McpInitFailed:      return "McpInitFailed";
        case ErrorCode::McpToolCallFailed:  return "McpToolCallFailed";
        case ErrorCode::McpNotAvailable:    return "McpNotAvailable";

        case ErrorCode::LspSpawnFailed:     return "LspSpawnFailed";
        case ErrorCode::LspInitFailed:      return "LspInitFailed";
        case ErrorCode::LspRequestFailed:   return "LspRequestFailed";
        case ErrorCode::LspNotAvailable:    return "LspNotAvailable";

        case ErrorCode::EncryptionFailed:   return "EncryptionFailed";
        case ErrorCode::DecryptionFailed:   return "DecryptionFailed";
        case ErrorCode::KeyDerivationFailed:return "KeyDerivationFailed";
        case ErrorCode::HashFailed:         return "HashFailed";

        default:                            return "UnknownCode";
    }
}

inline constexpr std::string_view error_category(ErrorCode code) {
    auto v = static_cast<uint32_t>(code);
    if (v == 0)          return "General";
    if (v < 1000)        return "General";
    if (v < 1100)        return "Config";
    if (v < 1200)        return "Module";
    if (v < 2100)        return "Provider";
    if (v < 3100)        return "Tool";
    if (v < 3200)        return "FileSystem";
    if (v < 3300)        return "Command";
    if (v < 4100)        return "Session";
    if (v < 4200)        return "Memory";
    if (v < 5100)        return "Network";
    if (v < 6100)        return "Permission";
    if (v < 7100)        return "MCP";
    if (v < 7200)        return "LSP";
    if (v < 8100)        return "Crypto";
    return "Unknown";
}

} // namespace agent
