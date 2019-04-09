#pragma once

#include <cstring>
#include <string>

#include "common.h"

// If defined, enables the "small compare optimization", which adds the inlined comparison of the
// first few bytes before calling memcmp.
#define USE_SMALL_COMPARE

// The minimal string_view.
struct StringView {
    char const* begin;
    size_t length;

    StringView()
        : begin(nullptr)
        , length(0)
    {
    }

    StringView(char const* _begin, size_t _length)
        : begin(_begin)
        , length(_length)
    {
    }
};

#if defined(COUNT_STRING_COMPARES)
extern size_t compareStrCount;
#endif

namespace detail {

// Defines function uintptr_t load_uintptr(void const* p).
DEFINE_LOAD_STORE(uintptr_t, uintptr)

FORCE_INLINE bool strEqual(char const* begin1, size_t length1, char const* begin2, size_t length2)
{
#if defined(COUNT_STRING_COMPARES)
    compareStrCount++;
#endif
    if (length1 != length2) {
        return false;
    }
#if defined(USE_SMALL_COMPARE)
    if (length1 >= sizeof(intptr_t)) {
        uintptr_t first1 = load_uintptr(begin1);
        uintptr_t first2 = load_uintptr(begin2);
        if (first1 != first2) {
            return false;
        }
        return memcmp(begin1 + sizeof(uintptr_t), begin2 + sizeof(uintptr_t),
                      length1 - sizeof(uintptr_t)) == 0;
    } else {
        for (size_t i = 0; i < length1; i++) {
            if (begin1[i] != begin2[i]) {
                return false;
            }
        }
        return true;
    }
#else
    return memcmp(begin1, begin2, length1) == 0;
#endif
}

FORCE_INLINE bool strLess(char const* begin1, size_t length1, char const* begin2, size_t length2)
{
#if defined(COUNT_STRING_COMPARES)
    compareStrCount++;
#endif
#if defined(USE_SMALL_COMPARE)
    if (length1 >= sizeof(intptr_t) && length2 >= sizeof(intptr_t)) {
        uintptr_t first1 = load_uintptr(begin1);
        uintptr_t first2 = load_uintptr(begin2);
        if (first1 != first2) {
            // This is required, because the first differing byte will be the least significant
            // different byte in case of little endian loads.
#if defined(COMMON_LITTLE_ENDIAN)
            first1 = byteSwap(first1);
            first2 = byteSwap(first2);
#endif
            return first1 < first2;
        }
        int ret = memcmp(begin1 + sizeof(uintptr_t), begin2 + sizeof(uintptr_t),
                         std::min(length1, length2) - sizeof(uintptr_t));
        if (ret != 0) {
            return ret < 0;
        } else {
            return length1 < length2;
        }
    } else {
        size_t minLength = std::min(length1, length2);
        for (size_t i = 0; i < minLength; i++) {
            if (begin1[i] != begin2[i]) {
                return (unsigned char)begin1[i] < (unsigned char)begin2[i];
            }
        }
        return length1 < length2;
    }
#else
    int ret = memcmp(begin1, begin2, std::min(length1, length2));
    if (ret != 0) {
        return ret < 0;
    }
    return length1 < length2;
#endif
}

FORCE_INLINE bool strGreater(char const* begin1, size_t length1, char const* begin2, size_t length2)
{
#if defined(COUNT_STRING_COMPARES)
    compareStrCount++;
#endif
#if defined(USE_SMALL_COMPARE)
    if (length1 >= sizeof(intptr_t) && length2 >= sizeof(intptr_t)) {
        uintptr_t first1 = load_uintptr(begin1);
        uintptr_t first2 = load_uintptr(begin2);
        if (first1 != first2) {
            // This is required, because the first differing byte will be the least significant
            // different byte in case of little endian loads.
#if defined(COMMON_LITTLE_ENDIAN)
            first1 = byteSwap(first1);
            first2 = byteSwap(first2);
#endif
            return first1 > first2;
        }
        int ret = memcmp(begin1 + sizeof(uintptr_t), begin2 + sizeof(uintptr_t),
                         std::min(length1, length2) - sizeof(uintptr_t));
        if (ret != 0) {
            return ret > 0;
        } else {
            return length1 > length2;
        }
    } else {
        size_t minLength = std::min(length1, length2);
        for (size_t i = 0; i < minLength; i++) {
            if (begin1[i] != begin2[i]) {
                return (unsigned char)begin1[i] > (unsigned char)begin2[i];
            }
        }
        return length1 > length2;
    }
#else
    int ret = memcmp(begin1, begin2, std::min(length1, length2));
    if (ret != 0) {
        return ret > 0;
    }
    return length1 > length2;
#endif
}

} // namespace detail

FORCE_INLINE bool operator==(StringView line1, StringView line2)
{
    return detail::strEqual(line1.begin, line1.length, line2.begin, line2.length);
}

FORCE_INLINE bool operator!=(StringView line1, StringView line2)
{
    return !detail::strEqual(line1.begin, line1.length, line2.begin, line2.length);
}

FORCE_INLINE bool operator<(StringView line1, StringView line2)
{
    return detail::strLess(line1.begin, line1.length, line2.begin, line2.length);
}

FORCE_INLINE bool operator<=(StringView line1, StringView line2)
{
    return !detail::strGreater(line1.begin, line1.length, line2.begin, line2.length);
}

FORCE_INLINE bool operator>(StringView line1, StringView line2)
{
    return detail::strGreater(line1.begin, line1.length, line2.begin, line2.length);
}

FORCE_INLINE bool operator>=(StringView line1, StringView line2)
{
    return !detail::strLess(line1.begin, line1.length, line2.begin, line2.length);
}

FORCE_INLINE bool operator==(StringView line1, std::string const& line2)
{
    return detail::strEqual(line1.begin, line1.length, line2.c_str(), line2.length());
}

FORCE_INLINE bool operator!=(StringView line1, std::string const& line2)
{
    return !detail::strEqual(line1.begin, line1.length, line2.c_str(), line2.length());
}

FORCE_INLINE bool operator<(StringView line1, std::string const& line2)
{
    return detail::strLess(line1.begin, line1.length, line2.c_str(), line2.length());
}

FORCE_INLINE bool operator<=(StringView line1, std::string const& line2)
{
    return !detail::strGreater(line1.begin, line1.length, line2.c_str(), line2.length());
}

FORCE_INLINE bool operator>(StringView line1, std::string const& line2)
{
    return detail::strGreater(line1.begin, line1.length, line2.c_str(), line2.length());
}

FORCE_INLINE bool operator>=(StringView line1, std::string const& line2)
{
    return !detail::strLess(line1.begin, line1.length, line2.c_str(), line2.length());
}

FORCE_INLINE bool operator==(std::string const& line1, StringView line2)
{
    return detail::strEqual(line1.c_str(), line1.length(), line2.begin, line2.length);
}

FORCE_INLINE bool operator!=(std::string const& line1, StringView line2)
{
    return !detail::strEqual(line1.c_str(), line1.length(), line2.begin, line2.length);
}

FORCE_INLINE bool operator<(std::string const& line1, StringView line2)
{
    return detail::strLess(line1.c_str(), line1.length(), line2.begin, line2.length);
}

FORCE_INLINE bool operator<=(std::string const& line1, StringView line2)
{
    return !detail::strGreater(line1.c_str(), line1.length(), line2.begin, line2.length);
}

FORCE_INLINE bool operator>(std::string const& line1, StringView line2)
{
    return detail::strGreater(line1.c_str(), line1.length(), line2.begin, line2.length);
}

FORCE_INLINE bool operator>=(std::string const& line1, StringView line2)
{
    return !detail::strLess(line1.c_str(), line1.length(), line2.begin, line2.length);
}
