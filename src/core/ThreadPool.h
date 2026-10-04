#ifndef WINBOT_CORE_THREADPOOL_H
#define WINBOT_CORE_THREADPOOL_H
#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

// ── ThreadPool ────────────────────────────────────────────────────────────────
// A modern, lightweight C++23 ThreadPool designed for asynchronous task
// dispatching. Uses std::jthread and std::move_only_function to support
// move-only captures without extra allocation overhead.
class ThreadPool {
public:
    explicit ThreadPool(size_t numThreads = 0) {
        if (numThreads == 0) {
            numThreads = (std::max<size_t>)(1U, std::thread::hardware_concurrency());
        }
        m_workers.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            m_workers.emplace_back([this](const std::stop_token& st) {
                workerLoop(st);
            });
        }
    }

    ~ThreadPool() {
        stop();
    }

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // Enqueue a task to be executed by a worker thread.
    // Returns true if successfully enqueued, false if the pool is stopping.
    bool enqueue(std::move_only_function<void()> task) {
        {
            std::scoped_lock lock(m_mutex);
            if (m_stopping) {
                return false;
            }
            m_tasks.push(std::move(task));
            ++m_activeTasks;
        }
        m_cv.notify_one();
        return true;
    }

    // Wait until all queued and active tasks have finished execution.
    void waitIdle() {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_idleCv.wait(lock, [&]() {
            return m_activeTasks == 0 && m_tasks.empty();
        });
    }

    // Stop accepting new tasks, signal workers, and join all threads.
    void stop() {
        {
            std::scoped_lock lock(m_mutex);
            if (m_stopping) {
                return;
            }
            m_stopping = true;
        }
        m_cv.notify_all();
        m_idleCv.notify_all();
        for (auto& worker : m_workers) {
            worker.request_stop();
        }
        for (auto& worker : m_workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    [[nodiscard]] size_t threadCount() const noexcept {
        return m_workers.size();
    }

private:
    void workerLoop(const std::stop_token& st) {
        while (!st.stop_requested()) {
            std::move_only_function<void()> task;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [&]() {
                    return m_stopping || st.stop_requested() || !m_tasks.empty();
                });
                if ((m_stopping || st.stop_requested()) && m_tasks.empty()) {
                    return;
                }
                if (!m_tasks.empty()) {
                    task = std::move(m_tasks.front());
                    m_tasks.pop();
                }
            }
            if (task) {
                try {
                    task();
                } catch (const std::exception& e) {
                    (void)e; // Suppress unhandled exceptions escaping the worker thread
                } catch (...) { // NOLINT(bugprone-empty-catch)
                    // Suppress unhandled non-std exceptions escaping the worker thread
                }
                {
                    std::scoped_lock lock(m_mutex);
                    --m_activeTasks;
                }
                m_idleCv.notify_all();
            }
        }
    }

    std::vector<std::jthread>                   m_workers;
    std::queue<std::move_only_function<void()>> m_tasks;
    std::mutex                                  m_mutex;
    std::condition_variable                     m_cv;
    std::condition_variable                     m_idleCv;
    size_t                                      m_activeTasks{0};
    bool                                        m_stopping{false};
};

#endif // WINBOT_CORE_THREADPOOL_H
