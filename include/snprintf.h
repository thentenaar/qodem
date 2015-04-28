#ifndef _PORTABLE_SNPRINTF_H_
#define _PORTABLE_SNPRINTF_H_

#include <stdarg.h>
#include <stddef.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define PORTABLE_SNPRINTF_VERSION_MAJOR 2
#define PORTABLE_SNPRINTF_VERSION_MINOR 2

extern int portable_snprintf(char *str, size_t str_m, const char *fmt, /*args*/ ...);
extern int portable_vsnprintf(char *str, size_t str_m, const char *fmt, va_list ap);
#define snprintf  portable_snprintf
#define vsnprintf portable_vsnprintf

extern int asprintf  (char **ptr, const char *fmt, /*args*/ ...);
extern int vasprintf (char **ptr, const char *fmt, va_list ap);
extern int asnprintf (char **ptr, size_t str_m, const char *fmt, /*args*/ ...);
extern int vasnprintf(char **ptr, size_t str_m, const char *fmt, va_list ap);

#ifdef  __cplusplus
}
#endif

#endif
