; ADR 0062 fixture: an integer address 128 bytes beyond the signed byte.
; `ld r2, [r3+128]` assembles only once the assembler falls back to F0 mode 6,
; and the row runs the linked image so the encoding is proven on the machine.
.globl main
.text
main:
  ld r3, #value
  sub r3, #128          ; base = value - 128, so [base+128] is the word
  ld r2, [r3+128]       ; the form this ADR adds
  ret                   ; crt0 exits with R2 as the status
.data
value:
  .word 42
