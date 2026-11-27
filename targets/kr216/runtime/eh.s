; kr216 exception runtime: throw, resume and the FP-chain walk (ADR 0055).
;
; A `.cpp_except` descriptor is
;   u16 magic 0xE216, function_start, function_end, frame_size, region_count,
;   then region_count * {start_pc, end_pc, landing_pad}.
; All addresses are absolute and patched by the linker.
        .text
        .extern __krc_exit
        .extern __krc_eh_start
        .extern __krc_eh_end

; void *__cxa_throw(void *object, void *typeinfo, void *destructor)
; Naked: R13 is the throwing function's frame and [SP] is its return address.
        .global __cxa_throw
__cxa_throw:
        LD R6, #__krc_current_object
        ST [R6], R2
        LD R6, #__krc_current_type
        ST [R6], R3
        MOV R6, R2
        SUB R6, #6
        ST [R6], R4              ; exception header: destructor
        LD R9, [SP]
        MOV R8, R13
        JMP __krc_eh_walk

; void _Unwind_Resume(void *exception)
; Continue at the resume call site: a cleanup pad that resumes into itself is
; not a descriptor region, so the lookup steps past it to the caller, while a
; pad that resumes toward a catch in the same frame finds that region.
        .global _Unwind_Resume
_Unwind_Resume:
        LD R9, [SP]
        MOV R8, R13
        JMP __krc_eh_walk

; Walk state: R8 = frame pointer, R9 = pc in that frame.
__krc_eh_walk:
        MOV R2, R9
        CALL __krc_eh_lookup
        CMP R2, #0
        BNE __krc_eh_found
        LD R6, [R8+2]          ; return address in the caller
        LD R8, [R8]            ; saved caller FP
        CMP R8, #0
        BEQ __krc_eh_terminate
        MOV R9, R6
        JMP __krc_eh_walk

__krc_eh_found:
        MOV R6, R2             ; landing pad
        MOV R13, R8
        MOV R15, R13
        SUB R15, R3            ; SP = FP - frame_size
        LD R2, #__krc_current_object
        LD R2, [R2]
        LD R3, #__krc_current_type
        LD R3, [R3]
        JMP R6

__krc_eh_terminate:
        LD R2, #134
        CALL __krc_exit
        HALT

; void *__krc_eh_lookup(u16 pc)
; Returns the landing pad in R2 and the owning frame size in R3, or 0 in R2.
        .global __krc_eh_lookup
__krc_eh_lookup:
        LD R4, #__krc_eh_start
        LD R5, #__krc_eh_end
__krc_eh_block_loop:
        CMP R4, R5
        BCS __krc_eh_no_block
        LD R6, [R4+2]          ; function_start
        CMP R2, R6
        BCC __krc_eh_block_next
        LD R6, [R4+4]          ; function_end: the epilogue's LEAVE, inclusive
        CMP R2, R6
        BHI __krc_eh_block_next
        LD R3, [R4+6]          ; frame_size
        LD R7, [R4+8]          ; region_count
        ADD R4, #10
__krc_eh_region_loop:
        CMP R7, #0
        BEQ __krc_eh_no_region
        LD R6, [R4]
        CMP R2, R6
        BCC __krc_eh_region_next
        LD R6, [R4+2]
        CMP R2, R6
        BHI __krc_eh_region_next
        LD R2, [R4+4]
        RET
__krc_eh_region_next:
        ADD R4, #6
        SUB R7, #1
        JMP __krc_eh_region_loop
__krc_eh_no_region:
        LD R2, #0
        RET
__krc_eh_block_next:
        LD R6, [R4+8]
        ADD R4, #10
__krc_eh_skip_regions:
        CMP R6, #0
        BEQ __krc_eh_block_loop
        ADD R4, #6
        SUB R6, #1
        JMP __krc_eh_skip_regions
__krc_eh_no_block:
        LD R2, #0
        RET

        .data
        .align 2
        .global __krc_current_object
__krc_current_object:
        .word 0
        .global __krc_current_type
__krc_current_type:
        .word 0
