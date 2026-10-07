// A stand-in for MSVC's <direct.h> for bots built with MSVC_COMPAT (see bots/StardustBots.cmake)
#pragma once
#include <sys/stat.h>

inline int _mkdir(const char *path)
{
    return mkdir(path, 0755);
}
