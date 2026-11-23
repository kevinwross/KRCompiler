/* The kernel-mode drawing library behind <kr232/gfx.h>. The back surface lives at
   aperture offset 0x001E0000 (devices.h fb_back_) and a flip presents it. */
#include <kr232/fb.h>
#include <kr232/gfx.h>

#define FB_PAGE ((volatile unsigned*)0xFFF04000u)
#define APERTURE ((volatile unsigned char*)0xFC000000u)
#define BACK_OFFSET 0x001E0000u
#define FLIP_REGISTER (*(volatile unsigned*)0xFFF04020u)

static unsigned current_colour;

/* +0 is CONTROL: the mode words start at +4 (kr232-devices.md §3). */
unsigned fb_width(void) { return FB_PAGE[1]; }
unsigned fb_height(void) { return FB_PAGE[2]; }
unsigned fb_stride(void) { return FB_PAGE[3]; }
void fb_flip(void) { FLIP_REGISTER = 1u; }

void gfx_set_colour(unsigned colour) { current_colour = colour; }

static void put(int x, int y) {
    volatile unsigned* row = (volatile unsigned*)(APERTURE + BACK_OFFSET);
    const unsigned stride = fb_stride() / 4u;
    row[(unsigned)y * stride + (unsigned)x] = current_colour;
}

void gfx_clear(void) {
    const unsigned w = fb_width();
    const unsigned h = fb_height();
    for (unsigned y = 0; y < h; ++y) {
        for (unsigned x = 0; x < w; ++x) {
            put((int)x, (int)y);
        }
    }
}

void gfx_fill_rect(int x, int y, int width, int height) {
    for (int row = 0; row < height; ++row) {
        for (int col = 0; col < width; ++col) {
            put(x + col, y + row);
        }
    }
}
