; kr216 startup: call main with argc/argv zero and exit with its return value.
.text
.global _start
.extern main
_start:
    ld r2, #0
    ld r3, #0
    call main
    ld r7, #0
    sys
