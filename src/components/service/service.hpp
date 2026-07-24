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
#include <thread>

namespace agent {

struct ServiceProcess {
    std::string id;
    std::string name;
    std::string command;
    std::string working_dir;
    int pid = -1;
    std::string status;
    int exit_code = -1;
    int64_t started_at;
    int64_t finished_at;

    using OutputCallback = std::function<void(std::string_view line)>;
    OutputCallback on_stdout;
    OutputCallback on_stderr;
    std::function<void(int exit_code)> on_exit;
};

class StdioProcess {
public:
    StdioProcess() = default;
    ~StdioProcess();

    StdioProcess(const StdioProcess&) = delete;
    StdioProcess& operator=(const StdioProcess&) = delete;
    StdioProcess(StdioProcess&& other) noexcept;
    StdioProcess& operator=(StdioProcess&& other) noexcept;

    bool spawn(std::string_view command,
               const std::vector<std::string>& args = {},
               const std::string& working_dir = ".",
               const std::map<std::string, std::string>& env = {});

    bool write(std::string_view data);
    std::string read_line(int timeout_ms = 5000);
    std::string read_bytes(size_t count, int timeout_ms = 5000);
    bool has_data(int timeout_ms = 100);

    void close_stdin();
    void terminate();
    bool is_running() const;
    int wait(int timeout_ms = -1);

private:
    void cleanup();

#ifdef _WIN32
    void* m_hProcess = nullptr;
    void* m_hStdinWrite = nullptr;
    void* m_hStdoutRead = nullptr;
    void* m_hStderrRead = nullptr;
    int m_pid = -1;
#else
    int m_pid = -1;
    int m_stdin_fd = -1;
    int m_stdout_fd = -1;
    int m_stderr_fd = -1;
#endif
    bool m_running = false;

public:
    int pid() const { return m_pid; }
};

class Service : public Module<Service> {
public:
    static constexpr std::string_view static_name() { return "service"; }

    void on_initialize();
    void on_shutdown();

    std::string spawn(const ServiceProcess& proc);
    void stop(std::string_view service_id);
    void stop_all();
    void restart(std::string_view service_id);

    ServiceProcess status(std::string_view service_id);
    std::vector<ServiceProcess> list_services() const;
    bool is_running(std::string_view service_id) const;

    void register_backend(std::string_view name, std::string_view command);
    void unregister_backend(std::string_view name);

    std::string start_task(std::string_view name, std::string_view command, bool background);

private:
    std::map<std::string, std::unique_ptr<ServiceProcess>> m_processes;
    std::map<std::string, std::string> m_backends;
    std::atomic<int> m_next_id{1};
    std::mutex m_mutex;
};

} // namespace agent
