// A stand-in for <windows.h> for bots built with MSVC_COMPAT (see bots/StardustBots.cmake): just the types and
// functions the opponent bots use, implemented with the standard library.
#pragma once

#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <thread>

typedef int BOOL;
typedef unsigned long DWORD;
typedef void *HANDLE;
typedef void *LPVOID;
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#ifndef MAX_PATH
#define MAX_PATH 260
#endif
#define APIENTRY
#define WINAPI

typedef union _LARGE_INTEGER
{
    struct
    {
        DWORD LowPart;
        long HighPart;
    };
    long long QuadPart;
} LARGE_INTEGER;

inline BOOL QueryPerformanceCounter(LARGE_INTEGER *count)
{
    count->QuadPart = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    return TRUE;
}

inline BOOL QueryPerformanceFrequency(LARGE_INTEGER *frequency)
{
    frequency->QuadPart = 1000000000LL;
    return TRUE;
}

inline DWORD GetTickCount()
{
    return (DWORD) std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
}

inline void Sleep(DWORD milliseconds)
{
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

// There is no game window to look at here: no window is ever in the foreground
typedef void *HWND;

inline HWND GetForegroundWindow()
{
    return nullptr;
}

inline int GetWindowText(HWND, char *text, int size)
{
    if (size > 0) text[0] = '\0';
    return 0;
}

typedef const char *LPCSTR;
#define INVALID_HANDLE_VALUE ((HANDLE) (intptr_t) -1)
#define PAGE_READWRITE 0x04
#define FILE_MAP_ALL_ACCESS 0xF001F

// No other process shares memory with a bot here: opening or creating a mapping fails, as if it were unavailable
inline HANDLE OpenFileMapping(DWORD, BOOL, LPCSTR)
{
    return nullptr;
}
inline HANDLE CreateFileMapping(HANDLE, void *, DWORD, DWORD, DWORD, LPCSTR)
{
    return nullptr;
}
inline LPVOID MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, size_t)
{
    return nullptr;
}
inline BOOL UnmapViewOfFile(const void *)
{
    return FALSE;
}
inline BOOL CloseHandle(HANDLE)
{
    return TRUE;
}

inline BOOL CreateDirectory(LPCSTR path, void *)
{
    std::error_code error;
    return std::filesystem::create_directory(path, error) ? TRUE : FALSE;
}

// Structured exceptions are MSVC-only: there are none to translate into C++ exceptions
typedef struct _CONTEXT
{
    void *Eip;
} CONTEXT;
typedef struct _EXCEPTION_POINTERS
{
    CONTEXT *ContextRecord;
} EXCEPTION_POINTERS, *PEXCEPTION_POINTERS;
typedef void (*_se_translator_function)(unsigned int, EXCEPTION_POINTERS *);
inline _se_translator_function _set_se_translator(_se_translator_function)
{
    return nullptr;
}

#define MAXINT INT_MAX

template<class... Args>
inline int wsprintf(char *buffer, const char *format, Args... args)
{
    return std::sprintf(buffer, format, args...);
}
