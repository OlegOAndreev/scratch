#pragma once

#include "common.h"
#include "max-heap.h"

#include <algorithm>

namespace detail {

template<typename T>
T median3(T const& v1, T const& v2, T const& v3)
{
    if (v1 < v2) {
        if (v2 < v3) {
            return v2;
        } else {
            return (v1 < v3) ? v3 : v1;
        }
    } else {
        if (v2 < v3) {
            return (v1 < v3) ? v1 : v3;
        } else {
            return v2;
        }
    }
}

// "Small" sort: if the sizes are small, sort using the sorting networks and return true, otherwise return false.
template<typename It>
bool smallSort(It first, It last)
{
    if (last - first < 2) {
        return true;
    } else if (last - first == 2) {
        if (*first > *(first + 1)) {
            std::swap(*first, *(first + 1));
        }
        return true;
    } else if (last - first == 3) {
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
    for (It i = first; i < last - 1; i++) {
        It minI = i;
        auto minV = *i;
        for (It j = i + 1; j < last; j++) {
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
    for (It i = first + 1; i < last; i++) {
        auto v = std::move(*i);
        It j;
        for (j = i; j > first && v < *(j - 1); j--) {
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
        for (ptrdiff_t i = last - first; i > 1; i--) {
            std::pop_heap(first, first + i);
        }
    } else {
        makeHeap(first, last);
        for (ptrdiff_t i = last - first; i > 1; i--) {
            popHeap(first, first + i);
        }
    }

}

namespace detail {

template<typename It>
void quickSortImpl(It first, It last, size_t cutoff, size_t remainingDepth)
{
    while (true) {
        if (detail::smallSort(first, last)) {
            return;
        } else if ((size_t)(last - first) <= cutoff) {
            insertionSort(first, last);
            return;
        }

        if (remainingDepth == 0) {
            heapSort(first, last);
            return;
        }
        remainingDepth--;

        // Median of 3 selection: median of first, middle, last.
        auto pivot = detail::median3(*first, *(first + (last - first) / 2), *(last - 1));
        // Partition. [first, left) is less or equal to pivot, [right, last) is greater or equal to pivot.
        It left = first;
        It right = last - 1;
        while (left < right) {
            while (*left < pivot) {
                ++left;
            }
            while (pivot < *right) {
                --right;
            }
            if (left < right) {
                std::swap(*left, *right);
                ++left;
                --right;
            }
        }
        if (right - first > last - left) {
            quickSortImpl(left, last, cutoff, remainingDepth);
            last = right + 1;
        } else {
            quickSortImpl(first, right + 1, cutoff, remainingDepth);
            first = left;
        }
    }
}

template<typename It>
void quickSortThreeWayImpl(It first, It last, size_t cutoff, size_t remainingDepth)
{
    while (true) {
        if (detail::smallSort(first, last)) {
            return;
        } else if ((size_t)(last - first) <= cutoff) {
            insertionSort(first, last);
            return;
        }

        if (remainingDepth == 0) {
            heapSort(first, last);
            return;
        }
        remainingDepth--;

        // Median of 3 selection: median of first, middle, last.
        auto pivot = detail::median3(*first, *(first + (last - first) / 2), *(last - 1));
        // Partition. [first, left) is less than pivot, [left, leftPivot) is pivot, [right, last)
        // is greater than pivot.
        It left = first;
        It leftPivot = first;
        It right = last;
        while (leftPivot < right) {
            if (*leftPivot == pivot) {
                ++leftPivot;
            } else if (*leftPivot < pivot) {
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
        if (detail::smallSort(first, last)) {
            return;
        } else if ((size_t)(last - first) <= cutoff) {
            insertionSort(first, last);
            return;
        }

        if (remainingDepth == 0) {
            heapSort(first, last);
            return;
        }
        remainingDepth--;

        size_t size = last - first;
        auto pivot1 = *(first + size / 3);
        auto pivot2 = *(first + size * 2 / 3);
        if (pivot1 == pivot2) {
            // Find the new pivot2.
            for (It it = first; it != last; ++it) {
                if (*it != pivot1) {
                    pivot2 = *it;
                    break;
                }
            }
            // All elements are equal to pivot1, the array is constant (and, therefore, sorted), exit.
            if (pivot1 == pivot2) {
                return;
            }
        } else if (pivot1 > pivot2) {
            std::swap(pivot1, pivot2);
        }

        // Partition. Slightly modified dual-pivot quicksort. [first, left1) is less or equal to pivot1,
        // [left1, left2) is less or equal to pivot2, [right, last) is greater than pivot2.
        It left1 = first;
        It left2 = first;
        It right = last;

        while (left2 < right) {
            if (*left2 > pivot2) {
                --right;
                std::swap(*left2, *right);
            } else if (*left2 > pivot1) {
                ++left2;
            } else {
                std::swap(*left1, *left2);
                ++left1;
                ++left2;
            }
        }

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
// insertion sorts for small arrays (less than cutoff), uses sorting networks for 2, 3 and 4 elements.
// Unlike the std::sort assumes that copying the valuesv is cheap (when selecting the pivot).
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
    detail::quickSortDualPivotImpl(first, last, cutoff, nextLog2(last - first) * 4);
}
