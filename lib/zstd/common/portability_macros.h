#ifndef ZSTD_PORTABILITY_MACROS_H
#define ZSTD_PORTABILITY_MACROS_H

#ifndef __has_attribute
  #define __has_attribute(x) 0
#endif

#ifndef __has_builtin
#  define __has_builtin(x) 0
#endif

#ifndef __has_feature
#  define __has_feature(x) 0
#endif

#ifdef __ELF__
# define ZSTD_HIDE_ASM_FUNCTION(func) .hidden func
#else
# define ZSTD_HIDE_ASM_FUNCTION(func)
#endif

#ifndef DYNAMIC_BMI2
  #if ((defined(__clang__) && __has_attribute(__target__)) \
      || (defined(__GNUC__) \
          && (__GNUC__ >= 11))) \
      && (defined(__x86_64__) || defined(_M_X64)) \
      && !defined(__BMI2__)
  #  define DYNAMIC_BMI2 1
  #else
  #  define DYNAMIC_BMI2 0
  #endif
#endif

#define ZSTD_ASM_SUPPORTED 1

#define ZSTD_ENABLE_ASM_X86_64_BMI2 0

#endif
