/* The user-mode side of <kr232/gfx.h>: a drawing call becomes one command block
   through the kernel, which is what kr232-devices.md ?11.1 promises. */
extern int __kr232_syscall6(int number, ...);

#define KR232_CALL_FILL_RECT 16
#define KR232_CLEAR_EXTENT 4096

static unsigned current_colour;

void gfx_set_colour(unsigned colour) {
    current_colour = colour;
}

void gfx_fill_rect(int x, int y, int width, int height) {
    (void)__kr232_syscall6(KR232_CALL_FILL_RECT, x, y, width, height, (int)current_colour);
}

void gfx_clear(void) {
    /* The device clips to the target, so an over-large rectangle is the whole
       surface without asking for its size. */
    gfx_fill_rect(0, 0, KR232_CLEAR_EXTENT, KR232_CLEAR_EXTENT);
}
