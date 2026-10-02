; prototypes/beneath/site.asm -- the three shapes an openat2(437) site can
; take in a binary, assembled so docs/design/archivum-beneath.md section 3
; can specify the audit against real objdump output rather than imagined
; output. Verification-only (prototypes/README.md); never run, never on the
; build closure. Assembled and disassembled by hand:
;   fasmg site.asm site.elf && objdump -d -M intel site.elf && readelf -lW site.elf
; Layout mirrors compiler/x86_64/prelude/README.md's program wrapper:
; code in `segment readable executable`, literals in `segment readable`,
; mutable state in `segment readable writeable`.

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	; site 1 -- ADMISSIBLE: rdx is a rip-relative lea onto a 24-byte
	; constant in a non-writable segment, r10 an immediate 24, rax 437.
	; rdi (the Directorium's descriptor) comes from memory: the audit
	; requires only that it is NOT an immediate.
	mov	edi,[dir_record+8]
	lea	rsi,[path]
	lea	rdx,[how_beneath]
	mov	r10d,24
	mov	eax,437
	syscall

	; site 2 -- INDETERMINATE: rdx copied from another register.
	mov	rdx,rbx
	mov	r10d,24
	mov	eax,437
	syscall

	; site 3 -- REFUSED: rdx resolves, but onto a writable segment.
	lea	rdx,[how_rw]
	mov	r10d,24
	mov	eax,437
	syscall

	mov	eax,231
	xor	edi,edi
	syscall

segment readable
  ; struct open_how { u64 flags; u64 mode; u64 resolve; } -- 24 bytes
  how_beneath:
	dq	0x80000		; O_RDONLY | O_CLOEXEC
	dq	0		; mode
	dq	0x0B		; RESOLVE_BENEATH | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV
  path:	db	'inside.txt',0

segment readable writeable
  dir_record:	dq 0, 3
  how_rw:
	dq	0x80000, 0, 0x0B
