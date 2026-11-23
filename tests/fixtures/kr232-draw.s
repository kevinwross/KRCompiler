; kr232 M2 fixture: one pixel into the back surface, a flip, then a clean exit.
.globl main
.text
main:
  ld r2, #0xFC1E0000
  ld r3, #0x00112233
  st [r2], r3
  ld r4, #0xFFF04020
  ld r5, #1
  st [r4], r5
  ld r2, #0
  sys
