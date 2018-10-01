#pragma once

#include <functional>
#include <future>

namespace detail {

struct BetterThreadPoolImpl;

}

class BetterThreadPool {
public:
    // The default constructor determines the number of workers from the number of CPUs.
    BetterThreadPool();
    BetterThreadPool(int numThreads);
    ~BetterThreadPool();

    // Submits f(args) task to the pool, returns corresponding future.
    template<typename F, typename ...Args>
    auto submit(F&& f, Args&&... args) -> std::future<decltype(f(args...))>
    {
        std::promise<decltype(f(args...))> promise;
        std::future<decltype(f(args...))> ret = promise.get_future();
        // Passing param pack to lambda is allowed only in C++20, until then use std::bind.
        auto call = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        std::packaged_task<void()> task([promise = std::move(promise), call = std::move(call)] () mutable {
            promise.set_value(call());
        });
        submitImpl(std::move(task));
        return ret;
    }

    // Returns the number of worker threads in the pool.
    int numThreads() const;

private:
    detail::BetterThreadPoolImpl* impl;

    // std::packaged_task is more suitable than std::function because the latter is copyable, while the packaged_task
    // is move-only.
    void submitImpl(std::packaged_task<void()>&& task);
};
