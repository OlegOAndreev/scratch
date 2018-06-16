#pragma once

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "common.h"
#include "sort.h"

// Sort testing utilities.

namespace {

using std::vector;

template<typename It>
void callSortMethod(char const* sortMethod, It first, It last)
{
    if (strcmp(sortMethod, "std") == 0) {
        std::sort(first, last);
    } else if (strcmp(sortMethod, "quick") == 0) {
        quickSort(first, last);
    } else if (strcmp(sortMethod, "quick-2") == 0) {
        quickSort(first, last, 2);
    } else if (strcmp(sortMethod, "quick-4") == 0) {
        quickSort(first, last, 4);
    } else if (strcmp(sortMethod, "quick-16") == 0) {
        quickSort(first, last, 16);
    } else if (strcmp(sortMethod, "insertion") == 0) {
        insertionSort(first, last);
    } else {
        printf("Unknown sorting method %s\n", sortMethod);
        exit(1);
    }
}

void compareSortInt(char const* sortMethod, vector<int>& array, vector<int>& scratch)
{
    scratch = array;
    callSortMethod(sortMethod, array.begin(), array.end());
    std::sort(scratch.begin(), scratch.end());
    if (!std::equal(array.begin(), array.end(), scratch.begin())) {
        printf("Sorted arrays [%d] differ:\n", (int)array.size());
        for (int v : array) {
            printf("%d ", v);
        }
        printf("\nvs\n");
        for (int v : scratch) {
            printf("%d ", v);
        }
        printf("\n");
        exit(1);
    }
}

// Preparers test data and runs sorting method on the data, using std::sort to validate the results.
void testSort(char const* sortMethod, size_t maxSize)
{
    uint64_t totalStartTime = getTimeCounter();
    printf("Running integer tests\n");
    vector<int> array;
    vector<int> scratch;
    array.reserve(maxSize);
    scratch.reserve(maxSize);

    uint64_t startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        array.clear();
        for (size_t i = 0; i < size; i++) {
            array.push_back(123);
        }
        compareSortInt(sortMethod, array, scratch);
    }
    printf("Tested one value in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        for (size_t i = 0; i < size; i++) {
            array[i] = 10000000 + i;
        }
        compareSortInt(sortMethod, array, scratch);
    }
    printf("Tested ascending in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        for (size_t i = 0; i < size; i++) {
            array[i] = 10000000 - i;
        }
        compareSortInt(sortMethod, array, scratch);
    }
    printf("Tested descending in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        for (size_t i = 0; i < size / 2; i++) {
            array[i] = 10000000 + i;
        }
        for (size_t i = size / 2; i < size; i++) {
            array[i] = 10000000 + size - i;
        }
        compareSortInt(sortMethod, array, scratch);
    }
    printf("Tested an ascending and descending in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        for (size_t i = 0; i < size / 2; i++) {
            array[i] = 10000000 - i;
        }
        for (size_t i = size / 2; i < size; i++) {
            array[i] = 10000000 - size + i;
        }
        compareSortInt(sortMethod, array, scratch);
    }
    printf("Tested a descending and ascending in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        for (size_t i = 0; i < size / 2; i++) {
            array[i] = 10000000 + i;
        }
        for (size_t i = size / 2; i < size; i++) {
            array[i] = 10000000 - size / 2 + i;
        }
        compareSortInt(sortMethod, array, scratch);
    }
    printf("Tested an ascending and ascending in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        for (size_t i = 0; i < size / 2; i++) {
            array[i] = 10000000 + i;
        }
        for (size_t i = size / 2; i < size; i++) {
            array[i] = 10000000 - size + i;
        }
        compareSortInt(sortMethod, array, scratch);
    }
    printf("Tested an ascending and ascending (pt. 2) in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        for (size_t i = 0; i < size / 4; i++) {
            array[i] = 10000000 + i;
        }
        for (size_t i = size / 4; i < size * 3 / 4; i++) {
            array[i] = 10000000 + size / 2;
        }
        for (size_t i = size * 3 / 4; i < size; i++) {
            array[i] = 10000000 + i;
        }
        compareSortInt(sortMethod, array, scratch);
    }
    printf("Tested an array with a plateu in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        uint32_t state[4] = { (uint32_t)size, (uint32_t)size, (uint32_t)size, (uint32_t)size };
        for (size_t i = 0; i < size; i++) {
            array[i] = randomRange(state, 0, 10000000);
        }
        compareSortInt(sortMethod, array, scratch);
    }
    printf("Tested random in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        uint32_t state[4] = { (uint32_t)size, (uint32_t)size, (uint32_t)size, (uint32_t)size };
        for (size_t i = 0; i < size; i++) {
            array[i] = randomRange(state, 0, 10);
        }
        compareSortInt(sortMethod, array, scratch);
    }
    printf("Tested random small values in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        uint32_t state[4] = { (uint32_t)size, (uint32_t)size, (uint32_t)size, (uint32_t)size };
        for (size_t i = 0; i < size; i++) {
            array[i] = randomRange(state, 0, 2);
        }
        compareSortInt(sortMethod, array, scratch);
    }
    printf("Tested random two values in %dms\n", elapsedMsec(startTime));

    printf("All tests on %s [0-%d) passed in %dms\n", sortMethod, (int)maxSize, elapsedMsec(totalStartTime));
}

} // namespace
