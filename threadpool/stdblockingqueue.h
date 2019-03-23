#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>


// The simple std::-based blocking queue, based on deque, protected by the lock + condition
// variable.
template<typename T>
class StdBlockingQueue {
public:
    template<typename U>
    bool enqueue(U&& u);

    void dequeue(T& t);

private:
    std::deque<T> deque;

    std::mutex lock;
    std::condition_variable consumerWakeup;
};

template<typename T>
template<typename U>
bool StdBlockingQueue<T>::enqueue(U&& u)
{
    {
        std::lock_guard<std::mutex> l(lock);
        deque.push_back(std::forward<U>(u));
    }
    consumerWakeup.notify_one();
    return true;
}

template<typename T>
void StdBlockingQueue<T>::dequeue(T& t)
{
    std::unique_lock<std::mutex> l(lock);
    consumerWakeup.wait(l, [this] { return !deque.empty(); });
    t = std::move(deque.front());
    deque.pop_front();
}
