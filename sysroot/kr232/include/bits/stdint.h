#ifndef KRC_BITS_STDINT_H
#define KRC_BITS_STDINT_H

/* kr232: 32-bit registers and a 32-bit address space (kr232-isa.md ?1). long is
   32 bits, so int32_t is long and the pointer-sized types are 32 bits. */
typedef signed char int8_t;
typedef unsigned char uint8_t;
typedef short int16_t;
typedef unsigned short uint16_t;
typedef int int32_t;
typedef unsigned int uint32_t;

typedef int intptr_t;
typedef unsigned int uintptr_t;
typedef int intmax_t;
typedef unsigned int uintmax_t;

#endif
