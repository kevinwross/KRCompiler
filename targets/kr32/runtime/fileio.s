; KR32 host-service wrappers. Arguments follow the C ABI in R0..R3 and the
; host result is returned in R0.
        .text

        .global getchar
getchar:
        SYSCALL #34
        RET

        .global open
open:
        SYSCALL #35
        RET

        .global close
close:
        SYSCALL #36
        RET

        .global read
read:
        SYSCALL #37
        RET

        .global write
write:
        SYSCALL #38
        RET

        .global seek
seek:
        SYSCALL #39
        RET

        .global sys_time
sys_time:
        SYSCALL #40
        RET

        .global sys_argc
sys_argc:
        SYSCALL #42
        RET

        .global sys_arg
sys_arg:
        SYSCALL #43
        RET
