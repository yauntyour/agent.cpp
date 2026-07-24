#include "core/lifecycle.hpp"
#include <iostream>

namespace agent {

// ── ThreadPool implementation ─────────────────────────────────────
ThreadPool::ThreadPool(size_t threads) {
    for (size_t i = 0; i < threads; ++i) {
        m_workers.emplace_back([this]() {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock lock(m_mutex);
                    m_cv.wait(lock, [this]() { return m_stop.load() || !m_queue.empty(); });
                    if (m_stop.load() && m_queue.empty()) return;
                    task = std::move(m_queue.front());
                    m_queue.pop();
                }
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    m_stop.store(true);
    m_cv.notify_all();
    for (auto& worker : m_workers) {
        if (worker.joinable()) worker.join();
    }
}

void ThreadPool::wait_all() {
    while (true) {
        std::unique_lock lock(m_mutex);
        if (m_queue.empty()) break;
    }
}

size_t ThreadPool::pending() const {
    std::unique_lock lock(m_mutex);
    return m_queue.size();
}

} // namespace agent
