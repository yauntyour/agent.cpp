#include "components/service/service.hpp"
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#endif

namespace agent {

void Service::on_initialize() {}
void Service::on_shutdown() {
    stop_all();
}

std::string Service::spawn(const ServiceProcess& proc) {
    auto id = std::to_string(m_next_id++);
    auto* sproc = new ServiceProcess(proc);
    sproc->id = id;

#ifdef _WIN32
    STARTUPINFO si = {sizeof(si)};
    PROCESS_INFORMATION pi = {};

    std::string cmd = "cmd /c " + sproc->command;

    BOOL success = CreateProcessA(
        nullptr, const_cast<char*>(cmd.c_str()),
        nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr,
        sproc->working_dir.empty() ? nullptr : sproc->working_dir.c_str(),
        &si, &pi
    );

    if (!success) {
        sproc->status = "error";
        delete sproc;
        return "";
    }

    sproc->pid = pi.dwProcessId;
    sproc->status = "running";
    sproc->started_at = std::chrono::system_clock::now().time_since_epoch().count();

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
#else
    pid_t pid = fork();
    if (pid == 0) {
        // Child
        execl("/bin/sh", "sh", "-c", sproc->command.c_str(), nullptr);
        _exit(127);
    }

    sproc->pid = pid;
    sproc->status = "running";
    sproc->started_at = std::chrono::system_clock::now().time_since_epoch().count();
#endif

    {
        std::lock_guard lock(m_mutex);
        m_processes[id] = std::unique_ptr<ServiceProcess>(sproc);
    }

    return id;
}

void Service::stop(std::string_view service_id) {
    std::lock_guard lock(m_mutex);
    auto it = m_processes.find(std::string(service_id));
    if (it == m_processes.end()) return;

#ifdef _WIN32
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, it->second->pid);
    if (h) {
        TerminateProcess(h, 0);
        CloseHandle(h);
    }
#else
    kill(it->second->pid, SIGTERM);
#endif

    it->second->status = "stopped";
    it->second->finished_at = std::chrono::system_clock::now().time_since_epoch().count();
}

void Service::stop_all() {
    std::lock_guard lock(m_mutex);
    for (auto& [id, proc] : m_processes) {
        if (proc->status == "running") {
#ifdef _WIN32
            HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, proc->pid);
            if (h) {
                TerminateProcess(h, 0);
                CloseHandle(h);
            }
#else
            kill(proc->pid, SIGTERM);
#endif
            proc->status = "stopped";
            proc->finished_at = std::chrono::system_clock::now().time_since_epoch().count();
        }
    }
}

void Service::restart(std::string_view service_id) {
    stop(service_id);
    std::lock_guard lock(m_mutex);
    auto it = m_processes.find(std::string(service_id));
    if (it == m_processes.end()) return;

    // Respawn
    auto proc = *it->second;
    m_processes.erase(it);
    auto new_id = spawn(proc);
}

ServiceProcess Service::status(std::string_view service_id) {
    std::lock_guard lock(m_mutex);
    auto it = m_processes.find(std::string(service_id));
    if (it == m_processes.end()) return {};
    return *it->second;
}

std::vector<ServiceProcess> Service::list_services() const {
    std::lock_guard lock(const_cast<std::mutex&>(m_mutex));
    std::vector<ServiceProcess> services;
    for (auto& [id, proc] : m_processes) {
        services.push_back(*proc);
    }
    return services;
}

bool Service::is_running(std::string_view service_id) const {
    std::lock_guard lock(const_cast<std::mutex&>(m_mutex));
    auto it = m_processes.find(std::string(service_id));
    return it != m_processes.end() && it->second->status == "running";
}

void Service::register_backend(std::string_view name, std::string_view command) {
    m_backends[std::string(name)] = std::string(command);
}

void Service::unregister_backend(std::string_view name) {
    m_backends.erase(std::string(name));
}

std::string Service::start_task(std::string_view name, std::string_view command, bool background) {
    ServiceProcess proc;
    proc.name = name;
    proc.command = command;
    proc.working_dir = ".";

    auto id = spawn(proc);
    if (!background) {
        // Wait for completion (simple polling)
        for (int i = 0; i < 600; ++i) {
            if (!is_running(id)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return id;
}

} // namespace agent
