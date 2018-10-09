#pragma once

#include <atomic>
#include <future>
#include <thread>
#include <vector>

// Simple thread pool with a central queue and no task dependencies.
// Queue should have methods:
//   void push(Task&& task)
//   bool pop(Task&)
//   void stop()
// Task should have a void operator()().
template<typename Task, template<typename> class Queue>
class SimpleThreadPoolImpl {
public:
    // The default constructor determines the number of workers from the number of CPUs.
    SimpleThreadPoolImpl();
    SimpleThreadPoolImpl(int numThreads);
    ~SimpleThreadPoolImpl();

    // Submits the task for execution in the pool (f must a be callable of type void())..
    template<typename F>
    void submit(F&& f);

    // Returns the number of worker threads in the pool.
    int numThreads() const;

private:
    std::vector<std::thread> workerThreads;
    Queue<Task> queue;
    std::atomic<int> numStoppedThreads{0};

    void workerMain();
};

template<typename Task, template<typename> class Queue>
SimpleThreadPoolImpl<Task, Queue>::SimpleThreadPoolImpl()
    : SimpleThreadPoolImpl(std::thread::hardware_concurrency())
{
}

template<typename Task, template<typename> class Queue>
SimpleThreadPoolImpl<Task, Queue>::SimpleThreadPoolImpl(int numThreads)
{
    for (int i = 0; i < numThreads; i++) {
        workerThreads.push_back(std::thread([this] { workerMain(); }));
    }
}

template<typename Task, template<typename> class Queue>
SimpleThreadPoolImpl<Task, Queue>::~SimpleThreadPoolImpl()
{
    int waitUntilStopped = workerThreads.size();
    while (numStoppedThreads.load(std::memory_order_relaxed) != waitUntilStopped) {
        queue.stop();
    }

    for (std::thread& t : workerThreads) {
        t.join();
    }
}

template<typename Task, template<typename> class Queue>
int SimpleThreadPoolImpl<Task, Queue>::numThreads() const
{
    return workerThreads.size();
}

template<typename Task, template<typename> class Queue>
template<typename F>
void SimpleThreadPoolImpl<Task, Queue>::submit(F&& f)
{
    queue.push(Task(std::forward<F>(f)));
}

template<typename Task, template<typename> class Queue>
void SimpleThreadPoolImpl<Task, Queue>::workerMain()
{
    Task task;
    while (queue.pop(task)) {
        task();
    }
    numStoppedThreads.fetch_add(1, std::memory_order_relaxed);
}


// Helper function, submits f(args) task to the pool (which must have pool::submit() method),
// returns corresponding future.
template<typename Pool, typename F, typename ...Args>
auto submitFuture(Pool& pool, F&& f, Args&&... args) -> std::future<decltype(f(args...))>
{
    std::promise<decltype(f(args...))> promise;
    std::future<decltype(f(args...))> ret = promise.get_future();
    // Passing param pack to lambda is allowed only in C++20, until then use std::bind.
    auto call = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
    std::packaged_task<void()> task([promise = std::move(promise), call = std::move(call)] () mutable {
        promise.set_value(call());
    });
    pool.submit(std::move(task));
    return ret;
}
