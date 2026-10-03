; selftest probe: socket(AF_INET, SOCK_STREAM) and connect to 127.0.0.1:PORT
; (PORT given with `fasmg -i 'PORT = n'`). Exit 0 if connected, 200 if
; socket() failed, else connect's errno.
include 'format/format.inc'
format ELF64 executable 3
entry start
segment readable executable
start:
	mov	eax,41
	mov	edi,2
	mov	esi,1
	xor	edx,edx
	syscall
	test	rax,rax
	js	nosock
	mov	rdi,rax
	mov	eax,42
	lea	rsi,[sa]
	mov	edx,16
	syscall
	test	rax,rax
	js	failed
	mov	eax,231
	xor	edi,edi
	syscall
nosock:
	mov	eax,231
	mov	edi,200
	syscall
failed:
	mov	rdi,rax
	neg	rdi
	mov	eax,231
	syscall
segment readable
sa	dw 2
	db (PORT shr 8) and 0xFF, PORT and 0xFF
	db 127, 0, 0, 1
	dq 0
