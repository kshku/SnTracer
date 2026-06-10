#include <sncore/api_common.h>

#if defined(SN_TRACER_STATIC)
    #define SN_API
#elif defined(SN_EXPORT)
    #define SN_API SN_API_HELPER_EXPORT
#else
    #define SN_API SN_API_HELPER_IMPORT
#endif

