#ifndef ZSTD_DEPS_COMMON
#define ZSTD_DEPS_COMMON

#include <linux/limits.h>
#include <linux/stddef.h>

#define ZSTD_memcpy(d,s,n) __builtin_memcpy((d),(s),(n))
#define ZSTD_memmove(d,s,n) __builtin_memmove((d),(s),(n))
#define ZSTD_memset(d,s,n) __builtin_memset((d),(s),(n))

#endif

#ifdef ZSTD_DEPS_NEED_MALLOC
#ifndef ZSTD_DEPS_MALLOC
#define ZSTD_DEPS_MALLOC

#define ZSTD_malloc(s) ({ (void)(s); NULL; })
#define ZSTD_free(p) ((void)(p))
#define ZSTD_calloc(n,s) ({ (void)(n); (void)(s); NULL; })

#endif
#endif

#ifdef ZSTD_DEPS_NEED_MATH64
#ifndef ZSTD_DEPS_MATH64
#define ZSTD_DEPS_MATH64

#include <linux/math64.h>

static uint64_t ZSTD_div64(uint64_t dividend, uint32_t divisor) {
  return div_u64(dividend, divisor);
}

#endif
#endif

#ifdef ZSTD_DEPS_NEED_ASSERT
#ifndef ZSTD_DEPS_ASSERT
#define ZSTD_DEPS_ASSERT

#include <linux/kernel.h>

#define assert(x) WARN_ON(!(x))

#endif
#endif

#ifdef ZSTD_DEPS_NEED_IO
#ifndef ZSTD_DEPS_IO
#define ZSTD_DEPS_IO

#include <linux/printk.h>

#define ZSTD_DEBUG_PRINT(...) pr_debug(__VA_ARGS__)

#endif
#endif
