; KR32 bootstrap runtime: process entry and the minimum console services.
; SYSCALL #32 exits with R0; SYSCALL #33 writes R0 as a character.
        .text
        .global _start
        .extern main
        .weak __krc_run_atexit
_start:
        CALL main
        PUSH R0
        CALL __krc_run_atexit
        POP R0
        HALT

        .global putchar
putchar:
        PUSH R0
        SYSCALL #33
        POP R0
        RET

        .global exit
exit:
        PUSH R0
        CALL __krc_run_atexit
        POP R0
        SYSCALL #32
        HALT

; Weak default for programs linked without the C runtime.
__krc_run_atexit:
        RET
