; KR32 bootstrap runtime: process entry and the minimum console services.
; SYSCALL #32 exits with R0; SYSCALL #33 writes R0 as a character.
        .text
        .global _start
        .extern main
_start:
        CALL main
        HALT

        .global putchar
putchar:
        PUSH R0
        SYSCALL #33
        POP R0
        RET

        .global exit
exit:
        SYSCALL #32
        HALT
