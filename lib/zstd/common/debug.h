#ifndef DEBUG_H_12987983217
#define DEBUG_H_12987983217

#define DEBUG_STATIC_ASSERT(c) (void)sizeof(char[(c) ? 1 : -1])

#ifndef DEBUGLEVEL
#  define DEBUGLEVEL 0
#endif

#if (DEBUGLEVEL>=1)
#  define ZSTD_DEPS_NEED_ASSERT
#  include "zstd_deps.h"
#else
#  ifndef assert
#    define assert(condition) ((void)0)
#  endif
#endif

#if (DEBUGLEVEL>=2)
#  define ZSTD_DEPS_NEED_IO
#  include "zstd_deps.h"
extern int g_debuglevel;

#  define RAWLOG(l, ...) {                                       \
                if (l<=g_debuglevel) {                           \
                    ZSTD_DEBUG_PRINT(__VA_ARGS__);               \
            }   }
#  define DEBUGLOG(l, ...) {                                     \
                if (l<=g_debuglevel) {                           \
                    ZSTD_DEBUG_PRINT(__FILE__ ": " __VA_ARGS__); \
                    ZSTD_DEBUG_PRINT(" \n");                     \
            }   }
#else
#  define RAWLOG(l, ...)      {}
#  define DEBUGLOG(l, ...)    {}
#endif

#endif
