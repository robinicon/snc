#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include "snc/core/Msg.h"

namespace snc {
namespace core {

class MessageQueue {
public:
    MessageQueue(size_t capacity = 1024, size_t highWater = 800, size_t lowWater = 200)
        : cap_(capacity), highWater_(highWater), lowWater_(lowWater) {}

    bool Enqueue(Msg::Ptr msg) {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (q_.size() >= cap_) return false;
            q_.push(std::move(msg));
        }
        cv_.notify_one();
        return true;
    }

    bool TryDequeue(Msg::Ptr& msg) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (q_.empty()) return false;
        msg = std::move(q_.front());
        q_.pop();
        return true;
    }

    bool Dequeue(Msg::Ptr& msg, long long timeout_ms = -1) {
        if (timeout_ms == 0) {
            // Non-blocking: only consume if a message is already queued.
            std::lock_guard<std::mutex> lk(mtx_);
            if (q_.empty()) return false;
            msg = std::move(q_.front());
            q_.pop();
            return true;
        }
        // Blocking mode: wait until a message is enqueued or stopped.
        std::unique_lock<std::mutex> lk(mtx_);
        if (timeout_ms > 0) {
            if (!cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&]{ return !q_.empty() || stopped_; })) {
                return false; // timeout
            }
        } else {
            cv_.wait(lk, [&]{ return !q_.empty() || stopped_; });
        }
        if (stopped_ && q_.empty()) return false;
        msg = std::move(q_.front());
        q_.pop();
        return true;
    }

    size_t Size() {
        std::lock_guard<std::mutex> lk(mtx_);
        return q_.size();
    }

    bool IsBackpressure() const {
        return q_.size() >= highWater_;
    }

    void Stop() {
        stopped_ = true;
        cv_.notify_all();
    }

private:
    size_t cap_;
    size_t highWater_;
    size_t lowWater_;
    std::queue<Msg::Ptr> q_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stopped_ = false;
};

} // namespace core
} // namespace snc
