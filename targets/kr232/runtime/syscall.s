; The user-mode side of the kernel interface. The C ABI passes the first four
; arguments in R2-R5, so this stub moves them into the registers the kernel
; reads and leaves the result in R2. R8-R10 are callee-saved in this ABI and the
; trap saves only {EFP, PC, flags, SP}, so the stub preserves them itself.
.globl __kr232_syscall
.text
__kr232_syscall:
  push r8
  push r9
  push r10
  mov r7, r2             ; call number
  mov r2, r3             ; argument 0
  mov r3, r4             ; argument 1
  mov r4, r5             ; argument 2
  syscall
  pop r10
  pop r9
  pop r8
  ret