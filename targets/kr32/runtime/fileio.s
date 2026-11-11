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

        .global unlink
unlink:
        SYSCALL #52
        RET

        .global rename
rename:
        SYSCALL #53
        RET

        .global stat
stat:
        SYSCALL #54
        RET

        .global truncate
truncate:
        SYSCALL #55
        RET

        .global getcwd
getcwd:
        SYSCALL #56
        RET

        .global chdir
chdir:
        SYSCALL #57
        RET

        .global dup
dup:
        SYSCALL #58
        RET

        .global dup2
dup2:
        SYSCALL #59
        RET
