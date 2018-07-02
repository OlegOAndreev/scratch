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

// Endianess will be used in compareStr when subtracting two ptrdiff_t.
#if !defined(COMMON_LITTLE_ENDIAN)
#error "Big endian not supported"
#endif

// Defines function ptrdiff_t load_ptrdiff(void const* p).
DEFINE_LOAD_STORE(ptrdiff_t, ptrdiff)

// Returns <0 if (begin1, length1) < (begin2, length2), 0 if (begin1, length1) == (begin2, length2), >0 otherwise.
// intptr_t is used, becaue it is 8-byte signed integer on 64-bit archs, unlike int.
inline intptr_t compareStr(char const* begin1, size_t length1, char const* begin2, size_t length2)
{
#if defined(COUNT_STRING_COMPARES)
    compareStrCount++;
#endif
//   // Optimize the common case: both strings have at least sizeof(ptrdiff_t) bytes.
//    if (length1 >= sizeof(intptr_t) && length2 >= sizeof(intptr_t)) {
//        intptr_t start1 = load_ptrdiff(begin1);
//        intptr_t start2 = load_ptrdiff(begin2);
//        if (start1 != start2) {
//            this is bad, remove at least one bit or do three compares
//            // This is a valid, because the first differing byte will be the most significant different byte
//            // (because we assumed little endian before and byteSwap to big endian now).
//            return byteSwap(start1) - byteSwap(start2);
//        }

        int ret = memcmp(begin1, begin2, std::min(length1, length2));
        if (ret != 0) {
            return ret;
        } else {
            // This is implementation-defined casting size_t -> ptrdiff_t.
            return length1 - length2;
        }
//    } else {
//        ...
//    }
}

} // namespace detail

inline bool operator==(StringView line1, StringView line2)
{
    if (line1.length != line2.length) {
        return false;
    }
    return detail::compareStr(line1.begin, line1.length, line2.begin, line2.length) == 0;
}

inline bool operator!=(StringView line1, StringView line2)
{
    return detail::compareStr(line1.begin, line1.length, line2.begin, line2.length) != 0;
}

inline bool operator<(StringView line1, StringView line2)
{
    return detail::compareStr(line1.begin, line1.length, line2.begin, line2.length) < 0;
}

inline bool operator<=(StringView line1, StringView line2)
{
    return detail::compareStr(line1.begin, line1.length, line2.begin, line2.length) <= 0;
}

inline bool operator>(StringView line1, StringView line2)
{
    return detail::compareStr(line1.begin, line1.length, line2.begin, line2.length) > 0;
}

inline bool operator>=(StringView line1, StringView line2)
{
    return detail::compareStr(line1.begin, line1.length, line2.begin, line2.length) >= 0;
}

inline bool operator==(StringView line1, std::string const& line2)
{
    if (line1.length != line2.length()) {
        return false;
    }
    return detail::compareStr(line1.begin, line1.length, line2.c_str(), line2.length()) == 0;
}

inline bool operator!=(StringView line1, std::string const& line2)
{
    return detail::compareStr(line1.begin, line1.length, line2.c_str(), line2.length()) != 0;
}

inline bool operator<(StringView line1, std::string const& line2)
{
    return detail::compareStr(line1.begin, line1.length, line2.c_str(), line2.length()) < 0;
}

inline bool operator<=(StringView line1, std::string const& line2)
{
    return detail::compareStr(line1.begin, line1.length, line2.c_str(), line2.length()) <= 0;
}

inline bool operator>(StringView line1, std::string const& line2)
{
    return detail::compareStr(line1.begin, line1.length, line2.c_str(), line2.length()) > 0;
}

inline bool operator>=(StringView line1, std::string const& line2)
{
    return detail::compareStr(line1.begin, line1.length, line2.c_str(), line2.length()) >= 0;
}

inline bool operator==(std::string const& line1, StringView line2)
{
    if (line1.length() != line2.length) {
        return false;
    }
    return detail::compareStr(line1.c_str(), line1.length(), line2.begin, line2.length) == 0;
}

inline bool operator!=(std::string const& line1, StringView line2)
{
    return detail::compareStr(line1.c_str(), line1.length(), line2.begin, line2.length) != 0;
}

inline bool operator<(std::string const& line1, StringView line2)
{
    return detail::compareStr(line1.c_str(), line1.length(), line2.begin, line2.length) < 0;
}

inline bool operator<=(std::string const& line1, StringView line2)
{
    return detail::compareStr(line1.c_str(), line1.length(), line2.begin, line2.length) <= 0;
}

inline bool operator>(std::string const& line1, StringView line2)
{
    return detail::compareStr(line1.c_str(), line1.length(), line2.begin, line2.length) > 0;
}

inline bool operator>=(std::string const& line1, StringView line2)
{
    return detail::compareStr(line1.c_str(), line1.length(), line2.begin, line2.length) >= 0;
}
