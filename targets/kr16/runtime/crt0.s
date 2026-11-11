.text
.extern main
.global _start
_start:
    MOV R0, #main
    CALL R0
    HLT

.global puts
puts:
    MOV R2, #0xF000
.Lputs_loop:
    LDB R0, [R1]
    CMP R0, #0
    BEQ .Lputs_done
    STB [R2], R0
    ADD R1, #1
    BRA .Lputs_loop
.Lputs_done:
    MOV R0, #10
    STB [R2], R0
    MOV R0, #0
    RET
