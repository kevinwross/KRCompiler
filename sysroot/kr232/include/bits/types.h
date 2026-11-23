#ifndef KRC_BITS_TYPES_H
#define KRC_BITS_TYPES_H

/* kr232's pointer width is 32 bits, so size_t and ptrdiff_t are the 32-bit
   integer types. wchar_t is signed, four bytes, as the ABI's profile says. */
typedef unsigned int size_t;
typedef int ptrdiff_t;
typedef int wchar_t;

#endif
