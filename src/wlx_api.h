#pragma once

#include <windows.h>

#define LISTPLUGIN_OK 0
#define LISTPLUGIN_ERROR 1

#define LC_COPY 1
#define LC_NEWPARAMS 2
#define LC_SELECTALL 3
#define LC_SETPERCENT 4

#define LCS_FIND_FIRST 1
#define LCS_MATCH_CASE 2
#define LCS_WHOLE_WORDS 4
#define LCS_BACKWARDS 8

struct ListDefaultParamStruct {
    int size;
    DWORD pluginInterfaceVersionLow;
    DWORD pluginInterfaceVersionHi;
    char defaultIniName[MAX_PATH];
};

#if defined(_WIN32)
#define WLX_CALL __stdcall
#define WLX_EXPORT extern "C" __declspec(dllexport)
#endif
