#pragma once

#include <cstring>
#include <string>

#include "common.h"

// Your minimalist string view.
struct StringView
{
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

inline bool strEqual(char const* begin1, size_t length1, char const* begin2, size_t length2)
{
#if defined(COUNT_STRING_COMPARES)
    compareStrCount++;
#endif
    if (length1 != length2) {
        return false;
    }
    if (length1 >= sizeof(intptr_t)) {
        uintptr_t first1 = load_uintptr(begin1);
        uintptr_t first2 = load_uintptr(begin2);
        if (first1 != first2) {
            return false;
        }
        return memcmp(begin1 + sizeof(uintptr_t), begin2 + sizeof(uintptr_t), length1 - sizeof(uintptr_t)) == 0;
    } else {
        for (size_t i = 0; i < length1; i++) {
            if (begin1[i] != begin2[i]) {
                return false;
            }
        }
        return true;
    }
}

inline bool strLess(char const* begin1, size_t length1, char const* begin2, size_t length2)
{
#if defined(COUNT_STRING_COMPARES)
    compareStrCount++;
#endif
    if (length1 >= sizeof(intptr_t) && length2 >= sizeof(intptr_t)) {
        uintptr_t first1 = load_uintptr(begin1);
        uintptr_t first2 = load_uintptr(begin2);
        if (first1 != first2) {
            // This is required, because the first differing byte will be the least significant different byte in case of
            // little endian loads.
#if defined(COMMON_LITTLE_ENDIAN)
            first1 = byteSwap(first1);
            first2 = byteSwap(first2);
#endif
            return first1 < first2;
        }
        int ret = memcmp(begin1 + sizeof(uintptr_t), begin2 + sizeof(uintptr_t), std::min(length1, length2) - sizeof(uintptr_t));
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
}

inline bool strGreater(char const* begin1, size_t length1, char const* begin2, size_t length2)
{
#if defined(COUNT_STRING_COMPARES)
    compareStrCount++;
#endif
    if (length1 >= sizeof(intptr_t) && length2 >= sizeof(intptr_t)) {
        uintptr_t first1 = load_uintptr(begin1);
        uintptr_t first2 = load_uintptr(begin2);
        if (first1 != first2) {
            // This is required, because the first differing byte will be the least significant different byte in case of
            // little endian loads.
#if defined(COMMON_LITTLE_ENDIAN)
            first1 = byteSwap(first1);
            first2 = byteSwap(first2);
#endif
            return first1 > first2;
        }
        int ret = memcmp(begin1 + sizeof(uintptr_t), begin2 + sizeof(uintptr_t), std::min(length1, length2) - sizeof(uintptr_t));
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
}

} // namespace detail

inline bool operator==(StringView line1, StringView line2)
{
    if (line1.length != line2.length) {
        return false;
    }
    return detail::strEqual(line1.begin, line1.length, line2.begin, line2.length);
}

inline bool operator!=(StringView line1, StringView line2)
{
    return !detail::strEqual(line1.begin, line1.length, line2.begin, line2.length);
}

inline bool operator<(StringView line1, StringView line2)
{
    return detail::strLess(line1.begin, line1.length, line2.begin, line2.length);
}

inline bool operator<=(StringView line1, StringView line2)
{
    return !detail::strGreater(line1.begin, line1.length, line2.begin, line2.length);
}

inline bool operator>(StringView line1, StringView line2)
{
    return detail::strGreater(line1.begin, line1.length, line2.begin, line2.length);
}

inline bool operator>=(StringView line1, StringView line2)
{
    return !detail::strLess(line1.begin, line1.length, line2.begin, line2.length);
}

inline bool operator==(StringView line1, std::string const& line2)
{
    return detail::strEqual(line1.begin, line1.length, line2.c_str(), line2.length());
}

inline bool operator!=(StringView line1, std::string const& line2)
{
    return !detail::strEqual(line1.begin, line1.length, line2.c_str(), line2.length());
}

inline bool operator<(StringView line1, std::string const& line2)
{
    return detail::strLess(line1.begin, line1.length, line2.c_str(), line2.length());
}

inline bool operator<=(StringView line1, std::string const& line2)
{
    return !detail::strGreater(line1.begin, line1.length, line2.c_str(), line2.length());
}

inline bool operator>(StringView line1, std::string const& line2)
{
    return detail::strGreater(line1.begin, line1.length, line2.c_str(), line2.length());
}

inline bool operator>=(StringView line1, std::string const& line2)
{
    return !detail::strLess(line1.begin, line1.length, line2.c_str(), line2.length());
}

inline bool operator==(std::string const& line1, StringView line2)
{
    return detail::strEqual(line1.c_str(), line1.length(), line2.begin, line2.length);
}

inline bool operator!=(std::string const& line1, StringView line2)
{
    return !detail::strEqual(line1.c_str(), line1.length(), line2.begin, line2.length);
}

inline bool operator<(std::string const& line1, StringView line2)
{
    return detail::strLess(line1.c_str(), line1.length(), line2.begin, line2.length);
}

inline bool operator<=(std::string const& line1, StringView line2)
{
    return !detail::strGreater(line1.c_str(), line1.length(), line2.begin, line2.length);
}

inline bool operator>(std::string const& line1, StringView line2)
{
    return detail::strGreater(line1.c_str(), line1.length(), line2.begin, line2.length);
}

inline bool operator>=(std::string const& line1, StringView line2)
{
    return !detail::strLess(line1.c_str(), line1.length(), line2.begin, line2.length);
}
