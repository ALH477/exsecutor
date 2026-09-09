; tests/unit/clean_syscalls.asm
;
; Positive fixture: every syscall site here resolves to a number in
; CLAUDE.md's closed allowlist -- read(0) write(1) close(3) fstat(5)
; lseek(8) mmap(9) munmap(11) openat(257) exit_group(231) -- and between
; them the two rax-resolving idioms tools/syscall-audit.sh trusts are both
; exercised: an immediate `mov` and the `xor reg,reg` self-zeroing form.
;
; The read() call is zero-length (rdx=0) so it returns immediately without
; blocking on stdin, whatever tests/run.sh connects it to.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	; read(0, msg, 0) -- resolves via the xor-zero idiom (rax=0 -> read).
	xor	eax,eax
	xor	edi,edi
	lea	rsi,[msg]
	xor	edx,edx
	syscall

	; write(1, msg, msg.len) -- resolves via immediate mov (rax=1 -> write).
	mov	eax,1
	mov	edi,1
	lea	rsi,[msg]
	mov	edx,msg.len
	syscall

	; exit_group(0) -- rax=231, not exit(60). See tests/unit/smoke.asm's
	; header comment for why that distinction is the point of this fixture.
	mov	eax,231
	xor	edi,edi
	syscall

segment readable writeable
  msg db 'exsecutor: audit-clean fixture', 10
  .len = $ - msg
