#include <kr232/fb.h>
#include <kr232/gfx.h>

int main(void) {
    gfx_set_colour(KR232_COLOR(255, 0, 0));
    gfx_fill_rect(0, 0, 8, 8);
    fb_flip();
    return 0;
}
