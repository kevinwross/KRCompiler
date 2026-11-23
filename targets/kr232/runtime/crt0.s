; kr232 C runtime entry. The ABI passes a function's first four arguments in
; R2-R5 and its result in R2, so the status `main` returns is already in R2 when
; it comes back. `sys` with R7 = 0 exits with that status. No stack setup is
; needed: reset SP is 0x08000000.
.globl _start
.text
_start:
  call main
  ld r7, #0
  sys
