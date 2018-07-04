#pragma once

#include "common.h"
#include "max-heap.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

#include "pdqsort/pdqsort.h"
#include "cpp-timsort/timsort.hpp"

// A number of sorting routines.

namespace detail {

// Minimal size of the cutoff for quicksort.
size_t const kMinQuickSortCutoff = 2;

// Cutoff for using insertion sort instead of quicksort for general element type.
size_t const kDefaultCutoff = 15;

// Cutoff for using insertion sort instead of quicksort for arithmetic element type (e.g. int or float).
size_t const kArithmeticTypeCutoff = 30;

// The size of the array when the switch from median-of-3 to median-of-5 happens.
size_t const kQuickSortAltSwitchToMedian5 = 100;

template<typename T>
size_t defaultCutoff()
{
    return kDefaultCutoff;
}
template<>
size_t defaultCutoff<char>()
{
    return kArithmeticTypeCutoff;
}
template<>
size_t defaultCutoff<signed char>()
{
    return kArithmeticTypeCutoff;
}
template<>
size_t defaultCutoff<unsigned char>()
{
    return kArithmeticTypeCutoff;
}
template<>
size_t defaultCutoff<signed short>()
{
    return kArithmeticTypeCutoff;
}
template<>
size_t defaultCutoff<unsigned short>()
{
    return kArithmeticTypeCutoff;
}
template<>
size_t defaultCutoff<signed int>()
{
    return kArithmeticTypeCutoff;
}
template<>
size_t defaultCutoff<unsigned int>()
{
    return kArithmeticTypeCutoff;
}
template<>
size_t defaultCutoff<signed long>()
{
    return kArithmeticTypeCutoff;
}
template<>
size_t defaultCutoff<unsigned long>()
{
    return kArithmeticTypeCutoff;
}
template<>
size_t defaultCutoff<signed long long>()
{
    return kArithmeticTypeCutoff;
}
template<>
size_t defaultCutoff<unsigned long long>()
{
    return kArithmeticTypeCutoff;
}
template<>
size_t defaultCutoff<float>()
{
    return kArithmeticTypeCutoff;
}
template<>
size_t defaultCutoff<double>()
{
    return kArithmeticTypeCutoff;
}

template<typename It>
FORCE_INLINE size_t median3(It first, size_t i0, size_t i1, size_t i2)
{
    if (*(first + i0) < *(first + i1)) {
        if (*(first + i1) < *(first + i2)) {
            return i1;
        } else if (*(first + i0) < *(first + i2)) {
            return i2;
        } else {
            return i0;
        }
    } else {
        if (*(first + i2) < *(first + i1)) {
            return i1;
        } else if (*(first + i2) < *(first + i0)) {
            return i2;
        } else {
            return i0;
        }
    }
}

template<typename It>
FORCE_INLINE size_t median5(It first, size_t i0, size_t i1, size_t i2, size_t i3, size_t i4)
{
    // Sorting network for 5 elements.
    if (*(first + i1) < *(first + i0)) {
        std::swap(i0, i1);
    }
    if (*(first + i4) < *(first + i3)) {
        std::swap(i3, i4);
    }
    if (*(first + i4) < *(first + i2)) {
        std::swap(i2, i4);
    }
    if (*(first + i3) < *(first + i2)) {
        std::swap(i2, i3);
    }
    if (*(first + i3) < *(first + i0)) {
        std::swap(i0, i3);
    }
    if (*(first + i2) < *(first + i0)) {
        std::swap(i0, i2);
    }
    if (*(first + i4) < *(first + i1)) {
        std::swap(i1, i4);
    }
    if (*(first + i3) < *(first + i1)) {
        std::swap(i1, i3);
    }
    if (*(first + i2) < *(first + i1)) {
        std::swap(i1, i2);
    }
    return i2;
}

// "Small" sort: if the sizes are small, sort and return true, otherwise return false.
template<typename It>
FORCE_INLINE bool smallSort(It first, It last)
{
    switch (last - first) {
    case 0:
    case 1:
        return true;
    case 2:
        if (*(first + 1) < *first) {
            std::swap(*first, *(first + 1));
        }
        return true;
    case 3:
        // Sorting network for 3 elements.
        if (*(first + 1) < *first) {
            std::swap(*first, *(first + 1));
        }
        if (*(first + 2) < *first) {
            std::swap(*first, *(first + 2));
        }
        if (*(first + 2) < *(first + 1)) {
            std::swap(*(first + 1), *(first + 2));
        }
        return true;
    case 4:
        // Sorting network for 4 elements.
        if (*(first + 1) < *first) {
            std::swap(*first, *(first + 1));
        }
        if (*(first + 3) < *(first + 2)) {
            std::swap(*(first + 2), *(first + 3));
        }
        if (*(first + 2) < *first) {
            std::swap(*first, *(first + 2));
        }
        if (*(first + 3) < *(first + 1)) {
            std::swap(*(first + 1), *(first + 3));
        }
        if (*(first + 2) < *(first + 1)) {
            std::swap(*(first + 1), *(first + 2));
        }
        return true;
    default:
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
            if (*j < minV) {
                minI = j;
                minV = *j;
            }
        }
        if (i != minI) {
            std::swap(*i, *minI);
        }
    }
}

// An optimization to regular insertion sort: if isLeftmost is false, assume that there is a (first - 1) element which
// is not larger than all the elements in [first, last)
template<typename It>
void insertionSortImpl(It first, It last, bool isLeftmost)
{
    if (first == last) {
        return;
    }
    if (isLeftmost) {
        for (It i = first + 1; i != last; ++i) {
            if (*(i - 1) < *i) {
                continue;
            }
            auto v = std::move(*i);
            It j;
            for (j = i; j > first && v < *(j - 1); --j) {
                *j = std::move(*(j - 1));
            }
            *j = std::move(v);
        }
    } else {
        for (It i = first + 1; i != last; ++i) {
            if (*(i - 1) < *i) {
                continue;
            }
            auto v = std::move(*i);
            It j;
            // We do not need the (j > first) check here because the first - 1 is always the smallest element.
            for (j = i; v < *(j - 1); --j) {
                *j = std::move(*(j - 1));
            }
            *j = std::move(v);
        }
    }
}

template<typename It>
void insertionSort(It first, It last)
{
    if (first == last) {
        return;
    }
    insertionSortImpl(first, last, true);
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

// Basic by-the-books implementation of the quicksort with Hoare partitioning, median-of-3 pivot selection and
// copying the pivot value into tmp variable. The only optimization is the isLeftmost, which allows insertion sort
// to be slightly more optimal.
template<typename It>
void quickSortImpl(It first, It last, size_t cutoff, size_t remainingDepth, bool isLeftmost = true)
{
    while (true) {
        if ((size_t)(last - first) <= cutoff) {
            if (!smallSort(first, last)) {
                insertionSortImpl(first, last, isLeftmost);
            }
            break;
        }

        if (remainingDepth == 0) {
            heapSort(first, last);
            break;
        }
        remainingDepth--;

        size_t size = last - first;
        size_t pivotIdx = median3(first, 0, size / 2, size - 1);
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
            if (left >= right) {
                break;
            }
            std::swap(*left, *(right - 1));
            ++left;
            --right;
        }

        if (left - first > last - left) {
            quickSortImpl(left, last, cutoff, remainingDepth, false);
            last = left;
        } else {
            quickSortImpl(first, left, cutoff, remainingDepth, isLeftmost);
            first = left;
            isLeftmost = false;
        }
    }
}


// An alternative to quickSortImpl: moves the pivot to the temp variable, replacing it with the first element. Also the partition
// scheme is slightly altered (one less comparison).
template<typename It>
void quickSortAltImpl(It first, It last, size_t cutoff, size_t remainingDepth, bool isLeftmost = true)
{
    while (true) {
        if ((size_t)(last - first) <= cutoff) {
            if (!smallSort(first, last)) {
                insertionSortImpl(first, last, isLeftmost);
            }
            return;
        }

        if (remainingDepth == 0) {
            heapSort(first, last);
            return;
        }
        remainingDepth--;

        // Median either of 3 or 5 elements (median of 3 elements can be insufficient for cases like asc-desc array).
        size_t size = last - first;
        size_t pivotIdx = (size < kQuickSortAltSwitchToMedian5)
                ? median3(first, 0, size / 2, size - 1)
                : median5(first, 0, size / 4, size / 2, size / 2 + size / 4, size - 1);
        // Swap pivot with the first element, sort the range [first + 1, last).
        auto pivot = std::move(*(first + pivotIdx));
        if (pivotIdx != 0) {
            *(first + pivotIdx) = std::move(*first);
        }

        // Partition: [first + 1, left) is less or equal to pivot, [right, last) is greater or equal to pivot.
        It left = first + 1;
        It right = last;
        while (true) {
            // NOTE: Unlike the quickSortImpl, this version requires that pivot is median of 3 or more elements
            // for the following two while loops to not go outside of bounds:
            //  1) it guarantees that there is at least one element less or equal to pivot and at least one or two elements
            //     greater or equal to pivot, so both loops will terminate in the first iteration of outer while loop;
            //  2) after that either left >= right and the loop terminates, or left < right and then the swapped elements
            //     will serve as sentinels.
            while (pivot < *(right - 1)) {
                --right;
            }
            while (*left < pivot) {
                ++left;
            }
            // NOTE: We do slightly different comparison from normal quicksort and do the last iteration after the loop
            // if needed.
            if (left >= right - 2) {
                break;
            }
            std::swap(*left, *(right - 1));
            ++left;
            --right;
        }
        if (left == right - 2) {
            std::swap(*left, *(right - 1));
            ++left;
            --right;
        }

        // After the last iteration [left, last) is greater or equal to pivot (right can be equal to left - 1).
        // Move the pivot back to the center. Now this element is in the right place, exclude it from sorting.
        *first = std::move(*(left - 1));
        *(left - 1) = std::move(pivot);

        if (left - first - 1 > last - left) {
            quickSortAltImpl(left, last, cutoff, remainingDepth, false);
            last = left - 1;
        } else {
            quickSortAltImpl(first, left - 1, cutoff, remainingDepth, isLeftmost);
            first = left;
            isLeftmost = false;
        }
    }
}

template<typename It>
void quickSortThreeWayImpl(It first, It last, size_t cutoff, size_t remainingDepth, bool isLeftmost = true)
{
    while (true) {
        if ((size_t)(last - first) <= cutoff) {
            if (!smallSort(first, last)) {
                insertionSortImpl(first, last, isLeftmost);
            }
            return;
        }

        if (remainingDepth == 0) {
            heapSort(first, last);
            return;
        }
        remainingDepth--;

        // Median of 3 selection: median of first, middle, last.
        size_t pivotIdx = median3(first, 0, (last - first) / 2, last - first - 1);
        // Swap pivot with first, sort [first + 1, last).
        auto pivot = std::move(*(first + pivotIdx));
        if (pivotIdx != 0) {
            *(first + pivotIdx) = std::move(*first);
        }

        // Partition. [first, left) is less than pivot, [left, leftPivot) is pivot, [right, last)
        // is greater than pivot.
        It left = first + 1;
        It leftPivot = first + 1;
        It right = last;
        while (leftPivot < right) {
            if (*leftPivot == pivot) {
                ++leftPivot;
            } else if (*leftPivot < pivot) {
                std::swap(*leftPivot, *left);
                ++left;
                ++leftPivot;
            } else {
                --right;
                std::swap(*leftPivot, *right);
            }
        }

        // Return pivot back to the place.
        --left;
        *first = std::move(*left);
        *left = std::move(pivot);

        if (left - first > last - right) {
            quickSortThreeWayImpl(right, last, cutoff, remainingDepth, false);
            last = left;
        } else {
            quickSortThreeWayImpl(first, left, cutoff, remainingDepth, isLeftmost);
            first = right;
            isLeftmost = false;
        }
    }
}

// Selects two elements from: 0, 1/3th element, 2/3th and last element. Do a 4-element sorting network.
template<typename It>
void dualPivotSelection(It first, It last, size_t* pivot1Idx, size_t* pivot2Idx)
{
    size_t size = last - first;
    size_t i0 = 0;
    size_t i1 = size / 3;
    size_t i2 = i1 * 2;
    size_t i3 = size - 1;
    if (*(first + i1) < *(first + i0)) {
        std::swap(i0, i1);
    }
    if (*(first + i3) < *(first + i2)) {
        std::swap(i2, i3);
    }
    if (*(first + i2) < *(first + i0)) {
        std::swap(i0, i2);
    }
    if (*(first + i3) < *(first + i1)) {
        std::swap(i1, i3);
    }
    if (*(first + i2) < *(first + i1)) {
        std::swap(i1, i2);
    }
    *pivot1Idx = i1;
    *pivot2Idx = i2;
}

template<typename It>
void quickSortDualPivotImpl(It first, It last, size_t cutoff, size_t remainingDepth, bool isLeftmost = true)
{
    while (true) {
        if ((size_t)(last - first) <= cutoff) {
            if (!smallSort(first, last)) {
                insertionSortImpl(first, last, isLeftmost);
            }
            break;
        }

        if (remainingDepth == 0) {
            heapSort(first, last);
            break;
        }
        remainingDepth--;

        size_t pivot1Idx;
        size_t pivot2Idx;
        dualPivotSelection(first, last, &pivot1Idx, &pivot2Idx);
        auto pivot1 = std::move(*(first + pivot1Idx));
        // Swap pivot1 and pivot2 with first and last - 1, sort [first + 1, last - 1).
        if (pivot1Idx != 0) {
            *(first + pivot1Idx) = std::move(*first);
            if (pivot2Idx == 0) {
                pivot2Idx = pivot1Idx;
            }
        }
        auto pivot2 = std::move(*(first + pivot2Idx));
        if (pivot2Idx != (size_t)(last - first - 1)) {
            *(first + pivot2Idx) = std::move(*(last - 1));
        }

        // Partition: [first + 1, left1) is less than pivot1, [left1, left2) is greater or equal to pivot1
        // and less or equal to pivot2, [right, last - 1) is greater than pivot2.
        It left1 = first + 1;
        It left2 = left1;
        It right = last - 1;

        while (left2 < right) {
            if (pivot2 < *left2) {
                --right;
                std::swap(*left2, *right);
            } else if (!(*left2 < pivot1)) {
                ++left2;
            } else {
                std::swap(*left1, *left2);
                ++left1;
                ++left2;
            }
        }

        // Return pivots back to their place: left1 - 1 and left2 respectively.
        --left1;
        *first = std::move(*left1);
        *left1 = std::move(pivot1);
        *(last - 1) = std::move(*left2);
        *left2 = std::move(pivot2);
        ++left2;

        // Final partition is [first, left1), [left1 + 1, left2 - 1) and [left2, last).
        // NOTE: Interesting enough, including this tail-call optimization mis-optimizes the loop on clang -O2, but not -O3 or gcc,
        // clang version Apple LLVM version 8.1.0 (clang-802.0.42).
        size_t l1 = left1 - first;
        size_t l2 = left2 - left1 - 2;
        size_t l3 = last - left2;
        if (l1 < l2) {
            if (l2 < l3) {
                // l1 < l2 < l3
                quickSortDualPivotImpl(first, left1, cutoff, remainingDepth, isLeftmost);
                quickSortDualPivotImpl(left1 + 1, left2 - 1, cutoff, remainingDepth, false);
                first = left2;
                isLeftmost = false;
            } else {
                // l1 < l3 < l2 or l3 < l1 < l2
                quickSortDualPivotImpl(first, left1, cutoff, remainingDepth, isLeftmost);
                quickSortDualPivotImpl(left2, last, cutoff, remainingDepth, false);
                first = left1 + 1;
                last = left2 - 1;
                isLeftmost = false;
            }
        } else {
            if (l1 < l3) {
                // l2 < l1 < l3
                quickSortDualPivotImpl(first, left1, cutoff, remainingDepth, isLeftmost);
                quickSortDualPivotImpl(left1 + 1, left2 - 1, cutoff, remainingDepth, false);
                first = left2;
                isLeftmost = false;
            } else {
                // l2 < l3 < l1 or l3 < l2 < l1
                quickSortDualPivotImpl(left1 + 1, left2 - 1, cutoff, remainingDepth, false);
                quickSortDualPivotImpl(left2, last, cutoff, remainingDepth, false);
                last = left1;
            }
        }
    }
}

// A slightly different dual-pivot quicksort implementation: always makes two pivots different (pivot1 < pivot2, not
// pivot1 <= pivot2 like the default implementation).
template<typename It>
void quickSortDualPivotAltImpl(It first, It last, size_t cutoff, size_t remainingDepth, bool isLeftmost = true)
{
    while (true) {
        if ((size_t)(last - first) <= cutoff) {
            if (!smallSort(first, last)) {
                insertionSortImpl(first, last, isLeftmost);
            }
            break;
        }

        if (remainingDepth == 0) {
            heapSort(first, last);
            break;
        }
        remainingDepth--;

        size_t pivot1Idx;
        size_t pivot2Idx;
        dualPivotSelection(first, last, &pivot1Idx, &pivot2Idx);
        // We need the pivot1 to be strictly less than pivot2 in all cases (or exit early if the array is constant).
        if (*(first + pivot1Idx) == *(first + pivot2Idx)) {
            size_t i;
            size_t size = last - first;
            for (i = 0; i < size; i++) {
                if (!(*(first + i) == *(first + pivot1Idx))) {
                    pivot2Idx = i;
                    break;
                }
            }
            if (i == size) {
                // The array is constant.
                return;
            }
            // Correct ordering of pivot1 and pivot2.
            if (*(first + pivot2Idx) < *(first + pivot1Idx)) {
                std::swap(pivot1Idx, pivot2Idx);
            }
        }
        auto pivot1 = std::move(*(first + pivot1Idx));
        // Swap pivot1 and pivot2 with first and last - 1, sort [first + 1, last - 1).
        if (pivot1Idx != 0) {
            *(first + pivot1Idx) = std::move(*first);
            if (pivot2Idx == 0) {
                pivot2Idx = pivot1Idx;
            }
        }
        auto pivot2 = std::move(*(first + pivot2Idx));
        if (pivot2Idx != (size_t)(last - first - 1)) {
            *(first + pivot2Idx) = std::move(*(last - 1));
        }

        // Partition: [first + 1, left1) is less or equal than pivot1, [left1, left2) is greater than pivot1
        // and less or equal than pivot2, [right, last - 1) is greater than pivot2.
        It left1 = first + 1;
        // Do an initial positioning of left so that we do not do useless swaps of initial portion
        // of values in the first partition part.
        while (!(pivot1 < *left1) && left1 < last - 1) {
            left1++;
        }
        It left2 = left1;
        It right = last - 1;

        while (left2 < right) {
            if (pivot2 < *left2) {
                --right;
                std::swap(*left2, *right);
            } else if (pivot1 < *left2) {
                ++left2;
            } else {
                std::swap(*left1, *left2);
                ++left1;
                ++left2;
            }
        }

        // Return pivots back to their place: left1 - 1 and left2 respectively.
        --left1;
        *first = std::move(*left1);
        *left1 = std::move(pivot1);
        *(last - 1) = std::move(*left2);
        *left2 = std::move(pivot2);
        ++left2;

        // Final partition is [first, left1), [left1 + 1, left2 - 1) and [left2, last).
        // NOTE: Interesting enough, including this tail-call optimization mis-optimizes the loop on clang -O2, but not -O3 or gcc,
        // clang version Apple LLVM version 8.1.0 (clang-802.0.42).
        size_t l1 = left1 - first;
        size_t l2 = left2 - left1 - 2;
        size_t l3 = last - left2;
        if (l1 < l2) {
            if (l2 < l3) {
                // l1 < l2 < l3
                quickSortDualPivotAltImpl(first, left1, cutoff, remainingDepth, isLeftmost);
                quickSortDualPivotAltImpl(left1 + 1, left2 - 1, cutoff, remainingDepth, false);
                first = left2;
                isLeftmost = false;
            } else {
                // l1 < l3 < l2 or l3 < l1 < l2
                quickSortDualPivotAltImpl(first, left1, cutoff, remainingDepth, isLeftmost);
                quickSortDualPivotAltImpl(left2, last, cutoff, remainingDepth, false);
                first = left1 + 1;
                last = left2 - 1;
                isLeftmost = false;
            }
        } else {
            if (l1 < l3) {
                // l2 < l1 < l3
                quickSortDualPivotAltImpl(first, left1, cutoff, remainingDepth, isLeftmost);
                quickSortDualPivotAltImpl(left1 + 1, left2 - 1, cutoff, remainingDepth, false);
                first = left2;
                isLeftmost = false;
            } else {
                // l2 < l3 < l1 or l3 < l2 < l1
                quickSortDualPivotAltImpl(left1 + 1, left2 - 1, cutoff, remainingDepth, false);
                quickSortDualPivotAltImpl(left2, last, cutoff, remainingDepth, false);
                last = left1;
            }
        }
    }
}

} // namespace detail

// A quicksort implementation, just for comparison with std::sort. Runs heapSort if recursed more than log(last - first),
// insertion sorts for small arrays (less than cutoff), uses specialized small sort for 2, 3 and 4 elements.
template<typename It>
void quickSort(It first, It last, size_t cutoff = 0)
{
    if (cutoff == 0) {
        cutoff = detail::defaultCutoff<typename std::iterator_traits<It>::value_type>();
    } else if (cutoff < detail::kMinQuickSortCutoff) {
        cutoff = detail::kMinQuickSortCutoff;
    }
    detail::quickSortImpl(first, last, cutoff, nextLog2(last - first) * 4);
}

// A variation of quickSort, which partitions elements in stricter fashion (no swaps for elements equal to pivot).
template<typename It>
void quickSortAlt(It first, It last, size_t cutoff = 0)
{
    if (cutoff == 0) {
        cutoff = detail::defaultCutoff<typename std::iterator_traits<It>::value_type>();
    } else if (cutoff < detail::kMinQuickSortCutoff) {
        cutoff = detail::kMinQuickSortCutoff;
    }
    detail::quickSortAltImpl(first, last, cutoff, nextLog2(last - first) * 4);
}

// A variation of quickSort which does three-way partition: partition array into (less than pivot), (pivot)
// and (greater than pivot) arrays.
template<typename It>
void quickSortThreeWay(It first, It last, size_t cutoff = 0)
{
    if (cutoff == 0) {
        cutoff = detail::defaultCutoff<typename std::iterator_traits<It>::value_type>();
    } else if (cutoff < detail::kMinQuickSortCutoff) {
        cutoff = detail::kMinQuickSortCutoff;
    }
    detail::quickSortThreeWayImpl(first, last, cutoff, nextLog2(last - first) * 4);
}

// An implementation of dual-pivot quicksort: partition array into (less than pivot1), (pivot) and (greater than pivot) arrays.
template<typename It>
void quickSortDualPivot(It first, It last, size_t cutoff = 0)
{
    if (cutoff == 0) {
        cutoff = detail::defaultCutoff<typename std::iterator_traits<It>::value_type>();
    } else if (cutoff < detail::kMinQuickSortCutoff) {
        cutoff = detail::kMinQuickSortCutoff;
    }
    detail::quickSortDualPivotImpl(first, last, cutoff, nextLog2(last - first) * 2);
}

// A version of quickSortDualPivot which has a bit different partitioning scheme.
template<typename It>
void quickSortDualPivotAlt(It first, It last, size_t cutoff = 0)
{
    if (cutoff == 0) {
        cutoff = detail::defaultCutoff<typename std::iterator_traits<It>::value_type>();
    } else if (cutoff < detail::kMinQuickSortCutoff) {
        cutoff = detail::kMinQuickSortCutoff;
    }
    detail::quickSortDualPivotAltImpl(first, last, cutoff, nextLog2(last - first) * 2);
}

// A helper to call the required method by name.
template<typename It>
void callSortMethod(char const* sortMethod, It first, It last)
{
    if (strcmp(sortMethod, "std") == 0) {
        std::sort(first, last);
    } else if (strcmp(sortMethod, "stable-std") == 0) {
        std::stable_sort(first, last);
    } else if (strcmp(sortMethod, "quick") == 0) {
        quickSort(first, last);
    } else if (strcmp(sortMethod, "quick-5") == 0) {
        quickSort(first, last, 5);
    } else if (strcmp(sortMethod, "quick-10") == 0) {
        quickSort(first, last, 10);
    } else if (strcmp(sortMethod, "quick-30") == 0) {
        quickSort(first, last, 30);
    } else if (strcmp(sortMethod, "quick-alt") == 0) {
        quickSortAlt(first, last);
    } else if (strcmp(sortMethod, "quick-alt-5") == 0) {
        quickSortAlt(first, last, 5);
    } else if (strcmp(sortMethod, "quick-alt-10") == 0) {
        quickSortAlt(first, last, 10);
    } else if (strcmp(sortMethod, "quick-alt-30") == 0) {
        quickSortAlt(first, last, 30);
    } else if (strcmp(sortMethod, "quick-3way") == 0) {
        quickSortThreeWay(first, last);
    } else if (strcmp(sortMethod, "quick-3way-5") == 0) {
        quickSortThreeWay(first, last, 5);
    } else if (strcmp(sortMethod, "quick-3way-10") == 0) {
        quickSortThreeWay(first, last, 10);
    } else if (strcmp(sortMethod, "quick-3way-30") == 0) {
        quickSortThreeWay(first, last, 30);
    } else if (strcmp(sortMethod, "quick-2pivot") == 0) {
        quickSortDualPivot(first, last);
    } else if (strcmp(sortMethod, "quick-2pivot-5") == 0) {
        quickSortDualPivot(first, last, 5);
    } else if (strcmp(sortMethod, "quick-2pivot-10") == 0) {
        quickSortDualPivot(first, last, 10);
    } else if (strcmp(sortMethod, "quick-2pivot-30") == 0) {
        quickSortDualPivot(first, last, 30);
    } else if (strcmp(sortMethod, "quick-2pivot-alt") == 0) {
        quickSortDualPivotAlt(first, last);
    } else if (strcmp(sortMethod, "quick-2pivot-alt-5") == 0) {
        quickSortDualPivotAlt(first, last, 5);
    } else if (strcmp(sortMethod, "quick-2pivot-alt-10") == 0) {
        quickSortDualPivotAlt(first, last, 10);
    } else if (strcmp(sortMethod, "quick-2pivot-alt-30") == 0) {
        quickSortDualPivotAlt(first, last, 30);
    } else if (strcmp(sortMethod, "heap") == 0) {
        heapSort(first, last, false);
    } else if (strcmp(sortMethod, "heap-std") == 0) {
        heapSort(first, last, true);
    } else if (strcmp(sortMethod, "selection") == 0) {
        selectionSort(first, last);
    } else if (strcmp(sortMethod, "insertion") == 0) {
        insertionSort(first, last);
    } else if (strcmp(sortMethod, "pdqsort") == 0) {
        pdqsort(first, last);
    } else if (strcmp(sortMethod, "pdqsort-branchless") == 0) {
        pdqsort_branchless(first, last);
    } else if (strcmp(sortMethod, "timsort") == 0) {
        gfx::timsort(first, last);
    } else {
        printf("Unknown sorting method %s\n", sortMethod);
        exit(1);
    }
}
