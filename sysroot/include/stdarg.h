#ifndef KRC_STDARG_H
#define KRC_STDARG_H

typedef char *va_list;

char *__builtin_va_start(void);

#define va_start(ap, last) ((ap) = __builtin_va_start())
#define va_end(ap) ((void)0)
#define va_arg(ap, type) (*(type *)(((ap) += 8) - 8))
#define va_copy(dest, src) ((dest) = (src))

#endif
