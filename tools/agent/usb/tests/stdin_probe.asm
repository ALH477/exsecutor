; selftest probe: read(0, buf, 1). Exit 0 on end of file (stdin is
; /dev/null), 1 if a byte arrived, 200 on error.
include 'format/format.inc'
format ELF64 executable 3
entry start
segment readable executable
start:
	xor	eax,eax
	xor	edi,edi
	lea	rsi,[buf]
	mov	edx,1
	syscall
	test	rax,rax
	js	failed
	mov	rdi,rax
	mov	eax,231
	syscall
failed:
	mov	eax,231
	mov	edi,200
	syscall
segment readable writeable
buf	rb 16
