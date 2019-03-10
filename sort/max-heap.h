#pragma once

#include <utility>

namespace detail {

template<typename It>
void siftUp(It first, size_t idx)
{
    // Fast exit for already heap case.
    size_t parentIdx = (idx - 1) / 2;
    if (*(first + idx) < *(first + parentIdx)) {
        return;
    }
    auto newValue = std::move(*(first + idx));
    do {
        *(first + idx) = std::move(*(first + parentIdx));
        idx = parentIdx;
        parentIdx = (idx - 1) / 2;
    } while (idx > 0 && *(first + parentIdx) < newValue);
    *(first + idx) = std::move(newValue);
}

template<typename It>
void siftDown(It first, size_t size, size_t idx)
{
#if 1
    // No fast-exit here, it is a un-optimization both on gcc and clang for int, string
    // and stringview in my experience.
    auto newValue = std::move(*(first + idx));
    // if idx < halfSize, there are both children available, the case of parent with only one child
    // is processed separately after the main loop.
    size_t halfSize = (size - 1) / 2;
    while (idx < halfSize) {
        // Index of the first child.
        size_t childIdx = idx * 2 + 1;
        // Making childIt a separate local var improves optimization in clang.
        It childIt = first + childIdx;
        if (*childIt < *(childIt + 1)) {
            ++childIdx;
            ++childIt;
        }
        if (!(newValue < *childIt)) {
            *(first + idx) = std::move(newValue);
            return;
        }
        *(first + idx) = std::move(*childIt);
        idx = childIdx;
    }

    // Check if this is the case where there is one element with only one child. This can happen
    // only if the size is even and we reached here because the index was larger or equal
    // to halfSize.
    if (idx == halfSize && size % 2 == 0) {
        size_t childIdx = idx * 2 + 1;
        It childIt = first + childIdx;
        if (newValue < *childIt) {
            *(first + idx) = std::move(*childIt);
            *(first + childIdx) = std::move(newValue);
        } else {
            *(first + idx) = std::move(newValue);
        }
    } else {
        *(first + idx) = std::move(newValue);
    }
#else
    // No fast-exit here, it is a un-optimization both on gcc and clang for int, string
    // and stringview in my experience.
    auto newValue = std::move(*(first + idx));
    // If idx >= halfSize, we are in a leaf.
    size_t halfSize = size / 2;
    while (idx < halfSize) {
        // Index of the first child which is guaranteed to exist.
        size_t childIdx = idx * 2 + 1;
        size_t childRIdx = idx * 2 + 2;
        // Check if there is a right child and it is larger than the left child.
        if (childRIdx < size && *(first + childIdx) < *(first + childRIdx)) {
            childIdx = childRIdx;
        }
        if (!(newValue < *(first + childIdx))) {
            break;
        }
        *(first + idx) = std::move(*(first + childIdx));
        idx = childIdx;
    }
    *(first + idx) = std::move(newValue);
#endif
}

// An optimization from libstdc++: start with moving the value into the leaf and the do a siftUp.
// This is an optimization because the new value has a very large probability of being a leaf value
// (or near-leaf), so we skip a lot of useless value compares. Ideally, alt version should be used
// for int/float-type values.
template<typename It>
void siftDownAlt(It first, size_t size, size_t idx)
{
    size_t startIdx = idx;
    // No fast-exit here, it is a un-optimization both on gcc and clang for int, string
    // and stringview in my experience.
    auto newValue = std::move(*(first + idx));
    // if idx < halfSize, there are both children available, the case of parent with only one child
    // is processed separately after the main loop.
    size_t halfSize = (size - 1) / 2;
    while (idx < halfSize) {
        // Index of the first child.
        size_t childIdx = idx * 2 + 1;
        // Making childIt a separate local var improves optimization in clang.
        It childIt = first + childIdx;
        if (*childIt < *(childIt + 1)) {
            ++childIdx;
            ++childIt;
        }
        *(first + idx) = std::move(*childIt);
        idx = childIdx;
    }

    // Check if this is the case where there is one element with only one child. This can happen
    // only if the size is even and we reached here because the index was larger or equal
    // to halfSize.
    if (idx == halfSize && size % 2 == 0) {
        size_t childIdx = idx * 2 + 1;
        *(first + idx) = std::move(*(first + childIdx));
        idx = childIdx;
    }

    // We now overshot the target index and the idx is the leaf index. Return newValue
    // to where it belongs.
    size_t parentIdx = (idx - 1) / 2;
    while (idx > startIdx && *(first + parentIdx) < newValue) {
        *(first + idx) = std::move(*(first + parentIdx));
        idx = parentIdx;
        parentIdx = (idx - 1) / 2;
    }
    *(first + idx) = std::move(newValue);
}

} // namespace detail

// Makes the max-heap in the range [first, last).
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

// A verson of makeHeap, using alternative implementation of siftDown.
template<typename It>
void makeHeapAlt(It first, It last)
{
    if (last - first <= 1) {
        return;
    }
    size_t size = last - first;
    size_t halfSize = size / 2;
    // I hate unsigned arithmetic.
    for (size_t idx = halfSize + 1; idx > 0; idx--) {
        detail::siftDownAlt(first, size, idx - 1);
    }
}

// Pushes the element from (last - 1) to the appropriate place in the max-heap [first, last - 1).
template<typename It>
void pushHeap(It first, It last)
{
    detail::siftUp(first, last - first - 1);
}

// Moves the element at first to last - 1 and replaces it with maximum from [first + 1, last)
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

// A version of popHeap, using alternative implementation of siftDown.
template<typename It>
void popHeapAlt(It first, It last)
{
    if (last - first <= 1) {
        return;
    }
    std::swap(*first, *(last - 1));
    detail::siftDownAlt(first, last - first - 1, 0);
}
