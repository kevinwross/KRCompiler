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

/* One pass, one stride read: put() asked the framebuffer page for the stride on
   every pixel, so a 120x120 rectangle cost five million instructions instead of
   ninety thousand. */
static void fill(int x, int y, int width, int height) {
    volatile unsigned* row = (volatile unsigned*)(APERTURE + BACK_OFFSET);
    const unsigned stride = fb_stride() / 4u;
    for (int r = 0; r < height; ++r) {
        for (int c = 0; c < width; ++c) {
            row[(unsigned)(y + r) * stride + (unsigned)(x + c)] = current_colour;
        }
    }
}

void gfx_clear(void) {
    fill(0, 0, (int)fb_width(), (int)fb_height());
}

void gfx_fill_rect(int x, int y, int width, int height) {
    fill(x, y, width, height);
}
/* The command block form: the page takes COMMAND, then its arguments, then a
   doorbell write; STATUS bit 0 is ERROR. */
#define GFX ((volatile unsigned*)0xFFF09000u)
#define GFX_COMMAND(index, value) (GFX[index] = (unsigned)(value))

static int submit(unsigned command) {
    GFX_COMMAND(0, command);
    GFX_COMMAND(9, 1);          /* offset 36: the doorbell */
    return (int)GFX[10];        /* offset 40: STATUS */
}

int gfx_copy_rect(int x, int y, int width, int height) {
    GFX_COMMAND(1, 0);          /* the surface origin */
    GFX_COMMAND(2, x);
    GFX_COMMAND(3, y);
    GFX_COMMAND(4, width);
    GFX_COMMAND(5, height);
    return submit(4);
}

int gfx_draw_line(int x0, int y0, int x1, int y1) {
    GFX_COMMAND(1, x0);
    GFX_COMMAND(2, y0);
    GFX_COMMAND(3, x1);
    GFX_COMMAND(4, y1);
    GFX_COMMAND(5, current_colour);
    return submit(5);
}

int gfx_draw_text(int x, int y, const char* text, int length) {
    GFX_COMMAND(1, x);
    GFX_COMMAND(2, y);
    GFX_COMMAND(3, (unsigned)text);
    GFX_COMMAND(4, length);
    GFX_COMMAND(5, current_colour);
    GFX_COMMAND(6, 0);
    return submit(6);
}

int gfx_upload(const void* pixels, int width, int height, int stride) {
    GFX_COMMAND(1, (unsigned)pixels);
    GFX_COMMAND(2, 0);          /* surface offset */
    GFX_COMMAND(3, width);
    GFX_COMMAND(4, height);
    GFX_COMMAND(5, stride);
    return submit(10);
}