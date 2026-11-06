#ifndef ZSTD_COMPILER_H
#define ZSTD_COMPILER_H

#include "portability_macros.h"

#if !defined(ZSTD_NO_INLINE)
#if (defined(__GNUC__) && !defined(__STRICT_ANSI__)) || defined(__cplusplus) || defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#  define INLINE_KEYWORD inline
#else
#  define INLINE_KEYWORD
#endif

#define FORCE_INLINE_ATTR __attribute__((always_inline))

#else

#define INLINE_KEYWORD
#define FORCE_INLINE_ATTR

#endif

#define WIN_CDECL

#define FORCE_INLINE_TEMPLATE static INLINE_KEYWORD FORCE_INLINE_ATTR

#if !defined(__clang__) && defined(__GNUC__) && __GNUC__ >= 4 && __GNUC_MINOR__ >= 8 && __GNUC__ < 5
#  define HINT_INLINE static INLINE_KEYWORD
#else
#  define HINT_INLINE static INLINE_KEYWORD FORCE_INLINE_ATTR
#endif

#define UNUSED_ATTR __attribute__((unused))

#define FORCE_NOINLINE static __attribute__((__noinline__))

#define TARGET_ATTRIBUTE(target) __attribute__((__target__(target)))

#define BMI2_TARGET_ATTRIBUTE TARGET_ATTRIBUTE("lzcnt,bmi,bmi2")

#if ( (__GNUC__ >= 4) || ( (__GNUC__ == 3) && (__GNUC_MINOR__ >= 1) ) )
#  define PREFETCH_L1(ptr)  __builtin_prefetch((ptr), 0  , 3  )
#  define PREFETCH_L2(ptr)  __builtin_prefetch((ptr), 0  , 2  )
#elif defined(__aarch64__)
#  define PREFETCH_L1(ptr)  __asm__ __volatile__("prfm pldl1keep, %0" ::"Q"(*(ptr)))
#  define PREFETCH_L2(ptr)  __asm__ __volatile__("prfm pldl2keep, %0" ::"Q"(*(ptr)))
#else
#  define PREFETCH_L1(ptr) (void)(ptr)
#  define PREFETCH_L2(ptr) (void)(ptr)
#endif

#define CACHELINE_SIZE 64

#define PREFETCH_AREA(p, s)  {            \
    const char* const _ptr = (const char*)(p);  \
    size_t const _size = (size_t)(s);     \
    size_t _pos;                          \
    for (_pos=0; _pos<_size; _pos+=CACHELINE_SIZE) {  \
        PREFETCH_L2(_ptr + _pos);         \
    }                                     \
}

#if !defined(__INTEL_COMPILER) && !defined(__clang__) && defined(__GNUC__) && !defined(__LCC__)
#  if (__GNUC__ == 4 && __GNUC_MINOR__ > 3) || (__GNUC__ >= 5)
#    define DONT_VECTORIZE __attribute__((optimize("no-tree-vectorize")))
#  else
#    define DONT_VECTORIZE _Pragma("GCC optimize(\"no-tree-vectorize\")")
#  endif
#else
#  define DONT_VECTORIZE
#endif

#define LIKELY(x) (__builtin_expect((x), 1))
#define UNLIKELY(x) (__builtin_expect((x), 0))

#if __has_builtin(__builtin_unreachable) || (defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 5)))
#  define ZSTD_UNREACHABLE { assert(0), __builtin_unreachable(); }
#else
#  define ZSTD_UNREACHABLE { assert(0); }
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ > 201710L) && defined(__has_c_attribute)
# define ZSTD_HAS_C_ATTRIBUTE(x) __has_c_attribute(x)
#else
# define ZSTD_HAS_C_ATTRIBUTE(x) 0
#endif

#define ZSTD_HAS_CPP_ATTRIBUTE(x) 0

#define ZSTD_FALLTHROUGH fallthrough

#ifndef ZSTD_ALIGNOF

#  define ZSTD_ALIGNOF(T) __alignof(T)

#endif

#endif
