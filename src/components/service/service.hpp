#pragma once
#include "core/module.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>

namespace agent {

struct ServiceProcess {
    std::string id;
    std::string name;
    std::string command;
    std::string working_dir;
    int pid = -1;
    std::string status;  // "running", "stopped", "error", "completed"
    int exit_code = -1;
    int64_t started_at;
    int64_t finished_at;

    using OutputCallback = std::function<void(std::string_view line)>;
    OutputCallback on_stdout;
    OutputCallback on_stderr;
    std::function<void(int exit_code)> on_exit;
};

class Service : public Module<Service> {
public:
    static constexpr std::string_view static_name() { return "service"; }

    void on_initialize() override;
    void on_shutdown() override;

    // ── Process management ─────────────────────────────────────
    std::string spawn(const ServiceProcess& proc);
    void stop(std::string_view service_id);
    void stop_all();
    void restart(std::string_view service_id);

    // ── Status queries ─────────────────────────────────────────
    ServiceProcess status(std::string_view service_id);
    std::vector<ServiceProcess> list_services() const;
    bool is_running(std::string_view service_id) const;

    // ── Backend services (auto-start with system) ──────────────
    void register_backend(std::string_view name, std::string_view command);
    void unregister_backend(std::string_view name);

    // ── Task tool integration ──────────────────────────────────
    std::string start_task(std::string_view name, std::string_view command, bool background);

private:
    std::map<std::string, std::unique_ptr<ServiceProcess>> m_processes;
    std::map<std::string, std::string> m_backends;
    std::atomic<int> m_next_id{1};
    std::mutex m_mutex;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace agent
