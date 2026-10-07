// A stand-in for <windows.h> for bots built with MSVC_COMPAT (see bots/StardustBots.cmake): just the types and
// functions the opponent bots use, implemented with the standard library.
#pragma once

#include <chrono>
#include <cstdint>
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
