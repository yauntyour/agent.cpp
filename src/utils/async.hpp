#pragma once
#include <string>
#include <vector>
#include <mutex>
#include <queue>
#include <functional>
#include <condition_variable>

namespace agent::async {

template<typename T>
class Channel {
public:
    void send(T value) {
        {
            std::unique_lock lock(m_mutex);
            m_queue.push(std::move(value));
        }
        m_cv.notify_one();
    }

    T receive() {
        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this]() { return !m_queue.empty() || m_closed; });
        if (m_queue.empty()) throw std::runtime_error("Channel closed");
        T value = std::move(m_queue.front());
        m_queue.pop();
        return value;
    }

    bool try_receive(T& value) {
        std::unique_lock lock(m_mutex);
        if (m_queue.empty()) return false;
        value = std::move(m_queue.front());
        m_queue.pop();
        return true;
    }

    void close() {
        {
            std::unique_lock lock(m_mutex);
            m_closed = true;
        }
        m_cv.notify_all();
    }

private:
    std::queue<T> m_queue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_closed = false;
};

} // namespace agent::async
