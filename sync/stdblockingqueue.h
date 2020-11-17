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

    bool dequeue(T& t);

    bool tryDequeue(T& t);

    void close();
    bool isClosed() const;

private:
    std::deque<T> deque;
    bool closed = false;

    mutable std::mutex lock;
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
bool StdBlockingQueue<T>::dequeue(T& t)
{
    std::unique_lock<std::mutex> l(lock);
    consumerWakeup.wait(l, [this] { return !deque.empty() || closed; });
    if (deque.empty()) {
        return false;
    }
    t = std::move(deque.front());
    deque.pop_front();
    return true;
}

template<typename T>
bool StdBlockingQueue<T>::tryDequeue(T& t)
{
    std::unique_lock<std::mutex> l(lock);
    if (deque.empty()) {
        return false;
    }
    t = std::move(deque.front());
    deque.pop_front();
    return true;
}

template<typename T>
void StdBlockingQueue<T>::close()
{
    std::unique_lock<std::mutex> l(lock);
    closed = true;
    consumerWakeup.notify_all();
}

template<typename T>
bool StdBlockingQueue<T>::isClosed() const
{
    std::unique_lock<std::mutex> l(lock);
    return closed;
}
