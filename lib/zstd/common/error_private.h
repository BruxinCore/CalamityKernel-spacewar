#ifndef ERROR_H_MODULE
#define ERROR_H_MODULE

#include <linux/zstd_errors.h>
#include "compiler.h"
#include "debug.h"
#include "zstd_deps.h"

#define ERR_STATIC static __attribute__((unused))

typedef ZSTD_ErrorCode ERR_enum;
#define PREFIX(name) ZSTD_error_##name

#undef ERROR
#define ERROR(name) ZSTD_ERROR(name)
#define ZSTD_ERROR(name) ((size_t)-PREFIX(name))

ERR_STATIC unsigned ERR_isError(size_t code) { return (code > ERROR(maxCode)); }

ERR_STATIC ERR_enum ERR_getErrorCode(size_t code) { if (!ERR_isError(code)) return (ERR_enum)0; return (ERR_enum) (0-code); }

#define CHECK_V_F(e, f) size_t const e = f; if (ERR_isError(e)) return e
#define CHECK_F(f)   { CHECK_V_F(_var_err__, f); }

const char* ERR_getErrorString(ERR_enum code);

ERR_STATIC const char* ERR_getErrorName(size_t code)
{
    return ERR_getErrorString(ERR_getErrorCode(code));
}

static INLINE_KEYWORD UNUSED_ATTR
void _force_has_format_string(const char *format, ...) {
  (void)format;
}

#define _FORCE_HAS_FORMAT_STRING(...) \
  if (0) { \
    _force_has_format_string(__VA_ARGS__); \
  }

#define ERR_QUOTE(str) #str

#define RETURN_ERROR_IF(cond, err, ...) \
  if (cond) { \
    RAWLOG(3, "%s:%d: ERROR!: check %s failed, returning %s", \
           __FILE__, __LINE__, ERR_QUOTE(cond), ERR_QUOTE(ERROR(err))); \
    _FORCE_HAS_FORMAT_STRING(__VA_ARGS__); \
    RAWLOG(3, ": " __VA_ARGS__); \
    RAWLOG(3, "\n"); \
    return ERROR(err); \
  }

#define RETURN_ERROR(err, ...) \
  do { \
    RAWLOG(3, "%s:%d: ERROR!: unconditional check failed, returning %s", \
           __FILE__, __LINE__, ERR_QUOTE(ERROR(err))); \
    _FORCE_HAS_FORMAT_STRING(__VA_ARGS__); \
    RAWLOG(3, ": " __VA_ARGS__); \
    RAWLOG(3, "\n"); \
    return ERROR(err); \
  } while(0);

#define FORWARD_IF_ERROR(err, ...) \
  do { \
    size_t const err_code = (err); \
    if (ERR_isError(err_code)) { \
      RAWLOG(3, "%s:%d: ERROR!: forwarding error in %s: %s", \
             __FILE__, __LINE__, ERR_QUOTE(err), ERR_getErrorName(err_code)); \
      _FORCE_HAS_FORMAT_STRING(__VA_ARGS__); \
      RAWLOG(3, ": " __VA_ARGS__); \
      RAWLOG(3, "\n"); \
      return err_code; \
    } \
  } while(0);

#endif
