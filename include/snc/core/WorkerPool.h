#pragma once

#include <thread>
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <vector>
#include <atomic>

namespace snc {
namespace core {

class WorkerPool {
public:
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
        for (auto& t : workers_) t.join();
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
