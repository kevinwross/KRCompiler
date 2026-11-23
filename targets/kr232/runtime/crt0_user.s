; A user-mode startup: the kernel enters `main` with K = 0, where the kernel-only
; hypercall `sys` would fault, so the status goes back through `SYSCALL` call 0.
; The kernel-mode crt0.s stays as it is for programs that run without a kernel.
.globl _start
.text
_start:
  call main
  ld r7, #0
  syscall
