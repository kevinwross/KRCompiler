/* The control for volatile_pointer_arithmetic.c: the same address form through a
 * pointer that is not volatile must stay unmarked. Without this, a rule that marked
 * every dereference of a pointer expression would pass the other test while
 * suppressing optimisation of ordinary code. */

int read_second(int *base) {
    return *(base + 1);
}
