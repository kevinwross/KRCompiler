/*
 * Link evidence for <krc/display.h> and the kr216 stdio runtime: every display wrapper is
 * called and the stdio input and PRNG entry points are referenced, so the linked image has a
 * definition for each one. The file is also a runnable check - it returns 42 when the calls
 * answer as the ABI promises - which the kr216 repository's guest rows execute under the
 * emulator; this row only links it.
 */

#include <stdio.h>
#include <stdlib.h>

#include <krc/display.h>

int main(void) {
    uint32_t foreground = 0;
    uint32_t background = 0;
    int row = 0;
    int column = 0;
    int buttons = 0;
    char line[8];

    if (display_clear() != DISPLAY_OK) return 1;
    if (display_rows() != 30) return 2;
    if (display_cols() != 80) return 3;
    if (display_set_color(0x11223344UL, 0x55667788UL) != DISPLAY_OK) return 4;
    if (display_get_color(&foreground, &background) != DISPLAY_OK) return 5;
    if (foreground != 0x11223344UL || background != 0x55667788UL) return 6;
    if (display_putc('A') != DISPLAY_OK) return 7;
    if (display_puts("hello") != DISPLAY_OK) return 8;
    if (display_cursor(1, 2, 1) != DISPLAY_OK) return 9;
    if (display_get_key() != DISPLAY_NOT_READY) return 10;
    if (display_get_click(&row, &column, &buttons) != DISPLAY_NOT_READY) return 11;
    if (display_read_line(line, (int)sizeof line) != DISPLAY_NOT_READY) return 12;

    /* stdio and the PRNG: the runtime defines these beside the display wrappers. The branch is
       unreachable while the input source is open, so this fixture stays a link check. */
    srand(1);
    if (display_get_key() == DISPLAY_END_OF_INPUT) {
        if (rand() == getchar()) return 13;
    }
    return 42;
}
