#ifndef KR232_CONS_H
#define KR232_CONS_H

/* The screen console (kr232-devices.md section 7): byte output and byte input on the
   host text display. Kernel mode writes the terminal page directly; kernel-mode
   input is host-mediated and read through the C runtime's getchar, because no device
   register carries it. User mode reaches both directions through SYSCALL. The console
   page (section 4) is the debug serial stream and is not this package. */
void cons_putc(int ch);
void cons_puts(const char* text);
int cons_getc(void);

#endif
