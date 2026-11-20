; `.bss` is allocated and zero-filled at load. This program reads a word the
; source only reserved -- it never wrote one -- and exits 42 when it is zero.
.text
.global _start
_start:
    LD R0, [zero_word]
    CMP R0, #0
    BNE .Lbss_not_zero
    MOV R0, #42
    HLT
.Lbss_not_zero:
    MOV R0, #1
    HLT

.bss
.align 2
zero_word:
    .space 2
