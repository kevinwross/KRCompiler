; The display hypercall trampoline for kr232: int __krc_display(command, block,
; length). The C ABI already puts the three arguments in R2/R3/R4 and takes the
; result from R2, so only the call number (R7) has to be loaded.
.globl __krc_display
.text
__krc_display:
  ld r7, #5
  sys
  ret
