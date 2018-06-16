#pragma once

#include <algorithm>

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

// A trivial quicksort implementation, just for comparison with std::sort. Unlike the std::sort
// this function assumes that copying the values is cheap (when selecting the pivot).
template<typename It>
void quickSort(It first, It last, size_t cutoff = 15)
{
    while (true) {
        if (last - first < 2) {
            return;
        } else if (last - first == 2) {
            if (*first > *(first + 1)) {
                std::swap(*first, *(first + 1));
            }
            return;
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
            return;
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
            return;
        } else if ((size_t)(last - first) <= cutoff) {
            insertionSort(first, last);
            return;
        }

        // Median of 3 selection: median of first, middle, last.
        auto pivot = median3(*first, *(first + (last - first) / 2), *(last - 1));
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
            quickSort(left, last, cutoff);
            last = right + 1;
        } else {
            quickSort(first, right + 1, cutoff);
            first = left;
        }
    }
}
