#include <kr232/cons.h>
#include <kr232/fb.h>
#include <kr232/gfx.h>

/* A program a debugger can watch: it says one byte on the console, draws one
   rectangle and flips, so both device views have something to show. */
int main(void) {
    cons_putc(65);
    gfx_set_colour(KR232_COLOR(0, 0, 255));
    gfx_fill_rect(0, 0, 4, 4);
    fb_flip();
    return 7;
}
