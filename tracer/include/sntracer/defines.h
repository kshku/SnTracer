#pragma once

#include "sntracer/platform.h"

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SN_INLINE static inline

#if defined(SN_COMPILER_MSVC)
    #define SN_FORCE_INLINE static __forceinline
#else
    #define SN_FORCE_INLINE static inline __attribute__((always_inline))
#endif

#define SN_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)

#define SN_ASSERT(x) assert(x)

#define SN_SHOULD_NOT_REACH_HERE (SN_ASSERT(false))

#define SN_UNUSED(x) (void)(x)

#define SN_ARRAY_LENGTH(arr) (sizeof(arr) / sizeof(arr[0]))

#define SN_MAX(a, b) ((a) > (b) ? (a) : (b))

#define SN_MIN(a, b) ((a) < (b) ? (a) : (b))

#define SN_CLAMP(x, min, max) ((x) < (min) ? (min) : (x) > (max) ? (max) : (x))

