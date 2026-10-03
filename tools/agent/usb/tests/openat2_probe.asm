; selftest probe: openat2(AT_FDCWD, "/etc/passwd", {O_RDONLY}, 24), the one
; open syscall the seccomp filter admits. Exit 0 if it opened, else errno.
include 'format/format.inc'
format ELF64 executable 3
entry start
segment readable executable
start:
	mov	eax,437
	mov	rdi,-100
	lea	rsi,[path]
	lea	rdx,[how]
	mov	r10d,24
	syscall
	test	rax,rax
	js	failed
	mov	eax,231
	xor	edi,edi
	syscall
failed:
	mov	rdi,rax
	neg	rdi
	mov	eax,231
	syscall
segment readable
path	db '/etc/passwd',0
how	dq 0, 0, 0
