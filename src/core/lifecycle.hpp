#pragma once
#include <utility>
#include <coroutine>
#include <functional>
#include <future>
#include <queue>
#include <thread>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include "module.hpp"

namespace agent {

// ── Simple task awaiter for coroutine integration ─────────────────
struct task_awaiter {
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
};

// ── Generator for async streaming ─────────────────────────────────
template<typename T>
struct generator {
    struct promise_type {
        T current_value;
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        std::suspend_always yield_value(T value) {
            current_value = std::move(value);
            return {};
        }
        generator get_return_object() {
            return generator{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        void unhandled_exception() { std::terminate(); }
        void return_void() {}
    };

    struct iterator {
        std::coroutine_handle<promise_type> handle;
        bool operator!=(std::default_sentinel_t) const { return !handle.done(); }
        iterator& operator++() { handle.resume(); return *this; }
        T& operator*() { return handle.promise().current_value; }
    };

    generator(std::coroutine_handle<promise_type> h) : handle(h) {}
    ~generator() { if (handle) handle.destroy(); }
    generator(const generator&) = delete;
    generator& operator=(const generator&) = delete;
    generator(generator&& other) noexcept : handle(std::exchange(other.handle, nullptr)) {}
    generator& operator=(generator&& other) noexcept {
        if (this != &other) {
            if (handle) handle.destroy();
            handle = std::exchange(other.handle, nullptr);
        }
        return *this;
    }

    iterator begin() {
        handle.resume();
        return {handle};
    }
    std::default_sentinel_t end() { return {}; }

private:
    std::coroutine_handle<promise_type> handle;
};

// ── Thread pool for async task execution ──────────────────────────
class ThreadPool {
public:
    explicit ThreadPool(size_t threads = std::thread::hardware_concurrency());
    ~ThreadPool();

    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using result_type = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<result_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        auto future = task->get_future();
        {
            std::unique_lock lock(m_mutex);
            m_queue.emplace([task]() { (*task)(); });
        }
        m_cv.notify_one();
        return future;
    }

    void wait_all();
    size_t pending() const;

private:
    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_stop{false};
};

// ── Scope guard / RAII defer ──────────────────────────────────────
template<typename F>
class ScopeGuard {
public:
    explicit ScopeGuard(F&& f) : m_func(std::forward<F>(f)), m_active(true) {}
    ~ScopeGuard() { if (m_active) m_func(); }
    ScopeGuard(const ScopeGuard&) = delete;
    ScopeGuard& operator=(const ScopeGuard&) = delete;
    ScopeGuard(ScopeGuard&& other) noexcept : m_func(std::move(other.m_func)), m_active(other.m_active) {
        other.m_active = false;
    }
    void dismiss() { m_active = false; }

private:
    F m_func;
    bool m_active;
};

template<typename F>
auto make_scope_guard(F&& f) -> ScopeGuard<F> {
    return ScopeGuard<F>(std::forward<F>(f));
}

} // namespace agent
