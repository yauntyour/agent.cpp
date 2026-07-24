#pragma once
#include "core/module.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>

namespace agent {

enum class PermissionLevel {
    Auto,       // Auto-execute without asking
    Ask,        // Request user permission before executing
    Deny        // Never allow
};

struct PermissionRule {
    std::string tool_name;
    PermissionLevel level = PermissionLevel::Ask;
    std::string description;
};

class Permission : public Module<Permission> {
public:
    static constexpr std::string_view static_name() { return "permission"; }

    void on_initialize();
    void on_shutdown();

    // ── Tool permissions ───────────────────────────────────────
    void set_tool_permission(std::string_view tool_name, PermissionLevel level);
    PermissionLevel get_tool_permission(std::string_view tool_name) const;
    void set_default_permission(PermissionLevel level);
    std::vector<PermissionRule> list_rules() const;

    // ── Exec command safety check ──────────────────────────────
    struct CommandCheckResult {
        bool is_safe = true;
        std::string warning;
        std::vector<std::string> dangerous_patterns_found;
    };

    CommandCheckResult check_command(std::string_view command) const;
    void add_dangerous_pattern(std::string_view pattern);
    void remove_dangerous_pattern(std::string_view pattern);
    std::vector<std::string> list_dangerous_patterns() const;

    // ── Permission hook ────────────────────────────────────────
    bool should_ask(std::string_view tool_name) const;
    bool is_allowed(std::string_view tool_name) const;

private:
    PermissionLevel m_default_level = PermissionLevel::Ask;
    std::map<std::string, PermissionLevel> m_rules;
    std::vector<std::string> m_dangerous_patterns;

};

} // namespace agent
