; Host services for the C runtime, kernel mode. Standard input and output are the
; host text display (kr232-devices.md §7): SYS 1 writes characters to the terminal
; page and SYS 2 reads display input. The console page is the debug serial stream and
; nothing here touches it; a program that wants COM calls the console registers
; directly. A file call has no device behind it and answers -1.
;
; Register use matches the SYS convention: R2, R3, R4 carry the arguments and R2
; brings the answer back, which is the same order the C side passes them in.

.globl krc_sys_write
krc_sys_write:
  ld r7, #1              ; display write: R2 descriptor, R3 address, R4 count
  sys
  ret

.globl krc_sys_read
krc_sys_read:
  ld r7, #2              ; display read: the same three registers
  sys
  ret

; One byte of display input. The host answers 1 with the byte in sys_char, 0 at end
; of input, and -1 while the source is open and nothing has been typed yet; the
; library polls that case until the host delivers a byte or closes the source.
.globl krc_sys_getchar
krc_sys_getchar:
sys_char_poll:
  ld r7, #2
  ld r2, #0              ; the display input stream
  ld r3, #sys_char
  ld r4, #1
  sys
  cmp r2, #1
  beq sys_char_ready
  cmp r2, #0
  beq sys_char_eof
  bmi sys_char_poll
sys_char_eof:
  ld r2, #-1
  ret
sys_char_ready:
  ld r5, #sys_char
  ldbu r2, [r5]
  ret

.globl krc_sys_time
krc_sys_time:
  ld r7, #3              ; R2 = low word, R3 = high word; the profile has no 64-bit
  sys                        ; integer, so the low word is the answer
  ret

; No kr232 device is a file. Each of these answers -1, which the C library turns
; into a failed call rather than a missing symbol.
.globl krc_sys_open
krc_sys_open:
  ld r2, #-1
  ret

.globl krc_sys_close
krc_sys_close:
  ld r2, #-1
  ret

.globl krc_sys_seek
krc_sys_seek:
  ld r2, #-1
  ret

.globl krc_sys_unlink
krc_sys_unlink:
  ld r2, #-1
  ret

.globl krc_sys_argc
krc_sys_argc:
  ld r2, #0              ; no command line: nothing can pass one
  ret

.globl krc_sys_arg
krc_sys_arg:
  ld r2, #-1
  ret

; The C library ends a program with exit(status); kr232's SYS 0 is that call, with
; the status already in R2.
.globl exit
exit:
  ld r7, #0
  sys
  ret

; One character to the console: store it, then write that one byte. `puts` and the
; printf family funnel through here.
.globl putchar
putchar:
  ld r3, #sys_char
  stb [r3], r2
  ld r7, #1
  ld r2, #1
  ld r4, #1
  sys
  ret

.data
sys_char:
  .byte 0

; The exception runtime lives in kr32's eh.s; kr232 has no unwind tables, so the
; pointer the library references exists and nothing walks it.
.globl __krc_current_exception
__krc_current_exception:
  .word 0

.text
; Unwinding has no tables to walk on kr232: a program that throws would stop
; here rather than corrupt the stack.
.globl _Unwind_Resume
_Unwind_Resume:
  ld r2, #1
  ld r7, #0
  sys

.globl krc_sys_rename
krc_sys_rename:
  ld r2, #-1
  ret

.globl krc_sys_stat
krc_sys_stat:
  ld r2, #-1
  ret
.text
; The remaining host services have no kr232 device behind them, so each answers -1
; rather than leaving the library with a name no object defines.
.globl krc_sys_chdir
krc_sys_chdir:
  ld r2, #-1
  ret

.globl krc_sys_dup
krc_sys_dup:
  ld r2, #-1
  ret

.globl krc_sys_getcwd
krc_sys_getcwd:
  ld r2, #-1
  ret

.globl krc_sys_truncate
krc_sys_truncate:
  ld r2, #-1
  ret


.globl krc_sys_dup2
krc_sys_dup2:
  ld r2, #-1
  ret

.globl krc_sys_isatty
krc_sys_isatty:
  ld r2, #-1
  ret

.globl krc_sys_pipe
krc_sys_pipe:
  ld r2, #-1
  ret

.globl krc_sys_fcntl
krc_sys_fcntl:
  ld r2, #-1
  ret
