#include <kr232/input.h>

#define KEYBOARD_NEXT (*(volatile unsigned*)0xFFF06000u)
#define KEYBOARD_COUNT (*(volatile unsigned*)0xFFF06004u)

unsigned input_next(void) { return KEYBOARD_NEXT; }

unsigned input_pending(void) { return KEYBOARD_COUNT; }
