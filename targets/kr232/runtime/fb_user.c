/* The user-mode side of <kr232/fb.h>. A user program cannot read the framebuffer
   registers, so the mode arrives through the kernel once and is cached, and a
   flip is one SYSCALL. */
extern int __kr232_syscall6(int number, ...);

#define KR232_CALL_FB_INFO 22
#define KR232_CALL_FLIP 17

static unsigned cached_width;
static unsigned cached_height;
static unsigned cached_stride;
static int fetched;

static void fetch_mode(void) {
    unsigned block[3];
    block[0] = 0;
    block[1] = 0;
    block[2] = 0;
    (void)__kr232_syscall6(KR232_CALL_FB_INFO, (int)block, 0, 0, 0, 0);
    cached_width = block[0];
    cached_height = block[1];
    cached_stride = block[2];
    fetched = 1;
}

unsigned fb_width(void) {
    if (fetched == 0) {
        fetch_mode();
    }
    return cached_width;
}

unsigned fb_height(void) {
    if (fetched == 0) {
        fetch_mode();
    }
    return cached_height;
}

unsigned fb_stride(void) {
    if (fetched == 0) {
        fetch_mode();
    }
    return cached_stride;
}

void fb_flip(void) {
    (void)__kr232_syscall6(KR232_CALL_FLIP, 0, 0, 0, 0, 0);
}
