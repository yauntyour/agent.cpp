#include "components/service/service.hpp"
#include <chrono>
#include <thread>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <poll.h>
#endif

namespace agent {

// ── StdioProcess ──────────────────────────────────────────────────

StdioProcess::~StdioProcess() {
    cleanup();
}

StdioProcess::StdioProcess(StdioProcess&& other) noexcept {
#ifdef _WIN32
    m_hProcess = other.m_hProcess;
    m_hStdinWrite = other.m_hStdinWrite;
    m_hStdoutRead = other.m_hStdoutRead;
    m_hStderrRead = other.m_hStderrRead;
    other.m_hProcess = nullptr;
    other.m_hStdinWrite = nullptr;
    other.m_hStdoutRead = nullptr;
    other.m_hStderrRead = nullptr;
#else
    m_pid = other.m_pid;
    m_stdin_fd = other.m_stdin_fd;
    m_stdout_fd = other.m_stdout_fd;
    m_stderr_fd = other.m_stderr_fd;
    other.m_pid = -1;
    other.m_stdin_fd = -1;
    other.m_stdout_fd = -1;
    other.m_stderr_fd = -1;
#endif
    m_running = other.m_running;
    other.m_running = false;
}

StdioProcess& StdioProcess::operator=(StdioProcess&& other) noexcept {
    if (this != &other) {
        cleanup();
#ifdef _WIN32
        m_hProcess = other.m_hProcess;
        m_hStdinWrite = other.m_hStdinWrite;
        m_hStdoutRead = other.m_hStdoutRead;
        m_hStderrRead = other.m_hStderrRead;
        other.m_hProcess = nullptr;
        other.m_hStdinWrite = nullptr;
        other.m_hStdoutRead = nullptr;
        other.m_hStderrRead = nullptr;
#else
        m_pid = other.m_pid;
        m_stdin_fd = other.m_stdin_fd;
        m_stdout_fd = other.m_stdout_fd;
        m_stderr_fd = other.m_stderr_fd;
        other.m_pid = -1;
        other.m_stdin_fd = -1;
        other.m_stdout_fd = -1;
        other.m_stderr_fd = -1;
#endif
        m_running = other.m_running;
        other.m_running = false;
    }
    return *this;
}

bool StdioProcess::spawn(std::string_view command,
                          const std::vector<std::string>& args,
                          const std::string& working_dir,
                          const std::map<std::string, std::string>& env) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hStdinRead = nullptr, hStdinWrite = nullptr;
    HANDLE hStdoutRead = nullptr, hStdoutWrite = nullptr;
    HANDLE hStderrRead = nullptr, hStderrWrite = nullptr;

    if (!CreatePipe(&hStdinRead, &hStdinWrite, &sa, 0)) return false;
    SetHandleInformation(hStdinWrite, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0)) {
        CloseHandle(hStdinRead); CloseHandle(hStdinWrite);
        return false;
    }
    SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);

    if (!CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0)) {
        CloseHandle(hStdinRead); CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead); CloseHandle(hStdoutWrite);
        return false;
    }
    SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = hStdinRead;
    si.hStdOutput = hStdoutWrite;
    si.hStdError = hStderrWrite;

    PROCESS_INFORMATION pi = {};

    std::string cmd = std::string(command);
    for (auto& a : args) cmd += " " + a;

    BOOL success = CreateProcessA(
        nullptr, const_cast<char*>(cmd.c_str()),
        nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr,
        working_dir.empty() ? nullptr : working_dir.c_str(),
        &si, &pi
    );

    CloseHandle(hStdinRead);
    CloseHandle(hStdoutWrite);
    CloseHandle(hStderrWrite);

    if (!success) {
        CloseHandle(hStdinWrite);
        CloseHandle(hStdoutRead);
        CloseHandle(hStderrRead);
        return false;
    }

    m_hProcess = pi.hProcess;
    m_hStdinWrite = hStdinWrite;
    m_hStdoutRead = hStdoutRead;
    m_hStderrRead = hStderrRead;
    m_pid = pi.dwProcessId;
    m_running = true;

    CloseHandle(pi.hThread);
#else
    int stdin_pipe[2], stdout_pipe[2], stderr_pipe[2];
    if (pipe(stdin_pipe) != 0) return false;
    if (pipe(stdout_pipe) != 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        return false;
    }
    if (pipe(stderr_pipe) != 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        return false;
    }

    if (pid == 0) {
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);

        close(stdin_pipe[0]);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);

        if (!working_dir.empty()) {
            if (chdir(working_dir.c_str()) != 0) _exit(127);
        }

        for (auto& [k, v] : env) {
            setenv(k.c_str(), v.c_str(), 1);
        }

        std::vector<char*> argv;
        std::string cmd_str(command);
        argv.push_back(const_cast<char*>(cmd_str.c_str()));
        for (auto& a : args) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        execvp(command.data(), argv.data());
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    m_pid = pid;
    m_stdin_fd = stdin_pipe[1];
    m_stdout_fd = stdout_pipe[0];
    m_stderr_fd = stderr_pipe[0];
    m_running = true;

    fcntl(m_stdout_fd, F_SETFL, O_NONBLOCK);
    fcntl(m_stderr_fd, F_SETFL, O_NONBLOCK);
#endif

    return true;
}

bool StdioProcess::write(std::string_view data) {
    if (!m_running) return false;

#ifdef _WIN32
    DWORD written = 0;
    BOOL ok = WriteFile(m_hStdinWrite, data.data(), (DWORD)data.size(), &written, nullptr);
    FlushFileBuffers(m_hStdinWrite);
    return ok && written == data.size();
#else
    ssize_t n = ::write(m_stdin_fd, data.data(), data.size());
    return n == (ssize_t)data.size();
#endif
}

std::string StdioProcess::read_line(int timeout_ms) {
    if (!m_running) return "";

    std::string line;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
#ifdef _WIN32
        DWORD available = 0;
        if (!PeekNamedPipe(m_hStdoutRead, nullptr, 0, nullptr, &available, nullptr) || available == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        char ch;
        DWORD read = 0;
        if (ReadFile(m_hStdoutRead, &ch, 1, &read, nullptr) && read == 1) {
            if (ch == '\n') return line;
            if (ch != '\r') line += ch;
        }
#else
        struct pollfd pfd = {m_stdout_fd, POLLIN, 0};
        int ret = poll(&pfd, 1, 50);
        if (ret <= 0) continue;

        char ch;
        ssize_t n = ::read(m_stdout_fd, &ch, 1);
        if (n == 1) {
            if (ch == '\n') return line;
            if (ch != '\r') line += ch;
        } else if (n == 0) {
            m_running = false;
            return line;
        }
#endif
    }
    return line;
}

std::string StdioProcess::read_bytes(size_t count, int timeout_ms) {
    if (!m_running || count == 0) return "";

    std::string buf(count, '\0');
    size_t total = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (total < count && std::chrono::steady_clock::now() < deadline) {
#ifdef _WIN32
        DWORD available = 0;
        PeekNamedPipe(m_hStdoutRead, nullptr, 0, nullptr, &available, nullptr);
        if (available == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        DWORD to_read = std::min((DWORD)(count - total), available);
        DWORD read = 0;
        if (ReadFile(m_hStdoutRead, buf.data() + total, to_read, &read, nullptr)) {
            total += read;
        }
#else
        struct pollfd pfd = {m_stdout_fd, POLLIN, 0};
        int ret = poll(&pfd, 1, 50);
        if (ret <= 0) continue;

        ssize_t n = ::read(m_stdout_fd, buf.data() + total, count - total);
        if (n > 0) total += n;
        else if (n == 0) { m_running = false; break; }
#endif
    }

    buf.resize(total);
    return buf;
}

bool StdioProcess::has_data(int timeout_ms) {
    if (!m_running) return false;

#ifdef _WIN32
    DWORD available = 0;
    PeekNamedPipe(m_hStdoutRead, nullptr, 0, nullptr, &available, nullptr);
    if (available > 0) return true;
    if (timeout_ms > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(timeout_ms, 50)));
        PeekNamedPipe(m_hStdoutRead, nullptr, 0, nullptr, &available, nullptr);
        return available > 0;
    }
    return false;
#else
    struct pollfd pfd = {m_stdout_fd, POLLIN, 0};
    return poll(&pfd, 1, timeout_ms) > 0;
#endif
}

void StdioProcess::close_stdin() {
#ifdef _WIN32
    if (m_hStdinWrite) {
        CloseHandle(m_hStdinWrite);
        m_hStdinWrite = nullptr;
    }
#else
    if (m_stdin_fd >= 0) {
        close(m_stdin_fd);
        m_stdin_fd = -1;
    }
#endif
}

void StdioProcess::terminate() {
    if (!m_running) return;

#ifdef _WIN32
    if (m_hProcess) {
        TerminateProcess(m_hProcess, 1);
    }
#else
    if (m_pid > 0) {
        kill(m_pid, SIGTERM);
    }
#endif
    m_running = false;
}

bool StdioProcess::is_running() const {
    if (!m_running) return false;

#ifdef _WIN32
    if (!m_hProcess) return false;
    DWORD exit_code;
    if (GetExitCodeProcess(m_hProcess, &exit_code)) {
        if (exit_code != STILL_ACTIVE) {
            const_cast<StdioProcess*>(this)->m_running = false;
            return false;
        }
    }
    return true;
#else
    if (m_pid <= 0) return false;
    int status;
    pid_t result = waitpid(m_pid, &status, WNOHANG);
    if (result == 0) return true;
    const_cast<StdioProcess*>(this)->m_running = false;
    return false;
#endif
}

int StdioProcess::wait(int timeout_ms) {
    if (!m_running) return -1;

#ifdef _WIN32
    DWORD wait_time = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
    WaitForSingleObject(m_hProcess, wait_time);
    DWORD exit_code = 0;
    GetExitCodeProcess(m_hProcess, &exit_code);
    m_running = false;
    return (int)exit_code;
#else
    if (timeout_ms < 0) {
        int status;
        waitpid(m_pid, &status, 0);
        m_running = false;
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    } else {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline) {
            int status;
            pid_t r = waitpid(m_pid, &status, WNOHANG);
            if (r > 0) {
                m_running = false;
                return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return -1;
    }
#endif
}

void StdioProcess::cleanup() {
    terminate();

#ifdef _WIN32
    if (m_hStdinWrite) { CloseHandle(m_hStdinWrite); m_hStdinWrite = nullptr; }
    if (m_hStdoutRead) { CloseHandle(m_hStdoutRead); m_hStdoutRead = nullptr; }
    if (m_hStderrRead) { CloseHandle(m_hStderrRead); m_hStderrRead = nullptr; }
    if (m_hProcess) { CloseHandle(m_hProcess); m_hProcess = nullptr; }
#else
    if (m_stdin_fd >= 0) { close(m_stdin_fd); m_stdin_fd = -1; }
    if (m_stdout_fd >= 0) { close(m_stdout_fd); m_stdout_fd = -1; }
    if (m_stderr_fd >= 0) { close(m_stderr_fd); m_stderr_fd = -1; }
    m_pid = -1;
#endif
    m_running = false;
}

// ── Service ───────────────────────────────────────────────────────

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
        for (int i = 0; i < 600; ++i) {
            if (!is_running(id)) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    return id;
}

} // namespace agent
