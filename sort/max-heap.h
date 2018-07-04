#pragma once

#include <utility>

namespace detail {

template<typename It>
void siftUp(It first, size_t idx)
{
    auto newValue = std::move(*(first + idx));
    while (idx > 0) {
        It parentIdx = (idx - 1) / 2;
        if (newValue < *(first + parentIdx)) {
            break;
        }
        *(first + idx) = std::move(*(first + parentIdx));
        idx = parentIdx;
    }
    *(first + idx) = std::move(newValue);
}

template<typename It>
void siftDown(It first, size_t size, size_t idx)
{
    auto newValue = std::move(*(first + idx));
    // If idx >= halfSize, we are in a leaf.
    size_t halfSize = size / 2;
    while (idx < halfSize) {
        // Find which of the children is the smaller one.
        size_t childIdx = idx * 2 + 1;
        size_t childRIdx = childIdx + 1;
        // Check if there is a right child and it is larger than the left child.
        if (childRIdx < size && *(first + childIdx) < *(first + childRIdx)) {
            if (!(newValue < *(first + childRIdx))) {
                break;
            }
            *(first + idx) = std::move(*(first + childRIdx));
            idx = childRIdx;
        } else {
            if (!(newValue < *(first + childIdx))) {
                break;
            }
            *(first + idx) = std::move(*(first + childIdx));
            idx = childIdx;
        }
    }
    *(first + idx) = std::move(newValue);
}

} // namespace detail

// Makes the max-heap in the range [first, last)
template<typename It>
void makeHeap(It first, It last)
{
    if (last - first <= 1) {
        return;
    }
    size_t size = last - first;
    size_t halfSize = size / 2;
    // I hate unsigned arithmetic.
    for (size_t idx = halfSize + 1; idx > 0; idx--) {
        detail::siftDown(first, size, idx - 1);
    }
}

// Pushes the element from (last - 1) to the appropriate place in the max-heap [first, last - 1).
template<typename It>
void pushHeap(It first, It last)
{
    detail::siftUp(first, last - first - 1);
}

// Moves the element at first to last - 1 and replaces it with minimal from [first + 1, last)
// and makes [first, last - 1) max-heap again.
template<typename It>
void popHeap(It first, It last)
{
    if (last - first <= 1) {
        return;
    }
    std::swap(*first, *(last - 1));
    detail::siftDown(first, last - first - 1, 0);
}
