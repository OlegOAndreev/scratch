#pragma once

#include <atomic>
#include <utility>

// Lock-free multi-producer single-consumer queue based on Dmitry Vyukov MPSC queue:
// http://www.1024cores.net/home/lock-free-algorithms/queues/intrusive-mpsc-node-based-queue
template<typename T>
class MpScUnboundedQueue {
public:
    using ElementType = T;

    MpScUnboundedQueue();
    ~MpScUnboundedQueue();

    template<typename U>
    bool enqueue(U&& u);

    bool dequeue(T& t);

private:
    struct Node {
        T data;
        std::atomic<Node*> next;

        Node();
        template<typename U>
        Node(U&& u);
    };

    // head and tail are inverted in Vyukov terms: head is where the producers position, tail
    // is the consumer position.
    std::atomic<Node*> head;
    Node* tail;
    Node stub;

    void enqueueImpl(Node* newNode);
};

template<typename T>
MpScUnboundedQueue<T>::MpScUnboundedQueue()
{
    head.store(&stub, std::memory_order_relaxed);
    tail = &stub;
}

template<typename T>
MpScUnboundedQueue<T>::~MpScUnboundedQueue()
{
    Node* it = tail;
    while (it != nullptr) {
        Node* next = it->next.load(std::memory_order_seq_cst);
        if (it != &stub) {
            delete it;
        }
        it = next;
    }
}

template<typename T>
template<typename U>
bool MpScUnboundedQueue<T>::enqueue(U&& u)
{
    Node* newNode = new Node(std::forward<U>(u));
    enqueueImpl(newNode);
    return true;
}

template<typename T>
bool MpScUnboundedQueue<T>::dequeue(T& t)
{
    Node* curTail = tail;
    Node* next = curTail->next.load(std::memory_order_acquire);
    if (curTail == &stub) {
        if (next == nullptr) {
            return false;
        }
        tail = next;
        curTail = next;
        next = next->next;
    }
    if (next != nullptr) {
        tail = next;
        t = std::move(curTail->data);
        delete curTail;
        return true;
    }
    // Head modification must be seq_cst, see blockingqueue.h for details.
    if (curTail != head.load(std::memory_order_seq_cst)) {
        return false;
    }
    stub.next.store(nullptr, std::memory_order_relaxed);
    enqueueImpl(&stub);
    next = curTail->next.load(std::memory_order_acquire);
    if (next != nullptr) {
        tail = next;
        t = std::move(curTail->data);
        delete curTail;
        return true;
    }
    return false;
}

template<typename T>
void MpScUnboundedQueue<T>::enqueueImpl(Node* newNode)
{
    // Head modification must be seq_cst, see blockingqueue.h for details.
    Node* oldHead = head.exchange(newNode, std::memory_order_seq_cst);
    oldHead->next.store(newNode, std::memory_order_release);
}

template<typename T>
MpScUnboundedQueue<T>::Node::Node()
{
    next.store(nullptr, std::memory_order_relaxed);
}

template<typename T>
template<typename U>
MpScUnboundedQueue<T>::Node::Node(U&& u)
    : data(std::forward<U>(u))
{
    next.store(nullptr, std::memory_order_relaxed);
}
