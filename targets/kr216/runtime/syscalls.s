; kr216 host-service trampolines. The call number is in R7, arguments in R2-R5.
.text
.global __krc_write
__krc_write:
    push fp
    mov fp, sp
    ld r7, #1
    sys
    pop fp
    ret

.global __krc_exit
__krc_exit:
    push fp
    mov fp, sp
    ld r7, #0
    sys
    pop fp
    ret

; SYS 5, the text display (kr216-display.md). R2 = command, R3 = argument block,
; R4 = block length; the result or status comes back in R2.
.global __krc_display
__krc_display:
    push fp
    mov fp, sp
    ld r7, #5
    sys
    pop fp
    ret

