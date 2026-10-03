; selftest probe: nanosleep(12 s), then exit 0. Burns no CPU, so only a
; WALL-CLOCK timeout stops it. nanosleep(35) is not on the seccomp
; allowlist: with the filter on this dies by SIGSYS before sleeping.
include 'format/format.inc'
format ELF64 executable 3
entry start
segment readable executable
start:
	mov	eax,35
	lea	rdi,[ts]
	xor	esi,esi
	syscall
	mov	eax,231
	xor	edi,edi
	syscall
segment readable
ts	dq 12, 0
