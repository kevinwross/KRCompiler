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
