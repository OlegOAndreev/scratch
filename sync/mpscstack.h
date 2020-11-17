#pragma once

#include <atomic>

// A very barebones MPSC stack with only two methods: push() for pushing elements on stack
// and consumeAll() for popping all elements at once and processing them from top of the stack.
// The key feature is the absence of the ABA problem.
class MPSCStack {
public:
    void push(int v)
    {
        Node* newTop = new Node();
        newTop->v = v;
        Node* curTop = top.load();
        auto t = curTop;
        while (true) {
            newTop->next = curTop;
            if (top.compare_exchange_strong(curTop, newTop)) {
                break;
            }
        }
    }

    template<typename C>
    void consumeAll(C&& consumer)
    {
        Node* wasTop = top.exchange(nullptr);
        for (Node* it = wasTop; it != nullptr; it = it->next) {
            consumer(it->v);
        }
    }

private:
    struct Node {
        Node* next;
        int v;
    };

    std::atomic<Node*> top{nullptr};
};
