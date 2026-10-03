; selftest probe: openat(AT_FDCWD, "/etc/passwd", O_RDONLY). Exit 0 if it
; opened, else errno. openat(257) is NOT on the seccomp allowlist.
include 'format/format.inc'
format ELF64 executable 3
entry start
segment readable executable
start:
	mov	eax,257
	mov	rdi,-100
	lea	rsi,[path]
	xor	edx,edx
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
