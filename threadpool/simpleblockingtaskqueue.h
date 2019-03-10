#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>


// The simple blocking queue, based on deque, protected by the lock + condition variable.
template<typename Task>
class SimpleBlockingTaskQueue {
public:
    bool push(Task&& task)
    {
        {
            std::lock_guard<std::mutex> l(lock);
            tasks.push_back(std::move(task));
        }
        consumerWakeup.notify_one();
        return true;
    }

    void pop(Task& task)
    {
        std::unique_lock<std::mutex> l(lock);
        consumerWakeup.wait(l, [this] { return !tasks.empty(); });
        task = std::move(tasks.front());
        tasks.pop_front();
    }

private:
    std::deque<Task> tasks;

    std::mutex lock;
    std::condition_variable consumerWakeup;
};
