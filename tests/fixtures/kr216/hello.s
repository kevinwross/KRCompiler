; kr216 M2 fixture: exit with status 42.
.text
.global _start
_start:
    ld r2, #42
    sys
