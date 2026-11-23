/* The sysroot's widths must agree with the profile the compiler holds: kr232 has
   32-bit registers, a 32-bit pointer and a 32-bit long (kr232-isa.md section 1). */
#include <stdint.h>
#include <stddef.h>

_Static_assert(sizeof(int8_t) == 1, "int8_t is 8 bits");
_Static_assert(sizeof(int16_t) == 2, "int16_t is 16 bits");
_Static_assert(sizeof(int32_t) == 4, "int32_t is 32 bits");
_Static_assert(sizeof(intptr_t) == 4, "intptr_t is the pointer width");
_Static_assert(sizeof(intmax_t) == 4, "intmax_t is 32 bits on kr232");
_Static_assert(sizeof(size_t) == 4, "size_t is the pointer width");

int main(void) { return 0; }
