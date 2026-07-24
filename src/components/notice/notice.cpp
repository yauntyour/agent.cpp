#include "components/notice/notice.hpp"
#include <algorithm>

namespace agent {

void Notice::on_initialize() {}
void Notice::on_shutdown() {}

void Notice::send(const NoticeEvent& event) {
    m_history.push_back(event);
    if (m_history.size() > MAX_HISTORY) {
        m_history.erase(m_history.begin());
    }

    for (auto& handler : m_handlers) {
        handler(event);
    }
}

void Notice::info(std::string_view title, std::string_view message) {
    NoticeEvent e;
    e.type = NoticeEvent::Type::Info;
    e.title = title;
    e.message = message;
    e.timestamp = std::chrono::system_clock::now();
    send(e);
}

void Notice::warn(std::string_view title, std::string_view message) {
    NoticeEvent e;
    e.type = NoticeEvent::Type::Warning;
    e.title = title;
    e.message = message;
    e.timestamp = std::chrono::system_clock::now();
    send(e);
}

void Notice::error(std::string_view title, std::string_view message) {
    NoticeEvent e;
    e.type = NoticeEvent::Type::Error;
    e.title = title;
    e.message = message;
    e.timestamp = std::chrono::system_clock::now();
    send(e);
}

void Notice::background_completed(std::string_view task_name, std::string_view result) {
    NoticeEvent e;
    e.type = NoticeEvent::Type::BackgroundTaskCompleted;
    e.title = "Task Completed";
    e.message = std::string(task_name) + ": " + std::string(result);
    e.source = task_name;
    e.timestamp = std::chrono::system_clock::now();
    send(e);
}

void Notice::context_threshold(double percent_used) {
    NoticeEvent e;
    e.type = NoticeEvent::Type::ContextThreshold;
    e.title = "Context Threshold";
    e.message = "Context usage at " + std::to_string(static_cast<int>(percent_used)) +
                "%. Consider summarizing and resetting context.";
    e.timestamp = std::chrono::system_clock::now();
    send(e);
}

void Notice::permission_request(std::string_view tool_name, std::string_view details) {
    NoticeEvent e;
    e.type = NoticeEvent::Type::PermissionRequest;
    e.title = "Permission Required";
    e.message = std::string(details);
    e.source = tool_name;
    e.action_required = "approve";
    e.timestamp = std::chrono::system_clock::now();
    send(e);
}

void Notice::subscribe(NoticeHandler handler) {
    m_handlers.push_back(std::move(handler));
}

void Notice::unsubscribe(size_t handler_id) {
    if (handler_id < m_handlers.size()) {
        m_handlers.erase(m_handlers.begin() + handler_id);
    }
}

std::vector<NoticeEvent> Notice::recent_events(size_t count) const {
    if (count >= m_history.size()) return m_history;
    return {m_history.end() - count, m_history.end()};
}

void Notice::clear_history() {
    m_history.clear();
}

} // namespace agent
