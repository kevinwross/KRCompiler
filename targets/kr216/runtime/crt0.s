; kr216 startup: run .init_array, call main, run .fini_array and exit.
.text
.global _start
.extern main
.extern __init_array_start
.extern __init_array_end
.extern __fini_array_start
.extern __fini_array_end
_start:
    ld r8, #__init_array_start
    ld r9, #__init_array_end
init_loop:
    cmp r8, r9
    bcs init_done
    ld r7, [r8]
    call r7
    add r8, #2
    bra init_loop
init_done:
    ld r2, #0
    ld r3, #0
    call main
    push r2
    ld r8, #__fini_array_start
    ld r9, #__fini_array_end
fini_loop:
    cmp r8, r9
    bcs fini_done
    ld r7, [r8]
    call r7
    add r8, #2
    bra fini_loop
fini_done:
    pop r2
    ld r7, #0
    sys
