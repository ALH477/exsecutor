; compiler/x86_64/driver/io.inc
; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 The Exsecutor authors.
;
; DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
;
; This code is free software; you can redistribute it and/or modify it under
; the terms of the GNU General Public License as published by the Free
; Software Foundation, either version 3 of the License, or (at your option)
; any later version.
;
; This code is distributed in the hope that it will be useful, but WITHOUT
; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
; FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
; more details.
;
; You should have received a copy of the GNU General Public License along
; with this code. If not, see <https://www.gnu.org/licenses/>.
;
; Code produced by this compiler is not covered by the GPL --
; see Exception A in LICENSE.EXCEPTION.
; -----------------------------------------------------------------------------
; Reading one source file, and nothing else. Four syscalls, all of them
; already on the closed allowlist and all of them reached through
; rt/sys.inc's wrappers: `openat(257)`, `fstat(5)`, `read(0)`, `close(3)`.
; This file adds nothing to the allowlist and emits no raw `syscall`
; (docs/asm-conventions.md, "6. Syscall discipline").
;
; NO SOCKET SYSCALL APPEARS IN THIS FILE OR ANYWHERE ELSE UNDER driver/.
; §9.3: *"No network access, ever, at any phase."* `make audit` checks the
; assembled binary rather than this claim -- which is the point of writing it
; as a check instead of a sentence.
;
; `AT_FDCWD`, NOT AN AMBIENT CWD OF OUR OWN. rt/sys.inc's header is explicit
; that `sys_openat` has "no ambient-cwd concept of its own" and that callers
; pass `dirfd` themselves. `exsc` resolves a relative SOURCE against the
; process's working directory, which is where the invoker pointed it -- that
; is a property of the invocation, not ambient state §9.3 forbids, and it is
; the same directory the path was written for.
;
; THE ARENA IS SIZED FROM THE FILE, WHICH IS WHY IT IS CREATED HERE.
; rt/arena.inc is deliberately fixed-capacity: *"`arena_alloc` past the
; capacity given to `arena_init` `rassert`s ... rather than transparently
; mapping more"*, and `[UNIMPLEMENTED]: auto-growth / chunked arenas`. A
; driver that picked a constant capacity would therefore trap (SIGILL, exit
; 132) on a file bigger than someone guessed. Instead `fstat` runs first and
; the arena is `arena_init`ed at DRV_ARENA_FLOOR + DRV_ARENA_FACTOR times the
; file's size.
;
; WHERE DRV_ARENA_FACTOR COMES FROM, since a magic number that is only a
; guess is worse than no number. The lexer allocates three things out of this
; arena, and each is bounded per source byte:
;   - the source text itself: 1x.
;   - the token vector: at worst one `Tok` (16 bytes) per source byte, and
;     rt/vec.inc's doubling growth "abandons the old backing block rather
;     than resizing it in place", so a vector that reached N bytes has
;     consumed about 2N of arena. 32x.
;   - the diagnostic vector: `sizeof.Diag` is 96 bytes and the densest
;     possible run of diagnostics is one `EXS-E0103` per three-byte
;     dangerous codepoint -- 32 bytes of Diag per source byte, doubled the
;     same way. 64x.
; 96 covers the worst of those plus the source and the ~20 KiB of NFC
; scratch `lex_init` takes. It is an over-estimate on purpose: the region is
; anonymous `mmap`, so an unused reservation costs address space and no
; pages. DRV_SRC_MAX bounds the reservation itself at a bit over 1.5 GiB.
;
; `[UNIMPLEMENTED]`, stated rather than buried: a source file whose
; diagnostics somehow exceed that bound still traps through `arena_alloc`'s
; `rassert` instead of failing cleanly. The real fix is a growable arena, and
; that is rt/'s to build, not this file's to work around.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.3 (purity, the syscall surface),
; §8.1 (what the bytes are then held to).
; -----------------------------------------------------------------------------

include 'cli.inc'

DRV_AT_FDCWD = -100		; openat: resolve relative to the process cwd
DRV_O_RDONLY = 0

; x86-64 Linux `struct stat`: 144 bytes, st_mode at 24, st_size at 48.
; Named here rather than spelled as bare integers at the use site;
; rt/sys.inc deliberately does not define this layout ("nothing in this
; wave's rt/ reads individual stat fields yet"), so the first reader of a
; stat field defines it, and this is that reader.
DRV_STAT_SIZE   = 144
DRV_ST_MODE_OFF = 24
DRV_ST_SIZE_OFF = 48
DRV_S_IFMT      = 0xF000
DRV_S_IFREG     = 0x8000

DRV_SRC_MAX      = 16 * 1024 * 1024
DRV_ARENA_FLOOR  = 4 * 1024 * 1024
DRV_ARENA_FACTOR = 96

drv_m_open   db 'cannot open: ',0
drv_m_stat   db 'cannot stat: ',0
drv_m_read   db 'cannot read: ',0
drv_m_notreg db 'not a regular file: ',0
drv_m_close  db 'cannot close: ',0
drv_m_toobig db 'source is larger than this build accepts: ',0
drv_m_bytes  db ', bytes ',0
drv_m_nomem  db 'cannot reserve memory for the compilation, errno ',0
drv_m_errno  db ', errno ',0

; drv_cstr_dup(arena, ptr, len) -> rax = a fresh `len + 1` byte copy of the
; `len` bytes at `ptr`, NUL-terminated, out of `arena`. FOR THE SYSCALL
; BOUNDARY ONLY -- `openat` takes a C string and nothing else in this
; compiler does. Never for identity or comparison; rt/intern.inc's IDs are
; what identity is.
;
; rt/str.inc's `str_to_cstr` is this routine, and is unreachable from the
; build for the reason driver/cli.inc's `drv_arg_is` records in full.
; Cannot fail (or `rassert`s transitively through `arena_alloc`); leaves CF
; clear.
proc drv_cstr_dup, arena, ptr, len
	uses	rbx
	locals
	endl
	mov	rdi, [arena]
	mov	rsi, [len]
	inc	rsi
	call	arena_alloc
	mov	rbx, rax
	mov	rdi, rbx
	mov	rsi, [ptr]
	mov	rcx, [len]
	cld
	rep	movsb
	mov	rcx, [len]
	mov	byte [rbx + rcx], 0
	mov	rax, rbx
	return
endp

; drv_slurp(ptr, len, n) -> rax = a pointer to the file's bytes, with [n] set
; to how many there are; or rax = 0, having ALREADY said what went wrong.
; `ptr`/`len` name the file. On success `DrvCtx.arena` at `r15` has been
; `arena_init`ed and owns the buffer.
;
; THE 0 RETURN IS THE SENTINEL CHANNEL, not a fourth meaning for `CF`/`eax`.
; docs/asm-conventions.md's "1.3 Error protocol" lists four failure channels
; and this is the third of them ("an expected 'absent' result ... a sentinel;
; `CF` is not meaningful"): a successful `mmap`-backed allocation is never
; address 0, so 0 is unambiguous. It is NOT the errno channel, because this
; routine reports its own failure rather than handing an errno up: it is the
; only place that knows which of four syscalls failed and on which path, and
; a caller reconstructing that from a bare `-errno` would be guessing. What
; it does hand up is the decision -- the caller chooses the exit status.
;
; An empty file is a success with [n] = 0, not a failure. §8.1 has nothing to
; say against a zero-byte source and the lexer tokenizes it to a single
; `TOK_EOF`; refusing it here would be this file inventing a rule.
proc drv_slurp, ptr, len, n
	uses	rbx, r12, r13, r14
	locals
		slot st,   DRV_STAT_SIZE
		slot cpath, dq
		slot fsize, dq
		slot got,  dq
		slot erno, dd
		slot pad0, dd
	endl
	mov	qword [got], 0

	; A NUL-terminated copy for the kernel, out of the interner's arena
	; (never reset). argv's own bytes already carry a NUL, but this routine
	; takes a pointer and a length and must not assume one byte past the
	; length is readable, let alone zero.
	mov	rdi, [r15 + DrvCtx.iarena]
	mov	rsi, [ptr]
	mov	rdx, [len]
	call	drv_cstr_dup
	mov	[cpath], rax

	mov	rdi, DRV_AT_FDCWD
	mov	rsi, [cpath]
	mov	rdx, DRV_O_RDONLY
	xor	rcx, rcx
	call	sys_openat
	jc	.open_failed
	mov	rbx, rax		; fd, live until .close

	mov	rdi, rbx
	lea	rsi, [st]
	call	sys_fstat
	jc	.stat_failed

	mov	eax, [st + DRV_ST_MODE_OFF]
	and	eax, DRV_S_IFMT
	cmp	eax, DRV_S_IFREG
	jne	.not_regular

	mov	r13, [st + DRV_ST_SIZE_OFF]
	mov	[fsize], r13
	cmp	r13, DRV_SRC_MAX
	ja	.too_big

	; ---- the arena, sized from the file --------------------------------
	mov	rax, r13
	mov	rcx, DRV_ARENA_FACTOR
	mul	rcx			; rdx:rax; r13 <= DRV_SRC_MAX so rdx = 0
	add	rax, DRV_ARENA_FLOOR
	mov	rdi, [r15 + DrvCtx.arena]
	mov	rsi, rax
	call	arena_init
	jc	.no_memory

	; A zero-length file still gets a one-byte allocation, so the buffer
	; pointer is a real address and never collides with this routine's own
	; 0-means-failure sentinel.
	mov	rdi, [r15 + DrvCtx.arena]
	mov	rsi, r13
	test	rsi, rsi
	jnz	.have_size
	mov	rsi, 1
  .have_size:
	call	arena_alloc
	mov	r12, rax		; the buffer

	; ---- read to EOF ---------------------------------------------------
	; A loop, not one `read`: a short read is legal and this compiler has
	; no libc stdio to hide that. It stops on 0 (EOF) rather than on
	; having read st_size bytes, so a file that shrank between `fstat` and
	; here yields what is actually there instead of trailing garbage.
	xor	r14, r14		; bytes read so far
  .read:
	cmp	r14, r13
	jae	.read_done
	mov	rdi, rbx
	lea	rsi, [r12 + r14]
	mov	rdx, r13
	sub	rdx, r14
	call	sys_read
	jc	.read_failed
	test	rax, rax
	jz	.read_done		; EOF
	add	r14, rax
	jmp	.read
  .read_done:
	mov	[got], r14

	mov	rdi, rbx
	call	sys_close
	jc	.close_failed

	mov	rax, [n]
	mov	rcx, [got]
	mov	[rax], rcx
	mov	rax, r12
	return

  ; ---- the refusals -------------------------------------------------------
  ; Each names the file, ESCAPED (§8.3 -- a path is not ours, and one
  ; containing a bidi override must not be able to rewrite the message that
  ; reports it), and carries the kernel's own errno so the reason is not
  ; guessed at. Written out one block per case rather than funnelled through
  ; a shared tail: the shared version had to shuffle five arguments into
  ; place from three different branches and got them out of order.
  ;
  ; `eax` holds the wrapper's `-errno` at each of these labels
  ; (docs/asm-conventions.md, "1.3 Error protocol": the errno channel);
  ; `neg` turns it into the number a reader can look up.
  .open_failed:
	neg	eax
	mov	[erno], eax
	lea	rdi, [drv_m_open]
	mov	rsi, [ptr]
	mov	rdx, [len]
	lea	rcx, [drv_m_errno]
	mov	r8d, [erno]
	call	drv_msgu
	xor	eax, eax
	return

  .stat_failed:
	neg	eax
	mov	[erno], eax
	mov	rdi, rbx
	call	sys_close
	lea	rdi, [drv_m_stat]
	mov	rsi, [ptr]
	mov	rdx, [len]
	lea	rcx, [drv_m_errno]
	mov	r8d, [erno]
	call	drv_msgu
	xor	eax, eax
	return

  .read_failed:
	neg	eax
	mov	[erno], eax
	mov	rdi, rbx
	call	sys_close
	lea	rdi, [drv_m_read]
	mov	rsi, [ptr]
	mov	rdx, [len]
	lea	rcx, [drv_m_errno]
	mov	r8d, [erno]
	call	drv_msgu
	xor	eax, eax
	return

  .close_failed:
	neg	eax
	mov	[erno], eax
	lea	rdi, [drv_m_close]
	mov	rsi, [ptr]
	mov	rdx, [len]
	lea	rcx, [drv_m_errno]
	mov	r8d, [erno]
	call	drv_msgu
	xor	eax, eax
	return

  .no_memory:
	neg	eax
	mov	[erno], eax
	mov	rdi, rbx
	call	sys_close
	lea	rdi, [drv_m_nomem]
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	mov	r8d, [erno]
	call	drv_msgu
	xor	eax, eax
	return

  .not_regular:
	mov	rdi, rbx
	call	sys_close
	lea	rdi, [drv_m_notreg]
	mov	rsi, [ptr]
	mov	rdx, [len]
	xor	rcx, rcx
	call	drv_msg
	xor	eax, eax
	return

  .too_big:
	mov	rdi, rbx
	call	sys_close
	lea	rdi, [drv_m_toobig]
	mov	rsi, [ptr]
	mov	rdx, [len]
	lea	rcx, [drv_m_bytes]
	mov	r8, [fsize]
	call	drv_msgu
	xor	eax, eax
	return
endp
