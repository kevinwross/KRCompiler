#ifndef KR232_CONS_H
#define KR232_CONS_H

/* The console (kr232-devices.md section 2): byte output and byte input. Kernel
   mode reaches the page directly; the kernel will wrap these as SYSCALL. */
void cons_putc(int ch);
void cons_puts(const char* text);
int cons_getc(void);

#endif
