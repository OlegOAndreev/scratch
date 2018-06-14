#pragma once

#include <cstring>
#include <string>

namespace {

inline int compareStr(char const* begin1, size_t length1, char const* begin2, size_t length2)
{
    int ret = memcmp(begin1, begin2, std::min(length1, length2));
    if (ret != 0) {
        return ret;
    } else {
        // This is implementation-defined casting size_t -> int.
        return length1 - length2;
    }
}

}

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

    StringView(char const* begin, size_t length)
        : begin(begin)
        , length(length)
    {
    }
};

inline bool operator<(StringView line1, StringView line2)
{
    return compareStr(line1.begin, line1.length, line2.begin, line2.length) < 0;
}

inline bool operator<=(StringView line1, StringView line2)
{
    return compareStr(line1.begin, line1.length, line2.begin, line2.length) <= 0;
}

inline bool operator>(StringView line1, StringView line2)
{
    return compareStr(line1.begin, line1.length, line2.begin, line2.length) > 0;
}

inline bool operator>=(StringView line1, StringView line2)
{
    return compareStr(line1.begin, line1.length, line2.begin, line2.length) >= 0;
}

inline bool operator<(StringView line1, std::string const& line2)
{
    return compareStr(line1.begin, line1.length, line2.c_str(), line2.length()) < 0;
}

inline bool operator<=(StringView line1, std::string const& line2)
{
    return compareStr(line1.begin, line1.length, line2.c_str(), line2.length()) <= 0;
}

inline bool operator>(StringView line1, std::string const& line2)
{
    return compareStr(line1.begin, line1.length, line2.c_str(), line2.length()) > 0;
}

inline bool operator>=(StringView line1, std::string const& line2)
{
    return compareStr(line1.begin, line1.length, line2.c_str(), line2.length()) >= 0;
}

inline bool operator<(std::string const& line1, StringView line2)
{
    return compareStr(line1.c_str(), line1.length(), line2.begin, line2.length) < 0;
}

inline bool operator<=(std::string const& line1, StringView line2)
{
    return compareStr(line1.c_str(), line1.length(), line2.begin, line2.length) <= 0;
}

inline bool operator>(std::string const& line1, StringView line2)
{
    return compareStr(line1.c_str(), line1.length(), line2.begin, line2.length) > 0;
}

inline bool operator>=(std::string const& line1, StringView line2)
{
    return compareStr(line1.c_str(), line1.length(), line2.begin, line2.length) >= 0;
}
