#pragma once

#include <atomic>
#include <functional>
#include <future>
#include <thread>
#include <vector>

// A simple thread pool with a central queue, no task dependencies and no futures.
// BlockingQueue should have methods:
//   bool push(Task&& task)
//   bool pop(Task&),
//   void stop()
// Task should have a void operator()().
template<typename Task, template<typename> class BlockingQueue>
class SimpleThreadPool {
public:
    // The default constructor determines the number of workers from the number of CPUs.
    SimpleThreadPool();
    SimpleThreadPool(int numThreads);
    ~SimpleThreadPool();

    // Submits the task for execution in the pool (f must a be callable of type void()).
    template<typename F>
    void submit(F&& f);

    // Submits a number of ranged tasks for range [from, to) for execution in the pool,
    // i.e. splits the range [from, to) into subranges [from, r1), [r1, r2)
    // and calls f for each: f(from, r1), f(r1, r2), ... (f must be a callable of type void(size_t, size_t)).
    template<typename F>
    void submitRange(F&& f, size_t from, size_t to);

    // Returns the number of worker threads in the pool.
    int numThreads() const;

private:
    std::vector<std::thread> workerThreads;
    BlockingQueue<Task> queue;
    std::atomic<int> numStoppedThreads{0};

    void workerMain();
};

template<typename Task, template<typename> class Queue>
SimpleThreadPool<Task, Queue>::SimpleThreadPool()
    : SimpleThreadPool(std::thread::hardware_concurrency())
{
}

template<typename Task, template<typename> class Queue>
SimpleThreadPool<Task, Queue>::SimpleThreadPool(int numThreads)
{
    for (int i = 0; i < numThreads; i++) {
        workerThreads.push_back(std::thread([this] { workerMain(); }));
    }
}

template<typename Task, template<typename> class Queue>
SimpleThreadPool<Task, Queue>::~SimpleThreadPool()
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
int SimpleThreadPool<Task, Queue>::numThreads() const
{
    return workerThreads.size();
}

template<typename Task, template<typename> class Queue>
template<typename F>
void SimpleThreadPool<Task, Queue>::submit(F&& f)
{
    if (!queue.push(Task(std::forward<F>(f)))) {
        // Queue is full, run the task in the caller thread.
        f();
    }
}

template<typename Task, template<typename> class Queue>
template<typename F>
void SimpleThreadPool<Task, Queue>::submitRange(F&& f, size_t from, size_t to)
{
    const size_t kMinGranularity = 16;

    // Try to split all work so that there are least worker threads * 4 pieces for proper load balancing.
    size_t pushGranularity = std::max((to - from) / (workerThreads.size() * 4), kMinGranularity);
    size_t pushed = 0;
    for (pushed = from; pushed < to; pushed += pushGranularity) {
        size_t n = std::min(to - pushed, pushGranularity);
        if (!queue.push([f, pushed, n] { f(pushed, pushed + n); })) {
            break;
        }
    }
    if (pushed < to) {
        // Extremely unlikely: the queue is full, just run the task in the caller.
        f(pushed, to);
    }
}

template<typename Task, template<typename> class Queue>
void SimpleThreadPool<Task, Queue>::workerMain()
{
    Task task;
    while (queue.pop(task)) {
        task();
    }
    numStoppedThreads.fetch_add(1, std::memory_order_relaxed);
}
