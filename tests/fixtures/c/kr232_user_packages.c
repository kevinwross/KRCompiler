/* A program written against the packages, not against call numbers: it prints
   through <kr232/cons.h>, draws through <kr232/gfx.h>, flips and reads the mode
   through <kr232/fb.h>, and asks <kr232/input.h> whether anything is queued.
   The mode belongs to the emulator, so the check is that the call answered with
   a real width rather than with a fixed one.

   Every command that can refuse is checked: the drawing calls must answer
   success and DRAW_TEXT must answer the documented refusal, because a device
   that draws nothing used to leave two goldens recorded against the void. */
#include <kr232/cons.h>
#include <kr232/fb.h>
#include <kr232/gfx.h>
#include <kr232/input.h>

static unsigned pixels[16];

int main(void) {
    unsigned index;

    cons_puts("packages\n");
    for (index = 0; index < 16; ++index) {
        pixels[index] = KR232_COLOR(0, 0, 255);
    }
    gfx_set_colour(KR232_COLOR(0, 255, 0));
    gfx_fill_rect(0, 0, 8, 8);
    gfx_set_colour(KR232_COLOR(255, 0, 0));
    if ((gfx_draw_line(0, 0, 15, 15) & 1) != 0) {
        return 6;
    }
    if ((gfx_copy_rect(0, 0, 8, 8) & 1) != 0) {
        return 7;
    }
    if ((gfx_upload(pixels, 4, 4, 16) & 1) != 0) {
        return 8;
    }
    if ((gfx_draw_text(0, 0, "packages", 8) & 1) == 0) {
        return 9;   /* the atlas is not implemented, so it must refuse */
    }
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
    /* One console byte reads back through the package, and the keyboard queue is
       empty because the host pushed nothing: NO_EVENT is the documented answer. */
    if (input_next() != KR232_NO_EVENT) {
        return 10;
    }
    /* The row queues the single byte 'z', so a mismatch names the call that failed. */
    if (cons_getc() != 'z') {
        return 11;
    }
    if (fb_stride() != fb_width() * 4) {
        return 12;
    }
    return 0;
}
