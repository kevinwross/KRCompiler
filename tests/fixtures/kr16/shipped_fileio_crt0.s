.text
.extern main
.global _start
_start:
    MOV R0, #main
    CALL R0
    HLT
