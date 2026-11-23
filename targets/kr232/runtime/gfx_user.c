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
/* Each of these builds the command block the kernel forwards, so the call
   carries one pointer instead of six arguments. The returned STATUS is the
   device's own answer. */
#define KR232_CALL_COPY 18
#define KR232_CALL_LINE 19
#define KR232_CALL_TEXT 20
#define KR232_CALL_UPLOAD 21

int gfx_copy_rect(int x, int y, int width, int height) {
    unsigned block[5];
    block[0] = 0;               /* the surface origin */
    block[1] = (unsigned)x;
    block[2] = (unsigned)y;
    block[3] = (unsigned)width;
    block[4] = (unsigned)height;
    return __kr232_syscall6(KR232_CALL_COPY, (int)block, 0, 0, 0, 0);
}

int gfx_draw_line(int x0, int y0, int x1, int y1) {
    unsigned block[5];
    block[0] = (unsigned)x0;
    block[1] = (unsigned)y0;
    block[2] = (unsigned)x1;
    block[3] = (unsigned)y1;
    block[4] = current_colour;
    return __kr232_syscall6(KR232_CALL_LINE, (int)block, 0, 0, 0, 0);
}

int gfx_draw_text(int x, int y, const char* text, int length) {
    unsigned block[6];
    block[0] = (unsigned)x;
    block[1] = (unsigned)y;
    block[2] = (unsigned)text;
    block[3] = (unsigned)length;
    block[4] = current_colour;
    block[5] = 0;
    return __kr232_syscall6(KR232_CALL_TEXT, (int)block, 0, 0, 0, 0);
}

int gfx_upload(const void* pixels, int width, int height, int stride) {
    unsigned block[5];
    block[0] = (unsigned)pixels;
    block[1] = 0;
    block[2] = (unsigned)width;
    block[3] = (unsigned)height;
    block[4] = (unsigned)stride;
    return __kr232_syscall6(KR232_CALL_UPLOAD, (int)block, 0, 0, 0, 0);
}