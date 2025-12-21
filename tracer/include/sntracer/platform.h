#pragma once

// Compiler
#if defined(__clang__)
    #define SN_COMPILER_CLANG
#elif defined(__GNUC__)
    #define SN_COMPILER_GCC
#elif defined(_MSC_VER)
    #define SN_COMPILER_MSVC
#else
    #error "Don't know whether works on this compiler!"
#endif

// Operating system
#if defined(__gnu_linux__) || defined(__linux__)
    #define SN_OS_LINUX
#elif defined(_WIN64)
    #define SN_OS_WINDOWS
#elif defined(macintosh) || defined(Macintosh) || (defined(__APPLE__) && defined(__MACH__))
    #define SN_OS_MAC
#else
    #error "Don't know whether works on this OS!"
#endif

