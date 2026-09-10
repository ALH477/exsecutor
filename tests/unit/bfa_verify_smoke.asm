; tests/unit/bfa_verify_smoke.asm
; TEST: run=yes expect-exit=0 audit=skip
include 'format/format.inc'
format ELF64 executable 3
entry start
include '../../compiler/x86_64/backend_fasmg/verify.inc'
segment readable executable
  start:
	mov	eax, 231
	xor	edi, edi
	syscall
