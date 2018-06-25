#pragma once

#include "common.h"
#include "max-heap.h"

#include <algorithm>

namespace detail {

template<typename It>
FORCE_INLINE size_t median3(It first, size_t i1, size_t i2, size_t i3)
{
    if (*(first + i1) < *(first + i2)) {
        if (*(first + i2) < *(first + i3)) {
            return i2;
        } else {
            return (*(first + i1) < *(first + i3)) ? i3 : i1;
        }
    } else {
        if (*(first + i2) < *(first + i3)) {
            return (*(first + i1) < *(first + i3)) ? i1 : i3;
        } else {
            return i2;
        }
    }
}

// "Small" sort: if the sizes are small, sort and return true, otherwise return false.
template<typename It>
FORCE_INLINE bool smallSort(It first, It last)
{
    if (last - first < 2) {
        return true;
    } else if (last - first == 2) {
        if (*first > *(first + 1)) {
            std::swap(*first, *(first + 1));
        }
        return true;
    } else if (last - first == 3) {
#if 1
        // Sorting network for 3 elements.
        if (*first > *(first + 1)) {
            std::swap(*first, *(first + 1));
        }
        if (*first > *(first + 2)) {
            std::swap(*first, *(first + 2));
        }
        if (*(first + 1) > *(first + 2)) {
            std::swap(*(first + 1), *(first + 2));
        }
#else
        // This code should be slightly more performant, than sorting networks (sometimes it does two comparisons
        // instead of 3), but it does not show up in the profile.
        if (*first > *(first + 1)) {
            if (*(first + 1) > *(first + 2)) {
                // first[2] < first[1] < first[0]
                std::swap(*first, *(first + 2));
            } else {
                if (*first > *(first + 2)) {
                    // first[1] < first[2] < first[0]
                    auto tmp = std::move(*first);
                    *first = std::move(*(first + 1));
                    *(first + 1) = std::move(*(first + 2));
                    *(first + 2) = std::move(tmp);
                } else {
                    // first[1] < first[0] < first[2]
                    std::swap(*first, *(first + 1));
                }
            }
        } else {
            if (*(first + 1) > *(first + 2)) {
                if (*first > *(first + 2)) {
                    // first[2] < first[0] < first[1]
                    auto tmp = std::move(*first);
                    *first = std::move(*(first + 2));
                    *(first + 2) = std::move(*(first + 1));
                    *(first + 1) = std::move(tmp);
                } else {
                    // first[0] < first[2] < first[1]
                    std::swap(*(first + 1), *(first + 2));
                }
            } else {
                // first[0] < first[1] < first[2], do nothing
            }
        }
#endif
        return true;
    } else if (last - first == 4) {
        // Sorting network for 4 elements.
        if (*first > *(first + 1)) {
            std::swap(*first, *(first + 1));
        }
        if (*(first + 2) > *(first + 3)) {
            std::swap(*(first + 2), *(first + 3));
        }
        if (*first > *(first + 2)) {
            std::swap(*first, *(first + 2));
        }
        if (*(first + 1) > *(first + 3)) {
            std::swap(*(first + 1), *(first + 3));
        }
        if (*(first + 1) > *(first + 2)) {
            std::swap(*(first + 1), *(first + 2));
        }
        return true;
    } else {
        return false;
    }
}

} // namespace detail

template<typename It>
void selectionSort(It first, It last)
{
    if (first == last) {
        return;
    }
    for (It i = first; i < last - 1; ++i) {
        It minI = i;
        auto minV = *i;
        for (It j = i + 1; j < last; ++j) {
            if (minV > *j) {
                minI = j;
                minV = *j;
            }
        }
        if (i != minI) {
            std::swap(*i, *minI);
        }
    }
}

template<typename It>
void insertionSort(It first, It last)
{
    if (first == last) {
        return;
    }
    for (It i = first + 1; i < last; ++i) {
        if (*i > *(i - 1)) {
            continue;
        }
        auto v = std::move(*i);
        It j;
        for (j = i; j > first && v < *(j - 1); --j) {
            *j = std::move(*(j - 1));
        }
        *j = std::move(v);
    }
}

// A heap-sort implementation. If useStdHeap is true, uses std::make_heap/std::push_heap/std::pop_heap to make the heap,
// otherwise uses makeHeap/pushHeap/popHeap.
template<typename It>
void heapSort(It first, It last, bool useStdHeap = false)
{
    if (useStdHeap) {
        std::make_heap(first, last);
        for (size_t i = last - first; i > 1; i--) {
            std::pop_heap(first, first + i);
        }
    } else {
        makeHeap(first, last);
        for (size_t i = last - first; i > 1; i--) {
            popHeap(first, first + i);
        }
    }

}

namespace detail {

template<typename It>
void quickSortImpl(It first, It last, size_t cutoff, size_t remainingDepth)
{
    while (true) {
        if ((size_t)(last - first) <= cutoff) {
            if (!detail::smallSort(first, last)) {
                insertionSort(first, last);
            }
            return;
        }

        if (remainingDepth == 0) {
            heapSort(first, last);
            return;
        }
        remainingDepth--;

        // Median of 3 selection: median of first, middle, last.
        size_t pivotIdx = detail::median3(first, 0, (last - first) / 2, last - first - 1);
#if 0
        auto pivot = *(first + pivotIdx);
        // Partition. [first, left) is less or equal to pivot, [right, last) is greater or equal to pivot.
        It left = first;
        It right = last;
        while (left < right) {
            while (*left < pivot) {
                ++left;
            }
            while (pivot < *(right - 1)) {
                --right;
            }
            if (left < right) {
                std::swap(*left, *(right - 1));
                ++left;
                --right;
            }
        }
        if (left - first > last - left) {
            quickSortImpl(left, last, cutoff, remainingDepth);
            last = left;
        } else {
            quickSortImpl(first, left, cutoff, remainingDepth);
            first = left;
        }
#else
        // A slightly different quickSort implementation: moves the pivot to the first element. Performs the same as the previous
        // version on random arrays, but much worse on pre-sorted arrays.

        // Move pivot to the first element.
        std::swap(*first, *(first + pivotIdx));

        // Partition: [first + 1, left) is less or equal to pivot, [right, last) is greater or equal to pivot, after the
        // last iteration [left, last) is greater or equal to pivot (right can be equal to left - 1).
        It left = first + 1;
        It right = last;
        while (left < right) {
            while (*left < *first) {
                ++left;
            }
            while (*first < *(right - 1)) {
                --right;
            }
            if (left < right) {
                std::swap(*left, *(right - 1));
                ++left;
                --right;
            }
        }

        // Move the pivot back to the center. Now this element is in the right place.
        std::swap(*first, *(left - 1));

        if (left - first - 1 > last - left) {
            quickSortImpl(left, last, cutoff, remainingDepth);
            last = left - 1;
        } else {
            quickSortImpl(first, left - 1, cutoff, remainingDepth);
            first = left;
        }
#endif
    }
}

template<typename It>
void quickSortThreeWayImpl(It first, It last, size_t cutoff, size_t remainingDepth)
{
    while (true) {
        if ((size_t)(last - first) <= cutoff) {
            if (!detail::smallSort(first, last)) {
                insertionSort(first, last);
            }
            return;
        }

        if (remainingDepth == 0) {
            heapSort(first, last);
            return;
        }
        remainingDepth--;

        // Median of 3 selection: median of first, middle, last.
        size_t pivotIdx = detail::median3(first, 0, (last - first) / 2, last - first - 1);
        // Move pivot to the start of the array.
        std::swap(*(first + pivotIdx), *first);

        // Partition. [first, left) is less than pivot, [left, leftPivot) is pivot, [right, last)
        // is greater than pivot.
        It left = first + 1;
        It leftPivot = first + 1;
        It right = last;
        while (leftPivot < right) {
            if (*leftPivot == *first) {
                ++leftPivot;
            } else if (*leftPivot < *first) {
                if (leftPivot != left) {
                    std::swap(*leftPivot, *left);
                }
                ++left;
                ++leftPivot;
            } else {
                --right;
                std::swap(*leftPivot, *right);
            }
        }

        // Return pivot back to the place.
        left--;
        std::swap(*first, *left);

        if (left - first > last - right) {
            quickSortThreeWayImpl(right, last, cutoff, remainingDepth);
            last = left;
        } else {
            quickSortThreeWayImpl(first, left, cutoff, remainingDepth);
            first = right;
        }
    }
}

template<typename It>
void quickSortDualPivotImpl(It first, It last, size_t cutoff, size_t remainingDepth)
{
    while (true) {
        if ((size_t)(last - first) <= cutoff) {
            if (!detail::smallSort(first, last)) {
                insertionSort(first, last);
            }
            return;
        }

        if (remainingDepth == 0) {
            heapSort(first, last);
            return;
        }
        remainingDepth--;

        // Very simple pivot selection: 1/3th element and 2/3th element.
        size_t pivot1Idx = (last - first) / 3;
        size_t pivot2Idx = pivot1Idx * 2;
        if (*(first + pivot1Idx) > *(first + pivot2Idx)) {
            std::swap(pivot1Idx, pivot2Idx);
        }
        // Move pivot1 to the start of the array, pivot2 to the end of the array.
        std::swap(*(first + pivot1Idx), *first);
        std::swap(*(first + pivot2Idx), *(last - 1));

        // Partition: [first + 1, left1) is less than pivot1, [left1, left2) is greater or equal to pivot1
        // and less or equal to pivot2, [right, last - 1) is greater than pivot2.
        It left1 = first + 1;
        It left2 = first + 1;
        It right = last - 1;

        while (left2 < right) {
            if (*left2 > *(last - 1)) {
                --right;
                std::swap(*left2, *right);
            } else if (*left2 > *first) {
                ++left2;
            } else {
                std::swap(*left1, *left2);
                ++left1;
                ++left2;
            }
        }

        // Return pivots back to their place: left1 - 1 and left2 respectively.
        left1--;
        std::swap(*left1, *first);
        std::swap(*right, *(last - 1));
        right++;

        size_t l1 = left1 - first;
        size_t l2 = left2 - left1;
        size_t l3 = last - left2;
        if (l1 < l2) {
            if (l2 < l3) {
                // l1 < l2 < l3
                quickSortDualPivotImpl(first, left1, cutoff, remainingDepth);
                quickSortDualPivotImpl(left1, left2, cutoff, remainingDepth);
                first = left2;
            } else {
                // l1 < l3 < l2 or l3 < l1 < l2
                quickSortDualPivotImpl(first, left1, cutoff, remainingDepth);
                quickSortDualPivotImpl(left2, last, cutoff, remainingDepth);
                first = left1;
                last = left2;
            }
        } else {
            if (l1 < l3) {
                // l2 < l1 < l3
                quickSortDualPivotImpl(left1, left2, cutoff, remainingDepth);
                quickSortDualPivotImpl(first, left1, cutoff, remainingDepth);
                first = left2;
            } else {
                // l2 < l3 < l1 or l3 < l2 < l1
                quickSortDualPivotImpl(left1, left2, cutoff, remainingDepth);
                quickSortDualPivotImpl(left2, last, cutoff, remainingDepth);
                last = left1;
            }
        }
    }
}

} // namespace detail

// A quicksort implementation, just for comparison with std::sort. Runs heapSort if recursed more than log(last - first),
// insertion sorts for small arrays (less than cutoff), uses specialized small sort for 2, 3 and 4 elements.
template<typename It>
void quickSort(It first, It last, size_t cutoff = 15)
{
    detail::quickSortImpl(first, last, cutoff, nextLog2(last - first) * 4);
}

// A variation of quickSort which does three-way partition: partition array into (less than pivot), (pivot)
// and (greater than pivot) arrays.
template<typename It>
void quickSortThreeWay(It first, It last, size_t cutoff = 15)
{
    detail::quickSortThreeWayImpl(first, last, cutoff, nextLog2(last - first) * 4);
}


// An implementation of dual-pivot quick sort: partition array into (less than pivot1), (pivot) and (greater than pivot) arrays.
template<typename It>
void quickSortDualPivot(It first, It last, size_t cutoff = 15)
{
    detail::quickSortDualPivotImpl(first, last, cutoff, nextLog2(last - first) * 2);
}
