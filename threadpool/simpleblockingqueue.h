#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>


// The simple blocking queue, based on deque, protected by the lock + condition variable.
template<typename T>
class SimpleBlockingQueue {
public:
    bool push(T&& t)
    {
        {
            std::lock_guard<std::mutex> l(lock);
            deque.push_back(std::move(t));
        }
        consumerWakeup.notify_one();
        return true;
    }

    void pop(T& t)
    {
        std::unique_lock<std::mutex> l(lock);
        consumerWakeup.wait(l, [this] { return !deque.empty(); });
        t = std::move(deque.front());
        deque.pop_front();
    }

private:
    std::deque<T> deque;

    std::mutex lock;
    std::condition_variable consumerWakeup;
};
