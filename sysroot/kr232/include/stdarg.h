#pragma once

/* Variable arguments on the register targets (ADR 0057, ADR 0059).
 *
 * Every argument to a variadic function arrives in an eight-byte stack slot,
 * the first at [FP+8] and the named argument at index i at [FP+8+8i]. The
 * `vastart` IR operation answers the address of the first variable slot, and
 * `va_arg` is pointer arithmetic over the slots: an `int` is read from the low
 * four bytes of its slot and a `double` from all eight, which is why a mixed
 * call needs no type tag.
 *
 * `va_list` is a plain pointer, so copying one with `=` copies the position and
 * `va_end` has nothing to release.
 */
typedef char* va_list;

#define va_start(ap, last_named) ((ap) = __builtin_va_start())
#define va_arg(ap, type) (*(type*)((ap += 8) - 8))
#define va_end(ap) ((void)0)
