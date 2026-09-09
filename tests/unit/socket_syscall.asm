; tests/unit/socket_syscall.asm
;
; Negative fixture: deliberately contains a socket(2) call site (rax=41),
; one of the socket-family numbers tools/syscall-audit.sh must hard-fail
; on with a loud message, per CLAUDE.md: "No socket-family syscall, ever
; (§9.3). `make audit` fails the build if one appears." A negative test
; that proves the audit catches what it claims to catch is worth more
; than five positive ones.
;
; Deliberately NOT executed by tests/run.sh (`run=no`): the point is
; static detection by the audit tool, not exercising a live socket(2)
; call from the test harness itself.
;
; TEST: run=no audit=fail

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	; socket(AF_INET=2, SOCK_STREAM=1, 0) -- rax=41 is socket(2), exactly
	; what this audit exists to reject.
	mov	eax,41
	mov	edi,2
	mov	esi,1
	xor	edx,edx
	syscall

	mov	eax,231
	xor	edi,edi
	syscall
