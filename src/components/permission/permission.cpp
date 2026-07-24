#include "components/permission/permission.hpp"
#include "core/config.hpp"
#include <regex>

namespace agent {

void Permission::on_initialize() {
    auto& cfg = Config::instance();
    m_dangerous_patterns = cfg.dangerous_commands;

    // Load auto-allowed tools
    for (auto& tool : cfg.auto_allowed_tools) {
        m_rules[tool] = PermissionLevel::Auto;
    }

    if (cfg.default_tool_permission == "auto") {
        m_default_level = PermissionLevel::Auto;
    }
}

void Permission::on_shutdown() {}

void Permission::set_tool_permission(std::string_view tool_name, PermissionLevel level) {
    m_rules[std::string(tool_name)] = level;
}

PermissionLevel Permission::get_tool_permission(std::string_view tool_name) const {
    auto it = m_rules.find(std::string(tool_name));
    if (it != m_rules.end()) return it->second;
    return m_default_level;
}

void Permission::set_default_permission(PermissionLevel level) {
    m_default_level = level;
}

std::vector<PermissionRule> Permission::list_rules() const {
    std::vector<PermissionRule> rules;
    for (auto& [name, level] : m_rules) {
        rules.push_back({name, level, ""});
    }
    return rules;
}

Permission::CommandCheckResult Permission::check_command(std::string_view command) const {
    CommandCheckResult result;
    std::string cmd(command);

    for (auto& pattern : m_dangerous_patterns) {
        // Check if command contains the dangerous pattern
        // Simple substring match (case-insensitive)
        std::string cmd_lower = cmd;
        std::string pat_lower = pattern;
        std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(), ::tolower);
        std::transform(pat_lower.begin(), pat_lower.end(), pat_lower.begin(), ::tolower);

        // Check for the pattern as a full command or argument
        if (cmd_lower.find(pat_lower) != std::string::npos) {
            result.is_safe = false;
            result.dangerous_patterns_found.push_back(pattern);
            result.warning = "Command contains dangerous pattern: " + pattern;
        }
    }

    // Additional checks for pipe to dangerous operations
    if (cmd.find("|") != std::string::npos) {
        result.warning += " Command uses pipes - review required.";
    }

    return result;
}

void Permission::add_dangerous_pattern(std::string_view pattern) {
    m_dangerous_patterns.push_back(std::string(pattern));
}

void Permission::remove_dangerous_pattern(std::string_view pattern) {
    m_dangerous_patterns.erase(
        std::remove(m_dangerous_patterns.begin(), m_dangerous_patterns.end(), pattern),
        m_dangerous_patterns.end());
}

std::vector<std::string> Permission::list_dangerous_patterns() const {
    return m_dangerous_patterns;
}

bool Permission::should_ask(std::string_view tool_name) const {
    return get_tool_permission(tool_name) == PermissionLevel::Ask;
}

bool Permission::is_allowed(std::string_view tool_name) const {
    return get_tool_permission(tool_name) != PermissionLevel::Deny;
}

} // namespace agent
