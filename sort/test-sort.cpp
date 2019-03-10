#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "common.h"
#include "sort.h"

// If defined, count each SaferInt and SimpleStringView compare, copy and move.
#define COUNT_OPS

// Exclude other testing arrays, use only randomly generated.
#define ONLY_RANDOM

#if defined(COUNT_OPS)
int numSaferIntCompares = 0;
int numSaferIntCopies = 0;
int numSaferIntMoves = 0;
int numStringViewCompares = 0;
int numStringViewCopies = 0;
int numStringViewMoves = 0;
#endif


// An int with a destructive move behavior.
struct SaferInt {
    SaferInt()
        : value(INT_MIN)
    {
    }

    SaferInt(int _value)
        : value(_value)
    {
    }

    SaferInt(SaferInt const& other)
        : value(other.value)
    {
#if defined(COUNT_OPS)
        numSaferIntCopies++;
#endif
    }

    SaferInt(SaferInt&& other) noexcept
        : value(other.value)
    {
#if defined(COUNT_OPS)
        numSaferIntMoves++;
#endif
        other.value = INT_MIN;
    }

    ~SaferInt()
    {
        value = INT_MIN;
    }

    SaferInt& operator=(SaferInt const& other)
    {
#if defined(COUNT_OPS)
        numSaferIntCopies++;
#endif
        value = other.value;
        return *this;
    }

    SaferInt& operator=(SaferInt&& other) noexcept
    {
#if defined(COUNT_OPS)
        numSaferIntMoves++;
#endif
        value = other.value;
        other.value = INT_MIN;
        return *this;
    }

    bool operator==(SaferInt const& other) const
    {
        return value == other.value;
    }

    bool operator<(SaferInt const& other) const
    {
#if defined(COUNT_OPS)
        numSaferIntCompares++;
#endif
        return value < other.value;
    }

    int value;
};

// Small compare optimization: only load the few bytes from the start of the strings and compare
// them. If they are equal, compare the rest.

// If defined, use inlined small compare optimization with intptr_t, is exclusive
// with USE_SMALL_COMPARE_INT32.
#define USE_SMALL_COMPARE_UINTPTR
// If defined, use inlined small compare optimization with int32_t, is exclusive
// with USE_SMALL_COMPARE_INTPTR.
//#define USE_SMALL_COMPARE_UINT32

#if defined(USE_SMALL_COMPARE_UINTPTR)
using SmallCompareType = size_t;

// Defines function uintptr_t load_uintptr(void const* p).
DEFINE_LOAD_STORE(uintptr_t, uintptr)

FORCE_INLINE SmallCompareType load_smallCompareType(void const* p)
{
    return load_uintptr(p);
}
#elif defined(USE_SMALL_COMPARE_UINT32)
using SmallCompareType = uint32_t;

FORCE_INLINE SmallCompareType load_smallCompareType(void const* p)
{
    return load_i32(p);
}
#endif


// Simple string view for tests.
struct SimpleStringView {
    char* ptr;
    size_t length;

    SimpleStringView()
        : ptr(nullptr)
        , length(0)
    {
    }

    SimpleStringView(SimpleStringView const& other)
        : ptr(other.ptr)
        , length(other.length)
    {
#if defined(COUNT_OPS)
        numStringViewCopies++;
#endif
    }

    SimpleStringView(SimpleStringView&& other) noexcept
        : ptr(other.ptr)
        , length(other.length)
    {
#if defined(COUNT_OPS)
        numStringViewMoves++;
#endif
    }

    SimpleStringView(char* _ptr, size_t _length)
        : ptr(_ptr)
        , length(_length)
    {
    }

    SimpleStringView& operator=(SimpleStringView const& other)
    {
#if defined(COUNT_OPS)
        numStringViewCopies++;
#endif
        ptr = other.ptr;
        length = other.length;
        return *this;
    }

    SimpleStringView& operator=(SimpleStringView&& other) noexcept
    {
#if defined(COUNT_OPS)
        numStringViewMoves++;
#endif
        ptr = other.ptr;
        length = other.length;
        return *this;
    }

    bool operator==(SimpleStringView const& other) const
    {
        if (length != other.length) {
            return false;
        }
#if defined(USE_SMALL_COMPARE_UINTPTR) || defined(USE_SMALL_COMPARE_UINT32)
        char const* ptr1 = ptr;
        char const* ptr2 = other.ptr;
        size_t length1 = length;
        if (length1 >= sizeof(SmallCompareType)) {
            SmallCompareType start1 = load_smallCompareType(ptr1);
            SmallCompareType start2 = load_smallCompareType(ptr2);
            if (start1 != start2) {
                return false;
            }
            ptr1 += sizeof(SmallCompareType);
            ptr2 += sizeof(SmallCompareType);
            length1 -= sizeof(SmallCompareType);
        }
        return memcmp(ptr1, ptr2, length1) == 0;
#else
        return memcmp(ptr, other.ptr, length) == 0;
#endif
    }


    bool operator<(SimpleStringView const& other) const
    {
#if defined(COUNT_OPS)
        numStringViewCompares++;
#endif
#if defined(USE_SMALL_COMPARE_UINTPTR) || defined(USE_SMALL_COMPARE_UINT32)
        char const* ptr1 = ptr;
        char const* ptr2 = other.ptr;
        size_t length1 = length;
        size_t length2 = other.length;
        if (length1 >= sizeof(SmallCompareType) && length2 >= sizeof(SmallCompareType)) {
            SmallCompareType start1 = load_smallCompareType(ptr1);
            SmallCompareType start2 = load_smallCompareType(ptr2);
            if (start1 != start2) {
                // start1 and start2 must be loaded as big-endian in order to be compared
                // (the first bytes are the most important).
#if defined(COMMON_LITTLE_ENDIAN)
                start1 = byteSwap(start1);
                start2 = byteSwap(start2);
#endif
                return start1 < start2;
            }
            ptr1 += sizeof(SmallCompareType);
            ptr2 += sizeof(SmallCompareType);
            length1 -= sizeof(SmallCompareType);
            length2 -= sizeof(SmallCompareType);
        }
        int ret = memcmp(ptr1, ptr2, std::min(length1, length2));
        if (ret != 0) {
            return ret < 0;
        } else {
            return length1 < length2;
        }
#else
        int ret = memcmp(ptr, other.ptr, std::min(length, other.length));
        if (ret != 0) {
            return ret < 0;
        } else {
            return length < other.length;
        }
#endif
    }
};

// Simple rope-like structure just for tests.
class SimpleStringRope {
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

    struct CharBuffer {
        std::unique_ptr<char[]> data;
        size_t used;
        size_t size;

        CharBuffer(size_t _size)
            : data(new char[_size])
            , used(0)
            , size(_size)
        {
        }
    };
    std::vector<CharBuffer> buffers;

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
size_t findDiffIndex(std::vector<T> const& array1, std::vector<T> const& array2)
{
    for (size_t i = 0; i < array1.size(); i++) {
        if (!(array1[i] == array2[i])) {
            return i;
        }
    }
    return SIZE_MAX;
}

void printFaster(char const* sortMethod1, char const* sortMethod2, double ratio)
{
    if (ratio > 1.25) {
        printf("%s MUCH faster", sortMethod2);
    } else if (ratio > 1.02) {
        printf("%s faster", sortMethod2);
    } else if (ratio < 0.8) {
        printf("%s MUCH faster", sortMethod1);
    } else if (ratio < 0.98) {
        printf("%s faster", sortMethod1);
    } else {
        printf("equal");
    }
}

template<typename T, typename ToStdout>
double compareSortImpl(ToStdout const& toStdout, char const* sortMethod1, char const* sortMethod2,
                       std::vector<std::vector<T>>& arrays1, std::vector<std::vector<T>>& arrays2,
                       char const* arrayType)
{
    uint64_t startTime1 = getTimeTicks();
    for (std::vector<T>& array : arrays1) {
        callSortMethod(sortMethod1, array.begin(), array.end());
    }
    int runTime1 = elapsedMsec(startTime1);
    uint64_t startTime2 = getTimeTicks();
    for (std::vector<T>& array : arrays2) {
        callSortMethod(sortMethod2, array.begin(), array.end());
    }
    int runTime2 = elapsedMsec(startTime2);

    for (size_t i = 0; i < arrays1.size(); i++) {
        std::vector<T> const& array1 = arrays1[i];
        std::vector<T> const& array2 = arrays2[i];
        size_t diffIndex = findDiffIndex(array1, array2);
        if (diffIndex != SIZE_MAX) {
            printf("Sorted arrays [%d] differ at index %d:\n", (int)array1.size(), (int)diffIndex);
            for (T const& v : array1) {
                toStdout(v);
                printf(" ");
            }
            printf("\nvs\n");
            for (T const& v : array2) {
                toStdout(v);
                printf(" ");
            }
            printf("\n");
            exit(1);
        }
    }

    double ratio = (double)runTime1 / runTime2;
    printf("%s: %dms (%s) vs %dms (%s) (%.2f, ", arrayType, runTime1, sortMethod1, runTime2,
           sortMethod2, ratio);
    printFaster(sortMethod1, sortMethod2, ratio);
    printf(")\n");
    return ratio;
}

template<typename T, typename ToStdout>
void compareSort(ToStdout const& toStdout, char const* sortMethod1, char const* sortMethod2,
                 std::vector<std::vector<T>>& arrays, char const* arrayType)
{
    // Sort two times, compare the ratios.
    std::vector<std::vector<T>> arraysCopy1 = arrays;
    std::vector<std::vector<T>> arraysCopy2 = arrays;
    double ratio1 = compareSortImpl(toStdout, sortMethod1, sortMethod2, arraysCopy1, arraysCopy2,
                                    arrayType);
    arraysCopy1 = arrays;
    arraysCopy2 = arrays;
    double ratio2 = compareSortImpl(toStdout, sortMethod2, sortMethod1, arraysCopy1, arraysCopy2,
                                    arrayType);
    double diff = ratio1 * ratio2;
    if (diff > 1.1 || diff < 0.9) {
        printf("== WARNING: FLAKY RESULTS ==\n");
    }
}

// Preparers test data and runs sorting method on the data, using std::sort to validate the results.
// Generator objects must have T operator(size_t value), which generates a new value from size_t.
// ToStdout objects must have void operator(T const& value), which writes the value to stdout.
template<typename T, typename Generator, typename ToStdout>
void testSortImpl(char const* typeName, Generator const& generator, ToStdout const& toStdout,
                  char const* sortMethod1, char const* sortMethod2, size_t minSize, size_t maxSize)
{
    uint64_t totalStartTime = getTimeTicks();
    printf("Running %s tests [%d-%d)\n", typeName, (int)minSize, (int)maxSize);

    // Arrays is all the test data prepared at once.
    std::vector<std::vector<T>> arrays;
    size_t numTests = maxSize - minSize;
    arrays.resize(numTests);

#if !defined(ONLY_RANDOM)
    for (size_t size = minSize; size < maxSize; size++) {
        vector<T>& array = arrays[size - minSize];
        array.resize(size);
        for (size_t i = 0; i < size; i++) {
            array[i] = generator(123);
        }
    }
    compareSort<T>(toStdout, sortMethod1, sortMethod2, arrays, "one value");
#endif

#if !defined(ONLY_RANDOM)
    for (size_t size = minSize; size < maxSize; size++) {
        vector<T>& array = arrays[size - minSize];
        array.resize(size);
        for (size_t i = 0; i < size; i++) {
            array[i] = generator(10000000 + i);
        }
    }
    compareSort<T>(toStdout, sortMethod1, sortMethod2, arrays, "ascending");
#endif

#if !defined(ONLY_RANDOM)
    for (size_t size = minSize; size < maxSize; size++) {
        vector<T>& array = arrays[size - minSize];
        array.resize(size);
        for (size_t i = 0; i < size; i++) {
            array[i] = generator(10000000 - i);
        }
    }
    compareSort<T>(toStdout, sortMethod1, sortMethod2, arrays, "descending");
#endif

#if !defined(ONLY_RANDOM)
    for (size_t size = minSize; size < maxSize; size++) {
        vector<T>& array = arrays[size - minSize];
        array.resize(size);
        for (size_t i = 0; i < size / 2; i++) {
            array[i] = generator(10000000 + i);
        }
        for (size_t i = size / 2; i < size; i++) {
            array[i] = generator(10000000 + size - i);
        }
    }
    compareSort<T>(toStdout, sortMethod1, sortMethod2, arrays, "ascending and descending");
#endif

#if !defined(ONLY_RANDOM)
    for (size_t size = minSize; size < maxSize; size++) {
        vector<T>& array = arrays[size - minSize];
        array.resize(size);
        for (size_t i = 0; i < size / 2; i++) {
            array[i] = generator(10000000 + i);
        }
        for (size_t i = size / 2; i < size; i++) {
            array[i] = generator(10000000 - size + i);
        }
    }
    compareSort<T>(toStdout, sortMethod1, sortMethod2, arrays, "ascending and ascending");
#endif

#if !defined(ONLY_RANDOM)
    for (size_t size = minSize; size < maxSize; size++) {
        vector<T>& array = arrays[size - minSize];
        array.resize(size);
        for (size_t i = 0; i < size / 4; i++) {
            array[i] = generator(10000000 + i);
        }
        for (size_t i = size / 4; i < size * 3 / 4; i++) {
            array[i] = generator(10000000 + size / 2);
        }
        for (size_t i = size * 3 / 4; i < size; i++) {
            array[i] = generator(10000000 + i);
        }
    }
    compareSort<T>(toStdout, sortMethod1, sortMethod2, arrays, "an array with a plateu");
#endif

    {
        uint32_t seed = (uint32_t)(minSize + maxSize);
        uint32_t state[4] = { seed, seed, seed, seed };
        for (size_t size = minSize; size < maxSize; size++) {
            std::vector<T>& array = arrays[size - minSize];
            array.resize(size);
            for (size_t i = 0; i < size; i++) {
                array[i] = generator(randomRange(state, 0, 10000000));
            }
        }
        compareSort<T>(toStdout, sortMethod1, sortMethod2, arrays, "random");
    }

#if !defined(ONLY_RANDOM)
    {
        uint32_t seed = (uint32_t)(minSize + maxSize);
        uint32_t state[4] = { seed, seed, seed, seed };
        for (size_t size = minSize; size < maxSize; size++) {
            vector<T>& array = arrays[size - minSize];
            array.resize(size);
            for (size_t i = 0; i < size; i++) {
                array[i] = generator(randomRange(state, 0, 50));
            }
        }
        compareSort<T>(toStdout, sortMethod1, sortMethod2, arrays, "random small values");
    }
#endif

#if !defined(ONLY_RANDOM)
    {
        uint32_t seed = (uint32_t)(minSize + maxSize);
        uint32_t state[4] = { seed, seed, seed, seed };
        for (size_t size = minSize; size < maxSize; size++) {
            vector<T>& array = arrays[size - minSize];
            array.resize(size);
            for (size_t i = 0; i < size; i++) {
                array[i] = generator(randomRange(state, 0, 2));
            }
        }
        compareSort<T>(toStdout, sortMethod1, sortMethod2, arrays, "random two values");
    }
#endif

    printf("All %s tests on %s vs %s [%d-%d) passed in %dms\n",
           typeName, sortMethod1, sortMethod2, (int)minSize, (int)maxSize,
           elapsedMsec(totalStartTime));
    printf("-------------------------\n");
}

void testSortInt(char const* sortMethod1, char const* sortMethod2, size_t minSize, size_t maxSize)
{
    auto generator = [] (size_t value) {
        return value;
    };
    auto toStdout = [] (int value) {
        printf("%d", value);
    };
    testSortImpl<int>("int", generator, toStdout, sortMethod1, sortMethod2, minSize, maxSize);
}

void testSortSaferInt(char const* sortMethod1, char const* sortMethod2, size_t minSize,
                      size_t maxSize)
{
    auto generator = [] (size_t value) {
        return SaferInt(value);
    };
    auto toStdout = [] (SaferInt value) {
        printf("%d", value.value);
    };
    testSortImpl<SaferInt>("SaferInt", generator, toStdout, sortMethod1, sortMethod2, minSize,
                           maxSize);
}

void testSortDouble(char const* sortMethod1, char const* sortMethod2, size_t minSize,
                    size_t maxSize)
{
    auto generator = [] (size_t value) {
        return double(value) * 1.234;
    };
    auto toStdout = [] (double value) {
        printf("%g", value);
    };
    testSortImpl<double>("double", generator, toStdout, sortMethod1, sortMethod2, minSize, maxSize);
}

void testSortString(char const* sortMethod1, char const* sortMethod2, size_t minSize,
                    size_t maxSize)
{
    int maxLen = 8;
    auto generator = [maxLen] (size_t value) {
        // Prints integer to the result, padding it to the required number of symbols with '0'.
        char buf[1000];
        int printed = sprintf(buf, "%d", (int)value);
        std::string result;
        if (printed < maxLen) {
            result.resize(maxLen - printed, '0');
        }
        result.append(buf, printed);
        return result;
    };
    auto toStdout = [] (std::string const& str) {
        printf("%s", str.c_str());
    };

    testSortImpl<std::string>("std::string", generator, toStdout, sortMethod1, sortMethod2,
                              minSize, maxSize);
}

void testSortBigString(char const* sortMethod1, char const* sortMethod2, size_t minSize,
                       size_t maxSize)
{
    int maxLen = 100;
    auto generator = [maxLen] (size_t value) {
        // Prints integer to the result, padding it to the required number of symbols with '0'.
        char buf[1000];
        int printed = sprintf(buf, "%d", (int)value);
        std::string result;
        if (printed < maxLen) {
            result.resize(maxLen - printed, '0');
        }
        result.append(buf, printed);
        return result;
    };
    auto toStdout = [] (std::string const& str) {
        printf("%s", str.c_str());
    };

    testSortImpl<std::string>("std::string-big", generator, toStdout, sortMethod1, sortMethod2,
                              minSize, maxSize);
}

void testSortStringView(char const* sortMethod1, char const* sortMethod2, size_t minSize,
                        size_t maxSize)
{
    SimpleStringRope rope;
    int maxLen = 8;
    auto generator = [maxLen, &rope] (size_t value) {
        // Prints integer to the result, padding it to the required number of symbols with '0'.
        char buf[1000];
        int printed = sprintf(buf, "%d", (int)value);
        if (printed < maxLen) {
            SimpleStringView result = rope.allocString(maxLen);
            memset(result.ptr, '0', maxLen - printed);
            memcpy(result.ptr + maxLen - printed, buf, printed);
            return result;
        } else {
            SimpleStringView result = rope.allocString(printed);
            memcpy(result.ptr, buf, printed);
            return result;
        }
    };
    auto toStdout = [] (SimpleStringView view) {
        printf("%.*s", (int)view.length, view.ptr);
    };

    testSortImpl<SimpleStringView>("SimpleStringView", generator, toStdout, sortMethod1,
                                   sortMethod2, minSize, maxSize);
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
    if (argc < 3 || argc > 5) {
        printf("Usage: %s sort-method sort-method [int|safer-int|double|string|big-string"
               "|string-view] [size[-size]]\n", argv[0]);
        return 1;
    }

    char const* sortMethod1 = argv[1];
    char const* sortMethod2 = argv[2];
    if (argc == 3 || argc == 4) {
        printf("Running with default sizes\n");
        char const* type = argc > 3 ? argv[3] : nullptr;
        // Some default sizes, which are not too slow and not too fast on my machine
        // (Macbook Pro 13 2015).
        if (type == nullptr || strcmp(type, "int") == 0) {
            testSortInt(sortMethod1, sortMethod2, 0, 8000);
            testSortInt(sortMethod1, sortMethod2, 10000, 11000);
            testSortInt(sortMethod1, sortMethod2, 1000000, 1000005);
        }
        if (type == nullptr || strcmp(type, "safer-int") == 0) {
            testSortSaferInt(sortMethod1, sortMethod2, 0, 8000);
            testSortSaferInt(sortMethod1, sortMethod2, 10000, 11000);
            testSortSaferInt(sortMethod1, sortMethod2, 1000000, 1000005);
        }
        if (type == nullptr || strcmp(type, "double") == 0) {
            testSortDouble(sortMethod1, sortMethod2, 0, 8000);
            testSortDouble(sortMethod1, sortMethod2, 10000, 11000);
            testSortDouble(sortMethod1, sortMethod2, 1000000, 1000005);
        }
        if (type == nullptr || strcmp(type, "string") == 0) {
            testSortString(sortMethod1, sortMethod2, 0, 3000);
            testSortString(sortMethod1, sortMethod2, 10000, 10250);
            testSortString(sortMethod1, sortMethod2, 100000, 100050);
        }
        if (type == nullptr || strcmp(type, "big-string") == 0) {
            testSortBigString(sortMethod1, sortMethod2, 0, 2000);
            testSortBigString(sortMethod1, sortMethod2, 10000, 10200);
            testSortBigString(sortMethod1, sortMethod2, 100000, 100020);
        }
        if (type == nullptr || strcmp(type, "string-view") == 0) {
            testSortStringView(sortMethod1, sortMethod2, 0, 4000);
            testSortStringView(sortMethod1, sortMethod2, 10000, 10500);
            testSortStringView(sortMethod1, sortMethod2, 100000, 100050);
        }
    } else {
        char const* type = argv[3];
        size_t minSize;
        size_t maxSize;
        parseSize(argv[4], &minSize, &maxSize);
        if (strcmp(type, "int") == 0) {
            testSortInt(sortMethod1, sortMethod2, minSize, maxSize);
        } else if (strcmp(type, "safer-int") == 0) {
            testSortSaferInt(sortMethod1, sortMethod2, minSize, maxSize);
        } else if (strcmp(type, "double") == 0) {
            testSortDouble(sortMethod1, sortMethod2, minSize, maxSize);
        } else if (strcmp(type, "string") == 0) {
            testSortString(sortMethod1, sortMethod2, minSize, maxSize);
        } else if (strcmp(type, "big-string") == 0) {
            testSortBigString(sortMethod1, sortMethod2, minSize, maxSize);
        } else if (strcmp(type, "string-view") == 0) {
            testSortStringView(sortMethod1, sortMethod2, minSize, maxSize);
        }
    }

#if defined(COUNT_OPS)
    printf("Number copies: SaferInt %d, SimpleStringView %d\n", numSaferIntCopies,
           numStringViewCopies);
    printf("Number moves: SaferInt %d, SimpleStringView %d\n", numSaferIntMoves,
           numStringViewMoves);
    printf("Number compares: SaferInt %d, SimpleStringView %d\n", numSaferIntCompares,
           numStringViewCompares);
#endif

    return 0;
}
