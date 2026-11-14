; KR16 host-service wrappers.
;
; The VM takes the service number as the SYS immediate and reads the service
; block address from R0. A block is sixteen bytes: the status the host writes at
; offset 0, three result words at 2, 4, and 6, and four argument words at 8, 10,
; 12, and 14. Every word is sixteen bits.
;
; A wrapper returns the service's first result on success and the negated status
; on failure, so a caller can distinguish a value of zero from an error. Status
; zero is success; the file services report errors 1..13.
;
; Arguments follow the KR16 scalar ABI: the first three words travel in R1-R3 and
; the result returns in R0. Every wrapper therefore stays inside three argument
; words, which is why krc16_sys_seek is absolute only: a relative seek is the
; caller's own arithmetic against the stream position.

        .text

; unsigned krc16_sys_abi_version();
        .global krc16_sys_abi_version
krc16_sys_abi_version:
        PUSH R4
        MOV R4, SP
        LEA SP, [R4 - 16]
        LEA R0, [R4 - 16]
        SYS #0
        LD R0, [R4 - 14]
        MOV SP, R4
        POP R4
        RET

; unsigned krc16_sys_capabilities();
        .global krc16_sys_capabilities
krc16_sys_capabilities:
        PUSH R4
        MOV R4, SP
        LEA SP, [R4 - 16]
        LEA R0, [R4 - 16]
        SYS #0
        LD R0, [R4 - 12]
        MOV SP, R4
        POP R4
        RET

; int krc16_sys_open(const char* path, unsigned flags);
        .global krc16_sys_open
krc16_sys_open:
        PUSH R4
        MOV R4, SP
        LEA SP, [R4 - 16]
        ST [R4 - 8], R1
        ST [R4 - 6], R2
        LEA R0, [R4 - 16]
        SYS #1
        LD R1, [R4 - 16]
        CMP R1, #0
        BNE .Lkrc16_open_fail
        LD R0, [R4 - 14]
        MOV SP, R4
        POP R4
        RET
.Lkrc16_open_fail:
        MOV R0, #0
        SUB R0, R1
        MOV SP, R4
        POP R4
        RET

; int krc16_sys_close(int fd);
        .global krc16_sys_close
krc16_sys_close:
        PUSH R4
        MOV R4, SP
        LEA SP, [R4 - 16]
        ST [R4 - 8], R1
        LEA R0, [R4 - 16]
        SYS #2
        LD R0, [R4 - 16]
        CMP R0, #0
        BEQ .Lkrc16_close_done
        MOV R1, #0
        SUB R1, R0
        MOV R0, R1
.Lkrc16_close_done:
        MOV SP, R4
        POP R4
        RET

; int krc16_sys_read(int fd, void* buffer, unsigned count);
        .global krc16_sys_read
krc16_sys_read:
        PUSH R4
        MOV R4, SP
        LEA SP, [R4 - 16]
        ST [R4 - 8], R1
        ST [R4 - 6], R2
        ST [R4 - 4], R3
        LEA R0, [R4 - 16]
        SYS #3
        LD R1, [R4 - 16]
        CMP R1, #0
        BNE .Lkrc16_read_fail
        LD R0, [R4 - 14]
        MOV SP, R4
        POP R4
        RET
.Lkrc16_read_fail:
        MOV R0, #0
        SUB R0, R1
        MOV SP, R4
        POP R4
        RET

; int krc16_sys_write(int fd, const void* buffer, unsigned count);
        .global krc16_sys_write
krc16_sys_write:
        PUSH R4
        MOV R4, SP
        LEA SP, [R4 - 16]
        ST [R4 - 8], R1
        ST [R4 - 6], R2
        ST [R4 - 4], R3
        LEA R0, [R4 - 16]
        SYS #4
        LD R1, [R4 - 16]
        CMP R1, #0
        BNE .Lkrc16_write_fail
        LD R0, [R4 - 14]
        MOV SP, R4
        POP R4
        RET
.Lkrc16_write_fail:
        MOV R0, #0
        SUB R0, R1
        MOV SP, R4
        POP R4
        RET

; long krc16_sys_seek(int fd, long offset);
; The offset travels as a low and a high word, and the position returns as two
; words, so a caller declares a 32-bit long.
        .global krc16_sys_seek
krc16_sys_seek:
        PUSH R4
        MOV R4, SP
        LEA SP, [R4 - 16]
        ST [R4 - 8], R1
        ST [R4 - 6], R2
        ST [R4 - 4], R3
        MOV R1, #0
        ST [R4 - 2], R1
        LEA R0, [R4 - 16]
        SYS #5
        LD R1, [R4 - 16]
        CMP R1, #0
        BNE .Lkrc16_seek_fail
        LD R0, [R4 - 14]
        MOV SP, R4
        POP R4
        RET
.Lkrc16_seek_fail:
        MOV R0, #0
        SUB R0, R1
        MOV SP, R4
        POP R4
        RET

; int krc16_sys_unlink(const char* path);
        .global krc16_sys_unlink
krc16_sys_unlink:
        PUSH R4
        MOV R4, SP
        LEA SP, [R4 - 16]
        ST [R4 - 8], R1
        LEA R0, [R4 - 16]
        SYS #7
        LD R0, [R4 - 16]
        CMP R0, #0
        BEQ .Lkrc16_unlink_done
        MOV R1, #0
        SUB R1, R0
        MOV R0, R1
.Lkrc16_unlink_done:
        MOV SP, R4
        POP R4
        RET

; int krc16_sys_rename(const char* from, const char* to);
        .global krc16_sys_rename
krc16_sys_rename:
        PUSH R4
        MOV R4, SP
        LEA SP, [R4 - 16]
        ST [R4 - 8], R1
        ST [R4 - 6], R2
        LEA R0, [R4 - 16]
        SYS #8
        LD R0, [R4 - 16]
        CMP R0, #0
        BEQ .Lkrc16_rename_done
        MOV R1, #0
        SUB R1, R0
        MOV R0, R1
.Lkrc16_rename_done:
        MOV SP, R4
        POP R4
        RET
