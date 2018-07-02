#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "common.h"
#include "sort.h"

// Sort testing utilities.

using std::unique_ptr;
using std::string;
using std::vector;

// Simple string view for tests.
struct SimpleStringView
{
    char* ptr;
    size_t length;

    SimpleStringView(char* _ptr, size_t _length)
        : ptr(_ptr)
        , length(_length)
    {
    }
};

// Simple rope-like structure just for tests.
class SimpleStringRope
{
public:
    SimpleStringView allocString(size_t length)
    {
        if (buffers.empty()) {
            return SimpleStringView(allocNewBuffer(length), length);
        } else {
            CharBuffer& last = buffers.back();
            if (last.used + length <= last.size) {
                char* ptr = last.data.get() + last.used;
                last.used += length;
                return SimpleStringView(ptr, length);
            } else {
                return SimpleStringView(allocNewBuffer(length), length);
            }
        }
    }

private:
    size_t const kDefaultBufferSize = 10000;

    struct CharBuffer
    {
        unique_ptr<char[]> data;
        size_t used;
        size_t size;

        CharBuffer(size_t _size)
            : data(new char[_size])
            , used(0)
            , size(_size)
        {
        }
    };
    vector<CharBuffer> buffers;

    char* allocNewBuffer(size_t length)
    {
        size_t allocSize = length > kDefaultBufferSize ? length : kDefaultBufferSize;
        buffers.emplace_back(allocSize);
        CharBuffer& newBuffer = buffers.back();
        newBuffer.used += length;
        return newBuffer.data.get();
    }
};

template<typename T>
size_t findDiffIndex(vector<T> const& array1, vector<T> const& array2)
{
    for (size_t i = 0; i < array1.size(); i++) {
        if (array1[i] != array2[i]) {
            return i;
        }
    }
    return SIZE_MAX;
}

void compareSortInt(char const* sortMethod, vector<int>& array, vector<int>& scratch)
{
    scratch = array;
    callSortMethod(sortMethod, array.begin(), array.end());
    std::sort(scratch.begin(), scratch.end());
    size_t diffIndex = findDiffIndex(array, scratch);
    if (diffIndex != SIZE_MAX) {
        printf("Sorted arrays [%d] differ at index %d:\n", (int)array.size(), (int)diffIndex);
        for (int v : array) {
            printf("%d ", v);
        }
        printf("\nshould be\n");
        for (int v : scratch) {
            printf("%d ", v);
        }
        printf("\n");
        exit(1);
    }
}

void compareSortString(char const* sortMethod, vector<string>& array, vector<string>& scratch)
{
    scratch = array;
    callSortMethod(sortMethod, array.begin(), array.end());
    std::sort(scratch.begin(), scratch.end());
    if (!std::equal(array.begin(), array.end(), scratch.begin())) {
        printf("Sorted arrays [%d] differ:\n", (int)array.size());
        for (string const& v : array) {
            printf("%s ", v.c_str());
        }
        printf("\nvs\n");
        for (string const& v : scratch) {
            printf("%s ", v.c_str());
        }
        printf("\n");
        exit(1);
    }
}

// Preparers int test data and runs sorting method on the data, using std::sort to validate the results.
void testSortInt(char const* sortMethod, size_t minSize, size_t maxSize)
{
    uint64_t totalStartTime = getTimeCounter();
    printf("Running integer tests\n");
    // Arrays is all the test data prepared at once.
    vector<vector<int>> arrays;
    size_t numTests = maxSize - minSize;
    arrays.resize(numTests);
    // Scratch will be used for comparing with std::sort
    vector<int> scratch;
    scratch.resize(maxSize);
    uint64_t startTime;

    for (size_t size = minSize; size < maxSize; size++) {
        vector<int>& array = arrays[size - minSize];
        array.resize(size);
        for (size_t i = 0; i < size; i++) {
            array[i] = 123;
        }
    }
    startTime = getTimeCounter();
    for (size_t size = minSize; size < maxSize; size++) {
        compareSortInt(sortMethod, arrays[size - minSize], scratch);
    }
    printf("Tested one value in %dms\n", elapsedMsec(startTime));

    for (size_t size = minSize; size < maxSize; size++) {
        vector<int>& array = arrays[size - minSize];
        array.resize(size);
        for (size_t i = 0; i < size; i++) {
            array[i] = 10000000 + i;
        }
    }
    startTime = getTimeCounter();
    for (size_t size = minSize; size < maxSize; size++) {
        compareSortInt(sortMethod, arrays[size - minSize], scratch);
    }
    printf("Tested ascending in %dms\n", elapsedMsec(startTime));

    for (size_t size = minSize; size < maxSize; size++) {
        vector<int>& array = arrays[size - minSize];
        array.resize(size);
        for (size_t i = 0; i < size; i++) {
            array[i] = 10000000 - i;
        }
    }
    startTime = getTimeCounter();
    for (size_t size = minSize; size < maxSize; size++) {
        compareSortInt(sortMethod, arrays[size - minSize], scratch);
    }
    printf("Tested descending in %dms\n", elapsedMsec(startTime));

    for (size_t size = minSize; size < maxSize; size++) {
        vector<int>& array = arrays[size - minSize];
        array.resize(size);
        for (size_t i = 0; i < size / 2; i++) {
            array[i] = 10000000 + i;
        }
        for (size_t i = size / 2; i < size; i++) {
            array[i] = 10000000 + size - i;
        }
    }
    startTime = getTimeCounter();
    for (size_t size = minSize; size < maxSize; size++) {
        compareSortInt(sortMethod, arrays[size - minSize], scratch);
    }
    printf("Tested an ascending and descending in %dms\n", elapsedMsec(startTime));

    for (size_t size = minSize; size < maxSize; size++) {
        vector<int>& array = arrays[size - minSize];
        array.resize(size);
        for (size_t i = 0; i < size / 2; i++) {
            array[i] = 10000000 + i;
        }
        for (size_t i = size / 2; i < size; i++) {
            array[i] = 10000000 - size + i;
        }
    }
    startTime = getTimeCounter();
    for (size_t size = minSize; size < maxSize; size++) {
        compareSortInt(sortMethod, arrays[size - minSize], scratch);
    }
    printf("Tested an ascending and ascending in %dms\n", elapsedMsec(startTime));

    for (size_t size = minSize; size < maxSize; size++) {
        vector<int>& array = arrays[size - minSize];
        array.resize(size);
        for (size_t i = 0; i < size / 4; i++) {
            array[i] = 10000000 + i;
        }
        for (size_t i = size / 4; i < size * 3 / 4; i++) {
            array[i] = 10000000 + size / 2;
        }
        for (size_t i = size * 3 / 4; i < size; i++) {
            array[i] = 10000000 + i;
        }
    }
    startTime = getTimeCounter();
    for (size_t size = minSize; size < maxSize; size++) {
        compareSortInt(sortMethod, arrays[size - minSize], scratch);
    }
    printf("Tested an array with a plateu in %dms\n", elapsedMsec(startTime));

    for (size_t size = minSize; size < maxSize; size++) {
        vector<int>& array = arrays[size - minSize];
        array.resize(size);
        uint32_t state[4] = { (uint32_t)size, (uint32_t)size, (uint32_t)size, (uint32_t)size };
        for (size_t i = 0; i < size; i++) {
            array[i] = randomRange(state, 0, 10000000);
        }
    }
    startTime = getTimeCounter();
    for (size_t size = minSize; size < maxSize; size++) {
        compareSortInt(sortMethod, arrays[size - minSize], scratch);
    }
    printf("Tested random in %dms\n", elapsedMsec(startTime));

    for (size_t size = minSize; size < maxSize; size++) {
        vector<int>& array = arrays[size - minSize];
        array.resize(size);
        uint32_t state[4] = { (uint32_t)size, (uint32_t)size, (uint32_t)size, (uint32_t)size };
        for (size_t i = 0; i < size; i++) {
            array[i] = randomRange(state, 0, 10);
        }
    }
    startTime = getTimeCounter();
    for (size_t size = minSize; size < maxSize; size++) {
        compareSortInt(sortMethod, arrays[size - minSize], scratch);
    }
    printf("Tested random small values in %dms\n", elapsedMsec(startTime));

    for (size_t size = minSize; size < maxSize; size++) {
        vector<int>& array = arrays[size - minSize];
        array.resize(size);
        uint32_t state[4] = { (uint32_t)size, (uint32_t)size, (uint32_t)size, (uint32_t)size };
        for (size_t i = 0; i < size; i++) {
            array[i] = randomRange(state, 0, 2);
        }
    }
    startTime = getTimeCounter();
    for (size_t size = minSize; size < maxSize; size++) {
        compareSortInt(sortMethod, arrays[size - minSize], scratch);
    }
    printf("Tested random two values in %dms\n", elapsedMsec(startTime));

    printf("All int tests on %s [%d-%d) passed in %dms\n", sortMethod, (int)minSize, (int)maxSize, elapsedMsec(totalStartTime));
}

// Prints integer to the result, padding it to the required number of symbols with '0'.
void stringFromInt(int value, int maxLen, string* result)
{
    char buf[1000];
    int printed = sprintf(buf, "%d", value);
    if (printed < maxLen) {
        result->resize(maxLen - printed, '0');
    }
    result->assign(buf, printed);
}

// Preparers string test data and runs sorting method on the data, using std::sort to validate the results.
void testSortString(char const* sortMethod, size_t minSize, size_t maxSize)
{
    uint64_t totalStartTime = getTimeCounter();
    printf("Running string tests\n");
    vector<string> array;
    vector<string> scratch;
    array.reserve(maxSize);
    scratch.reserve(maxSize);
    uint64_t startTime;

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        array.resize(size);
        for (size_t i = 0; i < size; i++) {
            stringFromInt(123, 0, &array[i]);
        }
        compareSortString(sortMethod, array, scratch);
    }
    printf("Tested one value in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        array.resize(size);
        for (size_t i = 0; i < size; i++) {
            stringFromInt(10000000 + i, 8, &array[i]);
        }
        compareSortString(sortMethod, array, scratch);
    }
    printf("Tested ascending in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        array.resize(size);
        for (size_t i = 0; i < size; i++) {
            stringFromInt(10000000 - i, 8, &array[i]);
        }
        compareSortString(sortMethod, array, scratch);
    }
    printf("Tested descending in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        array.resize(size);
        for (size_t i = 0; i < size / 2; i++) {
            stringFromInt(10000000 + i, 8, &array[i]);
        }
        for (size_t i = size / 2; i < size; i++) {
            stringFromInt(10000000 + size - i, 8, &array[i]);
        }
        compareSortString(sortMethod, array, scratch);
    }
    printf("Tested an ascending and descending in %dms\n", elapsedMsec(startTime));


    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        array.resize(size);
        for (size_t i = 0; i < size / 2; i++) {
            stringFromInt(10000000 + i, 8, &array[i]);
        }
        for (size_t i = size / 2; i < size; i++) {
            stringFromInt(10000000 - size + i, 8, &array[i]);
        }
        compareSortString(sortMethod, array, scratch);
    }
    printf("Tested an ascending and ascending in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        array.resize(size);
        for (size_t i = 0; i < size / 4; i++) {
            stringFromInt(10000000 + i, 8, &array[i]);
            array[i] = 10000000 + i;
        }
        for (size_t i = size / 4; i < size * 3 / 4; i++) {
            stringFromInt(10000000 + size / 2, 8, &array[i]);
        }
        for (size_t i = size * 3 / 4; i < size; i++) {
            stringFromInt(10000000 + i, 8, &array[i]);
        }
        compareSortString(sortMethod, array, scratch);
    }
    printf("Tested an array with a plateu in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        array.resize(size);
        uint32_t state[4] = { (uint32_t)size, (uint32_t)size, (uint32_t)size, (uint32_t)size };
        for (size_t i = 0; i < size; i++) {
            stringFromInt(randomRange(state, 0, 10000000), 8, &array[i]);
        }
        compareSortString(sortMethod, array, scratch);
    }
    printf("Tested random in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        array.resize(size);
        uint32_t state[4] = { (uint32_t)size, (uint32_t)size, (uint32_t)size, (uint32_t)size };
        for (size_t i = 0; i < size; i++) {
            stringFromInt(randomRange(state, 0, 10), 2, &array[i]);
        }
        compareSortString(sortMethod, array, scratch);
    }
    printf("Tested random small values in %dms\n", elapsedMsec(startTime));

    startTime = getTimeCounter();
    for (size_t size = 0; size < maxSize; size++) {
        array.resize(size);
        uint32_t state[4] = { (uint32_t)size, (uint32_t)size, (uint32_t)size, (uint32_t)size };
        for (size_t i = 0; i < size; i++) {
            stringFromInt(randomRange(state, 0, 2), 1, &array[i]);
        }
        compareSortString(sortMethod, array, scratch);
    }
    printf("Tested random two values in %dms\n", elapsedMsec(startTime));

    printf("All string tests on %s [0-%d) passed in %dms\n", sortMethod, (int)maxSize, elapsedMsec(totalStartTime));
}

void parseSize(char const* arg, size_t* minSize, size_t* maxSize)
{
    char const* sep = strchr(arg, '-');
    if (sep != nullptr) {
        *minSize = (size_t)atoll(arg);
        *maxSize = (size_t)atoll(sep + 1);
    } else {
        *minSize = 0;
        *maxSize = (size_t)atoll(arg);
    }
}


int main(int argc, char** argv)
{
    if (argc != 3 && argc != 4) {
        printf("Usage: %s sort-method size[-size] [int|string]\n", argv[0]);
        return 1;
    }

    char const* sortMethod = argv[1];
    size_t minSize;
    size_t maxSize;
    parseSize(argv[2], &minSize, &maxSize);
    char const* type = argc > 3 ? argv[3] : nullptr;
    if (type == nullptr || strcmp(type, "int") == 0) {
        testSortInt(sortMethod, minSize, maxSize);
    }
    if (type == nullptr || strcmp(type, "string") == 0) {
        testSortString(sortMethod, minSize, maxSize);
    }
    return 0;
}
