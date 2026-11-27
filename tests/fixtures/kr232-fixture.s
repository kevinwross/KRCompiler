; kr232-fixture: the two-instruction container the KDBG rows read. It pushes R2
; and asks the emulator to exit with status 0, so the executed rows have a real
; container to decode, link and run.
;
; The rows assert that the container's debug data carries exactly two line
; records and one function, so the program is exactly two instructions.
.globl main
.text
main:
  push r2
  sys
