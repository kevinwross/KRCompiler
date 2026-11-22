#ifndef KRC_STDARG_H
#define KRC_STDARG_H

typedef char *va_list;

char *__builtin_va_start(void);

#define va_start(ap, last) ((ap) = __builtin_va_start())
#define va_end(ap) ((void)0)
#ifdef __kr16__
/* KR16 passes a variadic argument per two-byte stack slot (ADR 0029). */
#define va_arg(ap, type) (*(type *)(((ap) += 2) - 2))
#elif defined(__kr216__)
/* KR216 passes each vararg in its natural size, two-byte aligned. */
#define va_arg(ap, type) (*(type *)(((ap) += sizeof(type)) - sizeof(type)))
#else
#define va_arg(ap, type) (*(type *)(((ap) += 8) - 8))
#endif
#define va_copy(dest, src) ((dest) = (src))

#endif
