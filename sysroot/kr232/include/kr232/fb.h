#ifndef KR232_FB_H
#define KR232_FB_H

/* The framebuffer registers (kr232-devices.md section 3). A program running in
   kernel mode reads the mode and flips the surfaces directly; the kernel will wrap
   these as SYSCALL once it exists. */
unsigned fb_width(void);
unsigned fb_height(void);
unsigned fb_stride(void);
void fb_flip(void);

#endif
