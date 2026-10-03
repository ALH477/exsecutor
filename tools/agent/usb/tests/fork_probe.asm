; selftest probe: fork(57) up to 64 times; each child sleeps. Exit status =
; the number of forks that succeeded. Needs --no-seccomp (fork and nanosleep
; are not on the allowlist); measures RLIMIT_NPROC.
include 'format/format.inc'
format ELF64 executable 3
entry start
segment readable executable
start:
	xor	r12d,r12d
	mov	r13d,64
again:
	mov	eax,57
	syscall
	test	rax,rax
	js	done
	jz	child
	inc	r12d
	dec	r13d
	jnz	again
done:
	mov	edi,r12d
	mov	eax,231
	syscall
child:
	mov	eax,35
	lea	rdi,[ts]
	xor	esi,esi
	syscall
	jmp	child
segment readable
ts	dq 30, 0
