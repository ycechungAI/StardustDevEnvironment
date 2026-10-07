// Force-included into bots built with MSVC_COMPAT (see bots/StardustBots.cmake). Bots written on Windows use the
// Microsoft "secure" C runtime functions; these map them to the standard ones, and file paths with backslashes are
// converted so "bwapi-data\\AI\\x.txt" works here.
#pragma once

#ifdef __cplusplus

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cfloat>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <numeric>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <strings.h>
#include <thread>

namespace stardust_compat
{
    inline std::string path(const char *windowsPath)
    {
        std::string result(windowsPath);
        std::replace(result.begin(), result.end(), '\\', '/');
        return result;
    }
}

inline FILE *stardust_compat_fopen(const char *path, const char *mode)
{
    return ::fopen(stardust_compat::path(path).c_str(), mode);
}

inline int fopen_s(FILE **file, const char *path, const char *mode)
{
    *file = stardust_compat_fopen(path, mode);
    return *file ? 0 : errno;
}

template<class... Args>
inline int sprintf_s(char *buffer, size_t size, const char *format, Args... args)
{
    return std::snprintf(buffer, size, format, args...);
}

template<size_t N, class... Args>
inline int sprintf_s(char (&buffer)[N], const char *format, Args... args)
{
    return std::snprintf(buffer, N, format, args...);
}

inline int vsprintf_s(char *buffer, size_t size, const char *format, va_list args)
{
    return std::vsnprintf(buffer, size, format, args);
}

#ifndef _TRUNCATE
#define _TRUNCATE ((size_t) -1)
#endif

inline int vsnprintf_s(char *buffer, size_t size, size_t count, const char *format, va_list args)
{
    size_t limit = count == _TRUNCATE ? size : std::min(size, count + 1);
    return std::vsnprintf(buffer, limit, format, args);
}

template<size_t N>
inline int vsnprintf_s(char (&buffer)[N], size_t count, const char *format, va_list args)
{
    return vsnprintf_s(buffer, N, count, format, args);
}

template<class... Args>
inline int _snprintf_s(char *buffer, size_t size, size_t count, const char *format, Args... args)
{
    size_t limit = count == _TRUNCATE ? size : std::min(size, count + 1);
    return std::snprintf(buffer, limit, format, args...);
}

inline int strcpy_s(char *destination, size_t size, const char *source)
{
    if (!destination || !source || size == 0) return EINVAL;
    std::snprintf(destination, size, "%s", source);
    return 0;
}

template<size_t N>
inline int strcpy_s(char (&destination)[N], const char *source)
{
    return strcpy_s(destination, N, source);
}

inline int strcat_s(char *destination, size_t size, const char *source)
{
    size_t length = std::strlen(destination);
    if (length >= size) return EINVAL;
    std::snprintf(destination + length, size - length, "%s", source);
    return 0;
}

inline int gmtime_s(struct tm *result, const time_t *time)
{
    return gmtime_r(time, result) ? 0 : errno;
}

inline int localtime_s(struct tm *result, const time_t *time)
{
    return localtime_r(time, result) ? 0 : errno;
}

inline int _stricmp(const char *a, const char *b) { return strcasecmp(a, b); }
inline int _strnicmp(const char *a, const char *b, size_t n) { return strncasecmp(a, b, n); }

// Plain fopen gets the same path conversion. (The standard headers that use std::fopen are included above, so they
// aren't affected.)
#define fopen stardust_compat_fopen

// (The _s scanf variants take a buffer size after each %s, %c and %[ argument, so they can't be mapped generically;
// those call sites are patched per bot.)

#endif
