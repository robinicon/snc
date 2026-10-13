#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <functional>
#include <algorithm>

namespace snc {
namespace monitor {

/// Metric counter / gauge — lock-free by default, thread-safe snapshot
struct MetricPoint {
    uint64_t value;
    uint64_t timestamp;   // ms since epoch (approx)
};

/// Aggregate statistics for a time window
struct StatWindow {
    uint64_t count     = 0;
    uint64_t total     = 0;
    uint64_t min_val   = UINT64_MAX;
    uint64_t max_val   = 0;
    double   avg       = 0.0;
};

// ── Backpressure metrics ──────────────────────────────────────
class BackpressureStats {
public:
    BackpressureStats() = default;

    void RecordPressureEvent(uint64_t queueDepth) {
        pressCount_.fetch_add(1, std::memory_order_relaxed);
        // Manual fetch_max (not available in MSVC C++17)
        uint64_t oldMax = maxDepth_.load(std::memory_order_relaxed);
        while (queueDepth > oldMax &&
               !maxDepth_.compare_exchange_weak(oldMax, queueDepth,
                                                std::memory_order_relaxed)) {}
        // depthSamples_ protected by mtx_ to avoid data race with Snapshot()
        {
            std::lock_guard<std::mutex> lk(mtx_);
            depthSamples_.push_back(queueDepth);
            if (depthSamples_.size() > 1000) depthSamples_.erase(depthSamples_.begin());
        }
    }

    struct Report {
        uint64_t triggerCount  = 0;
        uint64_t maxDepth      = 0;
        uint64_t currentDepth  = 0;
        double   avgDepth      = 0.0;
        double   p50Depth      = 0.0;
        double   p95Depth      = 0.0;
        double   p99Depth      = 0.0;
    };

    Report Snapshot() const {
        std::lock_guard<std::mutex> lk(mtx_);
        Report r;
        r.triggerCount = pressCount_.load();
        r.maxDepth = maxDepth_.load();
        r.currentDepth = queueDepth_.load();
        r.avgDepth = avgDepth_;
        r.p50Depth = percentile(0.50);
        r.p95Depth = percentile(0.95);
        r.p99Depth = percentile(0.99);
        return r;
    }

    void SetCurrentDepth(uint64_t depth) {
        queueDepth_.store(depth, std::memory_order_relaxed);
    }

private:
    double percentile(double p) const {
        if (depthSamples_.empty()) return 0.0;
        auto sorted = depthSamples_;
        std::sort(sorted.begin(), sorted.end());
        size_t idx = (size_t)(p * (double)sorted.size());
        if (idx >= sorted.size()) idx = sorted.size() - 1;
        return (double)sorted[idx];
    }

    mutable std::mutex mtx_;
    std::atomic<uint64_t> pressCount_{0};
    std::atomic<uint64_t> maxDepth_{0};
    std::atomic<uint64_t> queueDepth_{0};
    std::atomic<double>   avgDepth_{0.0};
    std::vector<uint64_t> depthSamples_;
};

// ── IO Thread idle rate metrics ───────────────────────────────
class IoIdleStats {
public:
    IoIdleStats() = default;

    /// Call each IO loop iteration — pass true if idle (no events)
    void RecordIteration(bool idle) {
        iterCount_.fetch_add(1, std::memory_order_relaxed);
        idleCount_.fetch_add(idle ? 1 : 0, std::memory_order_relaxed);
    }

    struct Report {
        uint64_t totalIterations = 0;
        uint64_t idleIterations  = 0;
        double   idleRate        = 0.0;  // 0.0 ~ 1.0
    };

    Report Snapshot() const {
        Report r;
        r.totalIterations = iterCount_.load();
        r.idleIterations  = idleCount_.load();
        r.idleRate = r.totalIterations > 0
            ? (double)r.idleIterations / (double)r.totalIterations
            : 0.0;
        return r;
    }

private:
    std::atomic<uint64_t> iterCount_{0};
    std::atomic<uint64_t> idleCount_{0};
};

// ── Lua Worker block metrics ──────────────────────────────────
class LuaBlockStats {
public:
    LuaBlockStats() = default;

    /// Record a Lua execution that exceeded the timeout threshold
    void RecordBlock(uint64_t durationMs) {
        blockCount_.fetch_add(1, std::memory_order_relaxed);
        durationSamples_.push_back(durationMs);
        if (durationSamples_.size() > 500) durationSamples_.erase(durationSamples_.begin());
        totalBlockMs_.fetch_add(durationMs, std::memory_order_relaxed);
        // Manual fetch_max (not available in MSVC C++17)
        uint64_t oldMax = maxBlockMs_.load(std::memory_order_relaxed);
        while (durationMs > oldMax &&
               !maxBlockMs_.compare_exchange_weak(oldMax, durationMs,
                                                  std::memory_order_relaxed)) {}
    }

    struct Report {
        uint64_t blockCount    = 0;
        uint64_t totalBlockMs  = 0;
        double   avgBlockMs    = 0.0;
        double   maxBlockMs    = 0.0;
    };

    Report Snapshot() const {
        Report r;
        r.blockCount  = blockCount_.load();
        r.totalBlockMs = totalBlockMs_.load();
        r.avgBlockMs  = r.blockCount > 0
            ? (double)r.totalBlockMs / (double)r.blockCount
            : 0.0;
        r.maxBlockMs  = (double)maxBlockMs_.load();
        return r;
    }

private:
    std::atomic<uint64_t> blockCount_{0};
    std::atomic<uint64_t> totalBlockMs_{0};
    std::atomic<uint64_t> maxBlockMs_{0};
    std::vector<uint64_t> durationSamples_;
};

// ── Message queue depth stats (for all workers) ───────────────
class MsgQueueStats {
public:
    MsgQueueStats() = default;

    void RecordDepth(uint64_t depth) {
        samples_.push_back(depth);
        if (samples_.size() > 2000) samples_.erase(samples_.begin());
        current_.store(depth, std::memory_order_relaxed);
        // Manual fetch_max (not available in MSVC C++17)
        uint64_t oldMax = max_.load(std::memory_order_relaxed);
        while (depth > oldMax &&
               !max_.compare_exchange_weak(oldMax, depth,
                                           std::memory_order_relaxed)) {}
    }

    struct Report {
        uint64_t current   = 0;
        uint64_t max       = 0;
        double   p50       = 0.0;
        double   p95       = 0.0;
        double   p99       = 0.0;
        double   avg       = 0.0;
        size_t   sampleCount = 0;
    };

    Report Snapshot() const {
        std::lock_guard<std::mutex> lk(mtx_);
        Report r;
        r.current  = current_.load();
        r.max      = max_.load();
        r.sampleCount = samples_.size();
        if (samples_.empty()) return r;

        double sum = 0;
        auto sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        for (auto v : sorted) sum += (double)v;
        r.avg = sum / (double)sorted.size();
        r.p50 = percentile(sorted, 0.50);
        r.p95 = percentile(sorted, 0.95);
        r.p99 = percentile(sorted, 0.99);
        return r;
    }

private:
    static double percentile(const std::vector<uint64_t>& s, double p) {
        if (s.empty()) return 0.0;
        size_t idx = (size_t)(p * (double)s.size());
        if (idx >= s.size()) idx = s.size() - 1;
        return (double)s[idx];
    }

    mutable std::mutex mtx_;
    std::atomic<uint64_t> current_{0};
    std::atomic<uint64_t> max_{0};
    std::vector<uint64_t> samples_;
};

// ── Main Monitor — aggregates all subsystem metrics ───────────
class Monitor {
public:
    using Ptr = std::shared_ptr<Monitor>;

    static Ptr Create() { return std::make_shared<Monitor>(); }

    // ── Accessors ─────────────────────────────────────────────
    BackpressureStats&  backpressure()  { return bp_; }
    IoIdleStats&        ioIdle()        { return ioIdle_; }
    LuaBlockStats&      luaBlock()      { return luaBlock_; }
    MsgQueueStats&      msgQueue()      { return msgQueue_; }

    // ── Convenience: one-shot recording ───────────────────────
    void RecordIoIteration(bool idle) { ioIdle_.RecordIteration(idle); }
    void RecordBackpressure(uint64_t queueDepth) {
        bp_.RecordPressureEvent(queueDepth);
        msgQueue_.RecordDepth(queueDepth);
    }
    void RecordLuaBlock(uint64_t durationMs) { luaBlock_.RecordBlock(durationMs); }

    // ── Full report ───────────────────────────────────────────
    struct Report {
        BackpressureStats::Report bp;
        IoIdleStats::Report       ioIdle;
        LuaBlockStats::Report     luaBlock;
        MsgQueueStats::Report     msgQueue;
    };

    Report FullReport() const {
        Report r;
        r.bp   = bp_.Snapshot();
        r.ioIdle = ioIdle_.Snapshot();
        r.luaBlock = luaBlock_.Snapshot();
        r.msgQueue = msgQueue_.Snapshot();
        return r;
    }

    // ── Human-readable string ─────────────────────────────────
    std::string ToString() const {
        auto r = FullReport();
        std::string out;
        out += "=== SNC Monitor Report ===\n";
        out += "  [IO Idle Rate]      iterations=";
        out += std::to_string(r.ioIdle.totalIterations) +
               " idle=" + std::to_string(r.ioIdle.idleIterations) +
               " rate=" + FormatDouble(r.ioIdle.idleRate, 4) + "\n";
        out += "  [Backpressure]      triggers=" +
               std::to_string(r.bp.triggerCount) +
               " maxDepth=" + std::to_string(r.bp.maxDepth) +
               " p95=" + FormatDouble(r.bp.p95Depth, 0) +
               " p99=" + FormatDouble(r.bp.p99Depth, 0) + "\n";
        out += "  [Lua Blocks]        count=" +
               std::to_string(r.luaBlock.blockCount) +
               " totalMs=" + std::to_string(r.luaBlock.totalBlockMs) +
               " avg=" + FormatDouble(r.luaBlock.avgBlockMs, 1) + "ms" +
               " max=" + std::to_string((uint64_t)r.luaBlock.maxBlockMs) + "ms\n";
        out += "  [Msg Queue]         current=" +
               std::to_string(r.msgQueue.current) +
               " max=" + std::to_string(r.msgQueue.max) +
               " p50=" + FormatDouble(r.msgQueue.p50, 0) +
               " p95=" + FormatDouble(r.msgQueue.p95, 0) +
               " p99=" + FormatDouble(r.msgQueue.p99, 0) +
               " avg=" + FormatDouble(r.msgQueue.avg, 0) + "\n";
        return out;
    }

private:
    static std::string FormatDouble(double v, int decimals) {
        char buf[64];
        if (decimals == 0)
            snprintf(buf, sizeof(buf), "%lld", (long long)v);
        else {
            snprintf(buf, sizeof(buf), "%.*f", decimals, v);
        }
        return std::string(buf);
    }

    BackpressureStats  bp_;
    IoIdleStats        ioIdle_;
    LuaBlockStats      luaBlock_;
    MsgQueueStats      msgQueue_;
};

} // namespace monitor
} // namespace snc
