#pragma once

#include <future>


// Helper function, submits f(args) task to the pool (which must have pool::submit() method),
// returns corresponding future.
template<typename Pool, typename F, typename ...Args>
auto submitFuture(Pool& pool, F&& f, Args&&... args) -> std::future<decltype(f(args...))>
{
    std::promise<decltype(f(args...))> promise;
    std::future<decltype(f(args...))> ret = promise.get_future();
    // Passing param pack to lambda is allowed only in C++20, until then use std::bind.
    auto call = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    pool.submit([promise = std::move(promise), call = std::move(call)] () mutable {
        promise.set_value(call());
    });
    return ret;
}
