#ifndef PLATFORM_H
#define PLATFORM_H

// Cross-platform defines, must appear before system headers.

#if defined(_MSC_VER)
    #define _USE_MATH_DEFINES
    #define _CRT_SECURE_NO_WARNINGS
#endif

#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#if defined(_WIN32)
    #include <direct.h>
    #include <io.h>
    #ifndef F_OK
        #define F_OK 0
    #endif
    #define ACCESS     _access
    #define STRDUP     _strdup
    #define STRTOK     strtok_s
    #define STRCASECMP _stricmp
    #define SSCANF     sscanf_s
#else
    #include <unistd.h>
    #define ACCESS     access
    #define STRDUP     strdup
    #define STRTOK     strtok_r
    #define STRCASECMP strcasecmp
    #define SSCANF     sscanf
#endif

#endif // PLATFORM_H
