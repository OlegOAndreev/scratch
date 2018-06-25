#pragma once

#include <cstring>
#include <string>

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

inline int compareStr(char const* begin1, size_t length1, char const* begin2, size_t length2)
{
#if defined(COUNT_STRING_COMPARES)
    compareStrCount++;
#endif
    int ret = memcmp(begin1, begin2, std::min(length1, length2));
    if (ret != 0) {
        return ret;
    } else {
        // This is implementation-defined casting size_t -> int.
        return length1 - length2;
    }
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
