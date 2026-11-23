/* A program written against the packages, not against call numbers: it prints
   through <kr232/cons.h>, draws through <kr232/gfx.h>, flips and reads the mode
   through <kr232/fb.h>, and asks <kr232/input.h> whether anything is queued.
   The mode belongs to the emulator, so the check is that the call answered with
   a real width rather than with a fixed one. */
#include <kr232/cons.h>
#include <kr232/fb.h>
#include <kr232/gfx.h>
#include <kr232/input.h>

int main(void) {
    cons_puts("packages\n");
    gfx_set_colour(KR232_COLOR(0, 255, 0));
    gfx_fill_rect(0, 0, 8, 8);
    fb_flip();
    if (fb_width() == 0) {
        return 3;
    }
    if (fb_height() == 0) {
        return 4;
    }
    if (input_pending() != 0) {
        return 5;
    }
    return 0;
}
