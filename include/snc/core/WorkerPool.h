#pragma once

#include <thread>
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>
#include <chrono>
#include "snc/base/Log.h"

namespace snc {
namespace core {

class WorkerPool {
public:
    using Ptr = std::shared_ptr<WorkerPool>;

    static Ptr Create(size_t size) {
        return std::make_shared<WorkerPool>(size);
    }

    explicit WorkerPool(size_t size) : stop_(false), joined_(false) {
        for (size_t i = 0; i < size; ++i) {
            workers_.emplace_back([this]{ WorkerLoop(); });
        }
    }
    ~WorkerPool() { Stop(); }

    void Stop() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (joined_) return;
            stop_ = true;
        }
        cv_.notify_all();
        // Wait up to 5 seconds for workers to finish
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for (auto& t : workers_) {
            if (t.joinable()) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    SNC_LOG_WARN("workerpool", "Worker thread did not stop in time, detaching");
                    t.detach();
                } else {
                    t.join();
                }
            }
        }
        {
            std::lock_guard<std::mutex> lk(mtx_);
            joined_ = true;
        }
    }

    void Submit(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            queue_.push(std::move(task));
        }
        cv_.notify_one();
    }

private:
    void WorkerLoop() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [&]{ return stop_ || !queue_.empty(); });
                if (stop_ && queue_.empty()) return;
                task = std::move(queue_.front());
                queue_.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
    std::atomic<bool> stop_;
    bool joined_;
};

} // namespace core
} // namespace snc
