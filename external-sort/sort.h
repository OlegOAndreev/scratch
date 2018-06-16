#pragma once

#include <algorithm>

// A trivial quicksort implementation, just for comparison with std::sort and quickSort.
template<typename It>
void insertionSort(It first, It last)
{
    if (first == last) {
        return;
    }
    for (It i = first; i < last - 1; i++) {
        It minI = i;
        for (It j = i + 1; j < last; j++) {
            if (*minI > *j) {
                minI = j;
            }
        }
        if (i != minI) {
            std::swap(*i, *minI);
        }
    }
}

// A trivial quicksort implementation, just for comparison with std::sort.
template<typename It>
void quickSort(It first, It last, size_t insertionCutoff = 8)
{
    while (true) {
        if (last - first < 2) {
            return;
        }
        if ((size_t)(last - first) <= insertionCutoff) {
            insertionSort(first, last);
            return;
        }
        // Trivial pivot selection.
        auto pivot = *(first + (last - first) / 2);
        // Partition.
        It left = first, right = last - 1;
        while (left < right) {
            while (*left < pivot) {
                left++;
            }
            while (pivot < *right) {
                right--;
            }
            if (left < right) {
                std::swap(*left, *right);
                left++;
                right--;
            }
        }
        if (right - first > last - left) {
            quickSort(left, last, insertionCutoff);
            last = right + 1;
        } else {
            quickSort(first, right + 1, insertionCutoff);
            first = left;
        }
    }
}
