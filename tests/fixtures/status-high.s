; Returns a status above 2^31 from main, so crt0's SYS exit carries the whole
; word: a POSIX process status would keep only its low eight bits.
.globl main
.text
main:
  ld r2, #0x80000005
  ret
