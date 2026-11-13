; KR32 bootstrap runtime: process entry and the minimum console services.
; SYSCALL #32 exits with R0; SYSCALL #33 writes R0 as a character.
        .text
        .global _start
        .extern main
        .extern __stack_limit
        .weak __krc_run_atexit
_start:
        ; Tile saves use the transform stack, which grows down from the stack
        ; limit into the 64 KiB between the program stack and the heap. TSP is
        ; zero at reset, and MPUSH faults below 64, so it must be set before any
        ; matrix code runs.
        LEA R1, [__stack_limit]
        WRSP R1
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
