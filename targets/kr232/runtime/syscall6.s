; The user-mode side of the kernel interface for calls that carry more than the
; four arguments a C call puts in registers. A variadic declaration makes the
; caller push every argument in an eight-byte slot (ADR 0057), so the stub reads
; them from its own frame and fills R2-R6 before the trap. R8-R10 are callee-saved
; and the trap does not preserve them, so the stub saves them itself.
.globl __kr232_syscall6
.text
__kr232_syscall6:
  enter #0, #0
  push r8
  push r9
  push r10
  ld r7, [r13+8]          ; call number
  ld r2, [r13+16]         ; argument 0
  ld r3, [r13+24]         ; argument 1
  ld r4, [r13+32]         ; argument 2
  ld r5, [r13+40]         ; argument 3
  ld r6, [r13+48]         ; argument 4
  syscall
  pop r10
  pop r9
  pop r8
  leave #0
  ret
