; KR32 exception runtime: throw, resume, and the frame walk (ADR 0026).
;
; The `.cpp_except` table describes every function's frame size and its
; protected call sites. A descriptor is:
;   u32 magic, func_start, func_end, frame_size, region_count,
;   region[] { start_pc, end_pc, landing_pad }
; All words are absolute addresses patched by the linker.
;
; This file is deliberately self-contained: the walk and the table scan are
; assembly so that an image which never throws needs no C runtime, and a
; program that does throw links the rest of the `__cxa_*` entry points from
; libc.c.
        .text
        .extern __krc_eh_start
        .extern __krc_eh_end

; void *__cxa_throw(void *object, void *typeinfo, void *destructor)
; Naked: it reads the return address from the stack and keeps the caller's R7,
; which is the throwing function's frame.
        .global __cxa_throw
__cxa_throw:
        SUB R3, R0, #16
        STORE R1, [R3]                 ; header: typeinfo
        STORE R2, [R3 + 4]             ; header: destructor
        LEA R4, [__krc_current_exception]
        LOAD R5, [R4]
        STORE R5, [R3 + 8]             ; header: previous exception
        STORE R3, [R4]
        LOAD R9, [SP]                  ; return address in the throwing function
        MOV R8, R7                     ; and its frame
        JMP __krc_eh_walk

; void _Unwind_Resume(void *exception)
; Continues the walk from a landing pad that did not handle the exception.
        .global _Unwind_Resume
_Unwind_Resume:
        LOAD R9, [SP]
        MOV R8, R7
        JMP __krc_eh_walk

; Walk state: R8 = frame, R9 = program counter in that frame.
__krc_eh_walk:
        MOV R0, R9
        CALL __krc_eh_lookup
        CMP R0, #0
        JNZ __krc_eh_found
        ; No handler here: step to the caller.
        LEA R4, [__krc_eh_frame_size]
        LOAD R4, [R4]
        ADD R4, R8, R4                 ; saved R7 sits at frame + size
        LOAD R9, [R4 + 4]              ; return address in the caller
        LOAD R8, [R4]
        CMP R8, #0
        JZ __krc_eh_terminate
        JMP __krc_eh_walk

__krc_eh_found:
        MOV R9, R0                     ; pad
        ; Enter the pad in the frame that owns it: R7 names that frame and SP
        ; is the frame base, which is what the body sees at a call site.
        MOV R7, R8
        SETSP R7
        LEA R4, [__krc_current_exception]
        LOAD R0, [R4]
        MOV R1, R0
        LOAD R1, [R1]                  ; typeinfo
        ADD R0, R0, #16                ; the thrown object
        JMP R9

__krc_eh_terminate:
        MOV R0, #134                   ; uncaught exception
        SYSCALL #32
        HALT

; void *__krc_eh_lookup(u32 pc)
; Returns the landing pad covering pc, or 0. Publishes the matching
; descriptor's frame size in __krc_eh_frame_size, and zero when no descriptor
; covers pc so the walk stops instead of reading an unknown frame layout.
; Clobbers R0-R6; the walk keeps its state in R8/R9.
        .global __krc_eh_lookup
__krc_eh_lookup:
        LEA R2, [__krc_eh_start]
        LEA R3, [__krc_eh_end]
__krc_eh_block_loop:
        CMP R2, R3
        JGE __krc_eh_no_block
        LOAD R5, [R2 + 4]              ; func_start
        CMP R0, R5
        JLT __krc_eh_block_next
        LOAD R6, [R2 + 8]              ; func_end
        CMP R0, R6
        JGE __krc_eh_block_next
        ; Found the function: publish its frame size.
        LOAD R6, [R2 + 12]
        STORE R6, [__krc_eh_frame_size]
        LOAD R5, [R2 + 16]             ; region_count
        ADD R2, R2, #20
__krc_eh_region_loop:
        CMP R5, #0
        JZ __krc_eh_no_region
        LOAD R6, [R2]                  ; start_pc
        CMP R0, R6
        JLT __krc_eh_region_next
        LOAD R6, [R2 + 4]              ; end_pc
        CMP R0, R6
        JGT __krc_eh_region_next
        LOAD R0, [R2 + 8]              ; landing pad
        RET
__krc_eh_region_next:
        ADD R2, R2, #12
        SUB R5, R5, #1
        JMP __krc_eh_region_loop
__krc_eh_no_region:
        MOV R0, #0
        RET
__krc_eh_block_next:
        LOAD R6, [R2 + 16]             ; skip 20 + 12 * region_count
        MUL R6, R6, #12
        ADD R6, R6, #20
        ADD R2, R2, R6
        JMP __krc_eh_block_loop
__krc_eh_no_block:
        STORE R0, [__krc_eh_frame_size] ; zero: the walk terminates
        MOV R0, #0
        RET

        .data
        .align 4
        .global __krc_current_exception
__krc_current_exception:
        .word 0
        .align 4
        .global __krc_eh_frame_size
__krc_eh_frame_size:
        .word 0
