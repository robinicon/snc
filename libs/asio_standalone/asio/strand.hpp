// asio/strand.hpp — Minimal strand (serial execution guarantee)
#pragma once
// io_context must be included before this header

namespace asio {

// Primary template — strand takes an executor_type
template<typename Executor = io_context::executor>
class strand {
public:
    using executor_type = Executor;

    class executor_wrapper {
    public:
        executor_wrapper() noexcept = default;
        explicit executor_wrapper(strand* s) noexcept : str_(s) {}
        executor_wrapper(const executor_wrapper&) noexcept = default;
        executor_wrapper& operator=(const executor_wrapper&) noexcept = default;
        bool operator==(const executor_wrapper& o) const noexcept { return str_ == o.str_; }
        bool operator!=(const executor_wrapper& o) const noexcept { return str_ != o.str_; }
        strand* get_strand() const noexcept { return str_; }
    private:
        friend class strand;
        strand* str_ = nullptr;
    };

    explicit strand(io_context& ctx) : ctx_(ctx) {}
    explicit strand(const executor_type& ex) : ctx_(*ex.context()) {}
    ~strand() = default;
    strand(const strand&) = delete;
    strand& operator=(const strand&) = delete;

    executor_type get_executor() const noexcept { return executor_type(ctx_.get_executor()); }
    io_context&   context() noexcept { return ctx_; }
    const io_context& context() const noexcept { return ctx_; }

    template<typename Fn>
    void post(Fn fn) {
        schedule(std::move(fn));
    }

    template<typename Fn>
    auto wrap(Fn fn) {
        return [this, fn = std::move(fn)](auto&&... args) mutable {
            using decay_fn = std::decay_t<Fn>;
            post([fn = std::move(fn),
                  tuple = std::make_tuple(std::forward<decltype(args)>(args)...)]() mutable {
                std::apply(fn, std::move(tuple));
            });
        };
    }

    template<typename Fn>
    void dispatch(Fn fn) {
        if (ctx_.is_running())
            schedule(std::move(fn));
        else
            ctx_.impl_post_unlocked_(std::move(fn));
    }

    template<typename Fn>
    void execute(Fn fn) {
        if (ctx_.is_running())
            schedule(std::move(fn));
        else
            std::move(fn)();
    }

private:
    template<typename Fn>
    void schedule(Fn fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        ++pending_;
        ctx_.post([this, fn = std::move(fn)]() mutable {
            fn();
            std::lock_guard<std::mutex> lock(mutex_);
            --pending_;
            if (pending_ == 0 && !queue_.empty())
                ctx_.wakeup_from_strand_();
        });
    }

    std::mutex                              mutex_;
    std::atomic<int>                        pending_{0};
    std::queue<std::function<void()>>       queue_;
    io_context&                             ctx_;
};

} // namespace asio
