#include "simplethreadpool.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "common.h"


namespace detail {

struct SimpleTaskQueue {
    std::deque<std::packaged_task<void()>> tasks;
    bool stopFlag;

    std::mutex lock;
    std::condition_variable workerWakeup;
};

struct SimpleThreadPoolImpl {
    std::vector<std::thread> workerThreads;
    SimpleTaskQueue queue;

    ~SimpleThreadPoolImpl();
};

SimpleThreadPoolImpl::~SimpleThreadPoolImpl()
{
}

void simpleWorkerMain(SimpleTaskQueue* queue)
{
    std::packaged_task<void()> task;
    while (true) {
        {
            std::unique_lock<std::mutex> l(queue->lock);
            if (queue->stopFlag) {
                return;
            }
            queue->workerWakeup.wait(l, [queue] { return queue->stopFlag || !queue->tasks.empty(); });
            if (queue->stopFlag) {
                return;
            }
            task = std::move(queue->tasks.front());
            queue->tasks.pop_front();
        }
        task();
    }
}

}

SimpleThreadPool::SimpleThreadPool()
    : SimpleThreadPool(std::thread::hardware_concurrency())
{
}

SimpleThreadPool::SimpleThreadPool(int numThreads)
    : impl(new detail::SimpleThreadPoolImpl{})
{
    for (int i = 0; i < numThreads; i++) {
        impl->workerThreads.push_back(std::thread(detail::simpleWorkerMain, &impl->queue));
    }
}

SimpleThreadPool::~SimpleThreadPool()
{
    {
        std::unique_lock<std::mutex> l(impl->queue.lock);
        impl->queue.stopFlag = true;
        impl->queue.workerWakeup.notify_all();
    }

    for (std::thread& t : impl->workerThreads) {
        t.join();
    }

    delete impl;
}

int SimpleThreadPool::numThreads() const
{
    return impl->workerThreads.size();
}

void SimpleThreadPool::submitImpl(std::packaged_task<void()> &&task)
{
    std::unique_lock<std::mutex> l(impl->queue.lock);
    impl->queue.tasks.push_back(std::move(task));
    impl->queue.workerWakeup.notify_one();
}
