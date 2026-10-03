; selftest probe: writes 512 x 4096 bytes ('A') to stdout, then exits 0.
include 'format/format.inc'
format ELF64 executable 3
entry start
segment readable executable
start:
	mov	r12d,512
again:
	mov	eax,1
	mov	edi,1
	lea	rsi,[buf]
	mov	edx,4096
	syscall
	dec	r12d
	jnz	again
	mov	eax,231
	xor	edi,edi
	syscall
segment readable
buf	db 4096 dup 'A'
