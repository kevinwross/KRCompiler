; KR32 host-service syscall stubs. The C runtime wraps these to map negative
; host results to -1 and errno. Arguments follow the C ABI in R0..R3.
        .text

        .global krc_sys_getchar
krc_sys_getchar:
        SYSCALL #34
        RET

        .global krc_sys_open
krc_sys_open:
        SYSCALL #35
        RET

        .global krc_sys_close
krc_sys_close:
        SYSCALL #36
        RET

        .global krc_sys_read
krc_sys_read:
        SYSCALL #37
        RET

        .global krc_sys_write
krc_sys_write:
        SYSCALL #38
        RET

        .global krc_sys_seek
krc_sys_seek:
        SYSCALL #39
        RET

        .global krc_sys_time
krc_sys_time:
        SYSCALL #40
        RET

        .global krc_sys_argc
krc_sys_argc:
        SYSCALL #42
        RET

        .global krc_sys_arg
krc_sys_arg:
        SYSCALL #43
        RET

        .global krc_sys_unlink
krc_sys_unlink:
        SYSCALL #52
        RET

        .global krc_sys_rename
krc_sys_rename:
        SYSCALL #53
        RET

        .global krc_sys_stat
krc_sys_stat:
        SYSCALL #54
        RET

        .global krc_sys_truncate
krc_sys_truncate:
        SYSCALL #55
        RET

        .global krc_sys_getcwd
krc_sys_getcwd:
        SYSCALL #56
        RET

        .global krc_sys_chdir
krc_sys_chdir:
        SYSCALL #57
        RET

        .global krc_sys_dup
krc_sys_dup:
        SYSCALL #58
        RET

        .global krc_sys_dup2
krc_sys_dup2:
        SYSCALL #59
        RET

; The host's native open-file dialog. Narrower than the C ABI: the host takes
; a kind in R0, a buffer in R1, its size in R2, and the result returns in R0.
        .global sys_open_dialog
sys_open_dialog:
        MOV R3, R1
        MOV R2, R0
        MOV R1, #1
        MOV R0, #0
        SYSCALL #51
        RET
