#pragma once
#include "core/module.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <chrono>

namespace agent {

struct NoticeEvent {
    enum class Type {
        Info,
        Warning,
        Error,
        BackgroundTaskCompleted,
        ContextThreshold,
        SystemStatus,
        PermissionRequest
    };

    Type type;
    std::string title;
    std::string message;
    std::string source;
    std::chrono::system_clock::time_point timestamp;
    std::string action_required;
};

using NoticeHandler = std::function<void(const NoticeEvent&)>;

class Notice : public Module<Notice> {
public:
    static constexpr std::string_view static_name() { return "notice"; }

    void on_initialize() override;
    void on_shutdown() override;

    // ── Send notices ───────────────────────────────────────────
    void send(const NoticeEvent& event);
    void info(std::string_view title, std::string_view message);
    void warn(std::string_view title, std::string_view message);
    void error(std::string_view title, std::string_view message);
    void background_completed(std::string_view task_name, std::string_view result);
    void context_threshold(double percent_used);
    void permission_request(std::string_view tool_name, std::string_view details);

    // ── Subscription ───────────────────────────────────────────
    void subscribe(NoticeHandler handler);
    void unsubscribe(size_t handler_id);

    // ── History ────────────────────────────────────────────────
    std::vector<NoticeEvent> recent_events(size_t count = 50) const;
    void clear_history();

private:
    std::vector<NoticeHandler> m_handlers;
    std::vector<NoticeEvent> m_history;
    static constexpr size_t MAX_HISTORY = 500;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace agent
