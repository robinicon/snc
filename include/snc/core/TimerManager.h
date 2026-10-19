// SPDX-License-Identifier: MIT
// Copyright (c) 2024–2026 SNC Project Contributors
// This source code is subject to the terms of the MIT License.
// See LICENSE for details.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <vector>
#include "snc/base/Time.h"

namespace snc {
namespace core {

using TimerCallback = std::function<void()>;

struct TimerEntry {
    int64_t id;
    long long expireMs;
    bool repeating;
    long long repeatMs = 0;   // for repeating timers: re-arm offset from trigger time
    TimerCallback cb;
};

class TimerManager {
public:
    TimerManager() : nextId_(1) {}

    int64_t AddTimer(long long ms, TimerCallback cb) {
        std::lock_guard<std::mutex> lk(mtx_);
        int64_t id = nextId_++;
        TimerEntry entry;
        entry.id = id;
        entry.expireMs = (long long)(base::NowMilliSeconds() + ms);
        entry.repeating = false;
        entry.cb = std::move(cb);
        timers_.emplace(id, std::move(entry));
        return id;
    }

    int64_t AddRepeatingTimer(long long ms, TimerCallback cb) {
        std::lock_guard<std::mutex> lk(mtx_);
        int64_t id = nextId_++;
        TimerEntry entry;
        entry.id = id;
        entry.expireMs = (long long)(base::NowMilliSeconds() + ms);
        entry.repeating = true;
        entry.repeatMs = ms;
        entry.cb = std::move(cb);
        timers_.emplace(id, std::move(entry));
        return id;
    }

    bool CancelTimer(int64_t id) {
        std::lock_guard<std::mutex> lk(mtx_);
        return timers_.erase(id) > 0;
    }

    void ProcessTimers() {
        long long now = base::NowMilliSeconds();
        std::vector<std::pair<int64_t, TimerCallback>> expired;  // (id, cb) pairs
        {
            std::lock_guard<std::mutex> lk(mtx_);
            for (auto it = timers_.begin(); it != timers_.end(); ) {
                if (it->second.expireMs <= now) {
                    int64_t id = it->first;
                    TimerCallback cb = it->second.cb;   // copy cb BEFORE erasing
                    if (it->second.repeating) {
                        it->second.expireMs = now + it->second.repeatMs;
                        ++it;
                    } else {
                        it = timers_.erase(it);
                    }
                    expired.push_back({id, std::move(cb)});
                } else {
                    ++it;
                }
            }
        }
        for (auto& p : expired) {
            TimerCallback cb = std::move(p.second);
            if (cb) cb();
        }
    }

    long long NextTimeoutMs() {
        std::lock_guard<std::mutex> lk(mtx_);
        if (timers_.empty()) return -1;
        long long now = base::NowMilliSeconds();
        // timers_ is sorted by expireMs (map key is id, not expireMs — so scan first entry only won't work).
        // Use min-heap of (expireMs, id) for O(1) peek instead.
        // For now keep O(N) but prune already-fired timers to reduce work.
        long long minExpire = -1;
        for (auto it = timers_.begin(); it != timers_.end(); ++it) {
            if (it->second.expireMs <= now) {
                // Already expired; fire the callback and remove
                TimerCallback cb = it->second.cb;
                if (it->second.repeating) {
                    it->second.expireMs = now + it->second.repeatMs;
                } else {
                    it = timers_.erase(it);
                    continue;
                }
                if (cb) cb();
            } else if (minExpire < 0 || it->second.expireMs - now < minExpire) {
                minExpire = it->second.expireMs - now;
            }
        }
        return minExpire;
    }

private:
    std::mutex mtx_;
    std::map<int64_t, TimerEntry> timers_;
    int64_t nextId_;
};

} // namespace core
} // namespace snc
