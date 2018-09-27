#pragma once

#include <functional>
#include <future>

namespace detail {

struct SimpleThreadPoolImpl;

}

// Simple thread pool with a single mutex-protected queue, uses C++11 async primitives (future/promise).
class SimpleThreadPool
{
public:
    SimpleThreadPool();
    SimpleThreadPool(int numThreads);
    ~SimpleThreadPool();

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

    // Returns number of worker threads in the pool.
    int numThreads() const;

private:
    detail::SimpleThreadPoolImpl* impl;

    // std::packaged_task is more suitable than std::function because the latter is copyable, while the packaged_task
    // is move-only.
    void submitImpl(std::packaged_task<void()>&& task);
};
