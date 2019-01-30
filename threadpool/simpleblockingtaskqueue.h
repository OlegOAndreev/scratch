#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>


// The simple blocking task queue, protected by the lock + condvar.
template<typename Task>
class SimpleBlockingTaskQueue {
public:
    bool push(Task&& task)
    {
        std::unique_lock<std::mutex> l(lock);
        tasks.push_back(std::move(task));
        workerWakeup.notify_one();
        return true;
    }

    bool pop(Task& task)
    {
        std::unique_lock<std::mutex> l(lock);
        if (stopFlag) {
            return false;
        }
        workerWakeup.wait(l, [this] { return stopFlag || !tasks.empty(); });
        if (stopFlag) {
            return false;
        }
        task = std::move(tasks.front());
        tasks.pop_front();
        return true;
    }

    void stop()
    {
        std::unique_lock<std::mutex> l(lock);
        stopFlag = true;
        workerWakeup.notify_all();
    }

private:
    std::deque<Task> tasks;
    bool stopFlag = false;

    std::mutex lock;
    std::condition_variable workerWakeup;
};
