// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <cstddef>
#include <queue>
#include <thread>
#include <future>
#include <functional>
#include <vector>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include "snc/base/Common.h"
#include "snc/base/Time.h"

namespace snc {
namespace base {

class RingBuffer {
public:
    explicit RingBuffer(size_t capacity) : cap_(capacity + 1), buf_(capacity + 1, 0), head_(0), tail_(0) {}

    size_t Write(const uint8_t* src, size_t n) {
        size_t avail = Space();
        size_t wrote = (n < avail) ? n : avail;
        for (size_t i = 0; i < wrote; ++i) {
            buf_[tail_] = src[i];
            tail_ = (tail_ + 1) % cap_;
        }
        return wrote;
    }

    size_t Read(uint8_t* dst, size_t n) {
        size_t avail = Size();
        size_t read = (n < avail) ? n : avail;
        for (size_t i = 0; i < read; ++i) {
            dst[i] = buf_[head_];
            head_ = (head_ + 1) % cap_;
        }
        return read;
    }

    void Drain(size_t n) {
        n = (n < Size()) ? n : Size();
        head_ = (head_ + n) % cap_;
    }

    bool Empty() const { return head_ == tail_; }
    bool Full() const { return (tail_ + 1) % cap_ == head_; }
    size_t Capacity() const { return cap_ - 1; }
    size_t Size() const {
        if (tail_ >= head_) return tail_ - head_;
        return cap_ - head_ + tail_;
    }
    size_t Space() const { return Capacity() - Size(); }

private:
    const size_t cap_;
    std::vector<uint8_t> buf_;
    size_t head_;
    size_t tail_;
};

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(size_t cap) : cap_(cap) {}

    bool Enqueue(T item) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (q_.size() >= cap_) return false;
        q_.push(std::move(item));
        return true;
    }

    bool Dequeue(T& item, long long timeout_ms = -1) {
        std::unique_lock<std::mutex> lk(mtx_);
        if (timeout_ms < 0) {
            cv_.wait(lk, [&]{ return !q_.empty(); });
        } else {
            cv_.wait_for(lk, std::chrono::milliseconds(timeout_ms), [&]{ return !q_.empty(); });
        }
        if (q_.empty()) return false;
        item = std::move(q_.front());
        q_.pop();
        return true;
    }

    T WaitAndDequeue() {
        T item;
        Dequeue(item);
        return item;
    }

    size_t Size() {
        std::lock_guard<std::mutex> lk(mtx_);
        return q_.size();
    }
    size_t Capacity() const { return cap_; }

private:
    size_t cap_;
    std::queue<T> q_;
    std::mutex mtx_;
    std::condition_variable cv_;
};

template <typename T>
class SPSCQueue {
public:
    SPSCQueue() : SPSCQueue(256) {}
    explicit SPSCQueue(size_t cap) : cap_(cap), buf_(cap, T{}), head_(0), tail_(0) {}

    bool Push(T item) {
        if ((tail_ + 1) % cap_ == head_) return false;
        buf_[tail_] = std::move(item);
        tail_ = (tail_ + 1) % cap_;
        return true;
    }

    bool Pop(T& item) {
        if (head_ == tail_) return false;
        item = std::move(buf_[head_]);
        head_ = (head_ + 1) % cap_;
        return true;
    }

    bool Empty() const { return head_ == tail_; }
    size_t Size() const {
        if (tail_ >= head_) return tail_ - head_;
        return cap_ - head_ + tail_;
    }

private:
    const size_t cap_;
    std::vector<T> buf_;
    size_t head_;
    size_t tail_;
};

class AtomicCounter {
public:
    explicit AtomicCounter(int64_t val = 0) : val_(val) {}
    void Increment() { ++val_; }
    void Decrement() { --val_; }
    void Add(int64_t n) { val_ += n; }
    void Reset() { val_ = 0; }
    int64_t Get() const { return val_; }
private:
    std::atomic<int64_t> val_;
};

class ThreadPool {
public:
    ThreadPool(size_t size, const std::string& name = "") : name_(name), stop_(false), joined_(false) {
        for (size_t i = 0; i < size; ++i) {
            workers_.emplace_back([this]{ WorkerLoop(); });
        }
    }
    ~ThreadPool() { Stop(); }

    template <typename F>
    auto Submit(F&& f) -> std::future<decltype(f())> {
        using RetType = decltype(f());
        auto task = std::make_shared<std::packaged_task<RetType()>>(std::forward<F>(f));
        std::future<RetType> result = task->get_future();
        {
            std::lock_guard<std::mutex> lk(mtx_);
            queue_.push([task]{ (*task)(); });
        }
        cv_.notify_one();
        return result;
    }

    void Stop() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (stop_) return;
            stop_ = true;
        }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
        {
            std::lock_guard<std::mutex> lk(mtx_);
            joined_ = true;
        }
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

    std::string name_;
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> queue_;
    std::mutex mtx_;
    std::condition_variable cv_;
    bool stop_;
    bool joined_;
};

} // namespace base
} // namespace snc
