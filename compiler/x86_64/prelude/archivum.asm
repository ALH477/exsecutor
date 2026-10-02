; compiler/x86_64/prelude/archivum.asm
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
;
; [OPEN] LICENSING. Form 1, for prelude.asm's reason (its header): whether
; prelude text that travels into a compiled program carries the Form 2
; designation line is the owner of LICENSE.EXCEPTION's decision.
; -----------------------------------------------------------------------------
; `archivum` beneath a root: the prelude routines ADR 0017 decides and
; docs/design/archivum-beneath.md specifies (D2-D5, D8-D10; section 3 is the
; audit this code is shaped for). STAGE 1 of that ADR: the routines and the
; audit exist and are tested; the surface that calls them -- `Directorium`,
; `ad_radicem`, `infra`, `lege_ex`, `crea`, all returning
; `eventus<_, erratum>` -- waits on `eventus` being inhabited
; (docs/design/sum-types.md D5) and is NOT here. Nothing in interface.inc
; declares these routines, so by prelude.asm's naming rule they carry the
; PRIVATE prefix `exsrt_`, not `bfausr_exsrt_`: no IR can call them yet.
;
; A SECOND EXECUTABLE BLOB, NOT AN `include` IN prelude.asm. runtime.md H1
; forbids `include` in prelude.asm (OUT is self-contained, and nothing but
; vendor/fasmg-x86/ is on a user's include path), so this file is its own
; verbatim blob, with prelude.asm's contract: no `format`, no `entry`, no
; `segment`, no `include`, no data. A wrapper includes it into
; `segment readable executable` AFTER prelude.asm (exsrt_start must stay the
; first routine), and includes archivum_rodata.asm -- the four `open_how`
; constants -- into `segment readable`. Today the only wrappers are
; tests/unit fixtures. Carrying both blobs into OUT is
; backend_fasmg/program.inc's change and belongs to stage 2: until a surface
; call exists no compiled program's closure can hold `archivum` (spec 4.7:
; it is not derivable), so carrying them now would add text to every OUT
; and a gated-off block to every binary for nothing.
;
; THE GATE. Everything below sits in `if EXS_POTESTAS_ARCHIVUM`, as the
; `ambitus` routines sit in theirs (runtime.md 2.6): with the atom at 0 this
; file assembles to zero bytes, so a binary whose closure lacks `archivum`
; is byte-identical with or without it, and its syscall audit is unchanged.
;
; SYSCALLS, under `archivum` and nothing else (ADR 0017 decision 1):
;   openat2(437)  four sites, one per admitted `open_how` constant
;   fstat(5)      lege_ex's file-type check
;   close(3)      lege_ex, releasing a descriptor it refuses
;   read(0)       exsrt_archivum_lege
;   write(1)      exsrt_archivum_scribe
; lseek(8) is in the atom's row and issued by no routine here [OPEN]: the
; design specifies no seek. NOT openat(257), anywhere: there is no unscoped
; open to fall back to (D9), and tools/syscall-audit.sh admits 257 under no
; program atom, so a fallback would fail the audit as well as the design.
;
; EVERY openat2 SITE IS THESE FOUR INSTRUCTIONS, CONTIGUOUS, IN THIS ORDER
; (design section 3; the audit's A1-A6 check exactly this shape and refuse
; any other):
;	lea	rdx, [exsrt_how_<name>]	; rip-relative, onto 24 bytes in a
;					; non-writable segment
;	mov	r10d, 24		; sizeof(struct open_how), VER0, exactly
;	mov	eax, 437		; openat2
;	syscall
; `rdi` (the directory descriptor) and `rsi` (the NUL-terminated path) are
; loaded before the window. On `exsrt_how_radix`, `rdi` is the immediate
; AT_FDCWD (A5); on the three scoped constants it is loaded from the frame
; and is never an immediate (A6). The EAGAIN retry jumps back to the `lea`,
; the window's first instruction, never into it.
;
; RETURN CONVENTION, provisional until stage 2: `rax` is the result or a
; NEGATED errno, the raw kernel convention. The kernel's own refusals pass
; through unchanged (EXDEV, ELOOP, ENOENT, ENOTDIR, EEXIST, ...). The prelude
; adds four of its own, each BEFORE any syscall, each spelled with the errno
; nearest its meaning because `erratum` has no variants to spell it with:
;   -EINVAL        (22) a root that is not absolute (D2); a path with an
;                       interior NUL (D10); lege_ex on a file that is not
;                       regular (D4 -- after the open, which is then closed)
;   -ENAMETOOLONG  (36) a path of 4096 bytes or more (D10)
;   -EBADF          (9) a NEGATIVE directory descriptor on a scoped call:
;                       AT_FDCWD (-100) arriving at run time would make the
;                       walk beneath the CWD, which is ambient (F10). The
;                       audit refuses it as an immediate (A6); this refuses
;                       it as a value. Defence in depth, not in the design.
; Stage 2 maps these onto `erratum` when `eventus` has variants.
;
; THE PATH BUFFER LIVES IN THE CALLER'S STACK FRAME, not in prelude-owned
; state as design D10 has it. The design's buffer is "not reentrant,
; [OPEN] until threads exist"; a 4096-byte frame is reentrant now and needs
; no `prelude_data.asm` change. Same limit, same refusals.
;
; Calling convention: prelude.asm's -- plain SysV, scratch only in
; rax rcx rdx rsi rdi r8-r11, nothing live in rcx or r11 across a syscall,
; rbp pushed for a frame. No callee-saved register is touched.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md 4.6, 4.7. ADR 0017. Design:
; docs/design/archivum-beneath.md (section 2 is the measurement;
; tests/unit/prelude_archivum.asm re-asserts it through THESE routines).
; -----------------------------------------------------------------------------

if ~ defined EXS_POTESTAS_ARCHIVUM
	err 'archivum.asm: EXS_POTESTAS_ARCHIVUM is undefined -- include prelude.asm (which checks all eleven atoms) first'
end if

if EXS_POTESTAS_ARCHIVUM

EXS_ARCHIVUM_VIA_MAX	= 4096		; PATH_MAX, the NUL included (D10)
EXS_ARCHIVUM_ITERUM	= 16		; EAGAIN retries after the first try (D8)
EXS_ARCHIVUM_HOW_SIZE	= 24		; sizeof(struct open_how), VER0 (A2)
EXS_ARCHIVUM_AT_FDCWD	= -100		; the only immediate dirfd, how_radix only

EXS_ARCHIVUM_EINTR	= 4
EXS_ARCHIVUM_EBADF	= 9
EXS_ARCHIVUM_EAGAIN	= 11
EXS_ARCHIVUM_EINVAL	= 22
EXS_ARCHIVUM_ENAMETOOLONG = 36

EXS_ARCHIVUM_STAT_SIZE	= 144		; x86-64 `struct stat`
EXS_ARCHIVUM_STAT_MODE	= 24		; st_mode, u32
EXS_ARCHIVUM_S_IFMT	= 0xF000
EXS_ARCHIVUM_S_IFREG	= 0x8000

; The scoped calls' frame, below `rbp`: the directory descriptor, the opened
; descriptor, a held errno, one spare slot, then the path buffer at `rsp`.
; 32 + 4096 keeps `rsp` 16-aligned at the helper `call`.
EXS_ARCHIVUM_F_DIRFD	= 8
EXS_ARCHIVUM_F_FD	= 16
EXS_ARCHIVUM_F_ERR	= 24
EXS_ARCHIVUM_FRAME	= 32 + EXS_ARCHIVUM_VIA_MAX

; exsrt_archivum_via(via: ptr textus, buf: ptr) -> i64
;   Copies a `textus` (runtime.md 2.3: { ptr @0, len @8 }) into `buf` and
;   appends the NUL the kernel wants. 0, or -ENAMETOOLONG for a length of
;   4096 or more, or -EINVAL if the bytes hold a NUL (D10: truncation only
;   shortens and B would still apply, but it would open a different file
;   from the one the program named). No syscall.
exsrt_archivum_via:
	mov	r8, [rdi + EXS_TEXTUS_PTR]
	mov	r9, [rdi + EXS_TEXTUS_LEN]
	cmp	r9, EXS_ARCHIVUM_VIA_MAX
	jae	.longa
	xor	ecx, ecx
  .copia:
	cmp	rcx, r9
	jae	.finis
	mov	al, [r8 + rcx]
	test	al, al
	jz	.nul
	mov	[rsi + rcx], al
	inc	rcx
	jmp	.copia
  .finis:
	mov	byte [rsi + rcx], 0
	xor	eax, eax
	ret
  .longa:
	mov	rax, -EXS_ARCHIVUM_ENAMETOOLONG
	ret
  .nul:
	mov	rax, -EXS_ARCHIVUM_EINVAL
	ret

; exsrt_archivum_radix(via: ptr textus) -> i64
;   The derivation of a root: design D2, `Directorium.ad_radicem`'s body.
;   `openat2(AT_FDCWD, via, &how_radix, 24)`: O_PATH|O_DIRECTORY|O_CLOEXEC,
;   resolve = RESOLVE_NO_MAGICLINKS. Symlinks in the root's own spelling are
;   followed and magic links are not. Returns the O_PATH directory
;   descriptor, or -errno. A `via` that does not begin with `/` is -EINVAL
;   before any syscall: relative to the CWD is ambient (F10). This is the
;   ONE site whose walk is not beneath a descriptor (A5).
exsrt_archivum_radix:
	push	rbp
	mov	rbp, rsp
	sub	rsp, EXS_ARCHIVUM_FRAME
	mov	rsi, rsp
	call	exsrt_archivum_via		; rdi = via already
	test	rax, rax
	jnz	.exi
	cmp	byte [rsp], '/'			; absolute only (D2); "" fails here too
	jne	.relativa
	mov	edi, EXS_ARCHIVUM_AT_FDCWD	; the immediate A5 requires
	mov	rsi, rsp
	mov	r9d, EXS_ARCHIVUM_ITERUM
  .iterum:
	lea	rdx, [exsrt_how_radix]
	mov	r10d, EXS_ARCHIVUM_HOW_SIZE
	mov	eax, 437			; openat2
	syscall
	cmp	rax, -EXS_ARCHIVUM_EAGAIN	; D8: transient, bounded
	jne	.exi
	dec	r9d
	jns	.iterum
	jmp	.exi
  .relativa:
	mov	rax, -EXS_ARCHIVUM_EINVAL
  .exi:
	mov	rsp, rbp
	pop	rbp
	ret

; exsrt_archivum_infra(dirfd: i32, via: ptr textus) -> i64
;   Attenuation, design D3: `d.infra(via)`. `openat2(dirfd, via, &how_infra,
;   24)`: O_PATH|O_DIRECTORY|O_CLOEXEC beneath `dirfd`, resolve = B|M|X. The
;   result is itself a root: `..` from it to its parent is EXDEV (F7).
exsrt_archivum_infra:
	push	rbp
	mov	rbp, rsp
	sub	rsp, EXS_ARCHIVUM_FRAME
	test	edi, edi			; a negative descriptor is AT_FDCWD or
	js	.malum				; garbage: never a root
	mov	[rbp - EXS_ARCHIVUM_F_DIRFD], edi
	mov	rdi, rsi
	mov	rsi, rsp
	call	exsrt_archivum_via
	test	rax, rax
	jnz	.exi
	mov	edi, [rbp - EXS_ARCHIVUM_F_DIRFD]	; from memory: never an immediate (A6)
	mov	rsi, rsp
	mov	r9d, EXS_ARCHIVUM_ITERUM
  .iterum:
	lea	rdx, [exsrt_how_infra]
	mov	r10d, EXS_ARCHIVUM_HOW_SIZE
	mov	eax, 437			; openat2
	syscall
	cmp	rax, -EXS_ARCHIVUM_EAGAIN
	jne	.exi
	dec	r9d
	jns	.iterum
	jmp	.exi
  .malum:
	mov	rax, -EXS_ARCHIVUM_EBADF
  .exi:
	mov	rsp, rbp
	pop	rbp
	ret

; exsrt_archivum_crea(dirfd: i32, via: ptr textus) -> i64
;   A NEW file beneath `dirfd`, design D4: `d.crea(via)`. how_crea is
;   O_WRONLY|O_CREAT|O_EXCL|O_NOCTTY|O_CLOEXEC, mode 0600, resolve = B|M|X.
;   O_EXCL refuses an existing name whatever it is -- a file, a symlink, a
;   hard link to a file outside the root (F15) -- so a write never lands on
;   an inode this call did not create. Returns the writable descriptor or
;   -errno.
exsrt_archivum_crea:
	push	rbp
	mov	rbp, rsp
	sub	rsp, EXS_ARCHIVUM_FRAME
	test	edi, edi
	js	.malum
	mov	[rbp - EXS_ARCHIVUM_F_DIRFD], edi
	mov	rdi, rsi
	mov	rsi, rsp
	call	exsrt_archivum_via
	test	rax, rax
	jnz	.exi
	mov	edi, [rbp - EXS_ARCHIVUM_F_DIRFD]
	mov	rsi, rsp
	mov	r9d, EXS_ARCHIVUM_ITERUM
  .iterum:
	lea	rdx, [exsrt_how_crea]
	mov	r10d, EXS_ARCHIVUM_HOW_SIZE
	mov	eax, 437			; openat2
	syscall
	cmp	rax, -EXS_ARCHIVUM_EAGAIN
	jne	.exi
	dec	r9d
	jns	.iterum
	jmp	.exi
  .malum:
	mov	rax, -EXS_ARCHIVUM_EBADF
  .exi:
	mov	rsp, rbp
	pop	rbp
	ret

; exsrt_archivum_lege_ex(dirfd: i32, via: ptr textus) -> i64
;   An EXISTING REGULAR file beneath `dirfd`, to read: design D4,
;   `d.lege_ex(via)`. how_lege is O_RDONLY|O_NOCTTY|O_NONBLOCK|O_CLOEXEC,
;   resolve = B|M|X. Then `fstat`, and anything that is not S_IFREG -- a
;   directory, a FIFO, a device node (F8) -- is closed and refused with
;   -EINVAL. O_NONBLOCK is what lets a FIFO be refused rather than waited
;   on; on a regular file Linux ignores it for reads. A device's driver
;   `open` has already run by the time `fstat` answers: design section 8,
;   [OPEN]. Returns the readable descriptor or -errno.
exsrt_archivum_lege_ex:
	push	rbp
	mov	rbp, rsp
	sub	rsp, EXS_ARCHIVUM_FRAME
	test	edi, edi
	js	.malum
	mov	[rbp - EXS_ARCHIVUM_F_DIRFD], edi
	mov	rdi, rsi
	mov	rsi, rsp
	call	exsrt_archivum_via
	test	rax, rax
	jnz	.exi
	mov	edi, [rbp - EXS_ARCHIVUM_F_DIRFD]
	mov	rsi, rsp
	mov	r9d, EXS_ARCHIVUM_ITERUM
  .iterum:
	lea	rdx, [exsrt_how_lege]
	mov	r10d, EXS_ARCHIVUM_HOW_SIZE
	mov	eax, 437			; openat2
	syscall
	cmp	rax, -EXS_ARCHIVUM_EAGAIN
	jne	.aperta
	dec	r9d
	jns	.iterum
	jmp	.exi
  .aperta:
	test	rax, rax
	js	.exi				; -errno from the walk
	mov	[rbp - EXS_ARCHIVUM_F_FD], rax
	mov	edi, eax
	mov	rsi, rsp			; the path is spent: reuse its buffer
	mov	eax, 5				; fstat
	syscall
	test	rax, rax
	js	.claude				; fstat failed: release, report that
	mov	eax, [rsp + EXS_ARCHIVUM_STAT_MODE]
	and	eax, EXS_ARCHIVUM_S_IFMT
	cmp	eax, EXS_ARCHIVUM_S_IFREG
	jne	.non_regularis
	mov	rax, [rbp - EXS_ARCHIVUM_F_FD]
	jmp	.exi
  .non_regularis:
	mov	rax, -EXS_ARCHIVUM_EINVAL
  .claude:
	mov	[rbp - EXS_ARCHIVUM_F_ERR], rax
	mov	edi, [rbp - EXS_ARCHIVUM_F_FD]
	mov	eax, 3				; close -- its own result is not the
	syscall					; caller's: Linux frees the number either way
	mov	rax, [rbp - EXS_ARCHIVUM_F_ERR]
	jmp	.exi
  .malum:
	mov	rax, -EXS_ARCHIVUM_EBADF
  .exi:
	mov	rsp, rbp
	pop	rbp
	ret

; exsrt_archivum_lege(fd: i32, buf: ptr, n: u64) -> i64
;   One `read(fd, buf, n)`: the count, 0 at end of file, or -errno. EINTR
;   is retried; nothing else is. Whether a short count is retried is the
;   caller's business (a reader type's, stage 2). The EINTR path needs a
;   second process to produce and is [UNTESTED], as `scribe`'s is.
exsrt_archivum_lege:
  .iterum:
	mov	eax, 0				; read
	syscall
	cmp	rax, -EXS_ARCHIVUM_EINTR
	je	.iterum
	ret

; exsrt_archivum_scribe(fd: i32, buf: ptr, n: u64) -> i64
;   Writes all `n` bytes, looping over partial writes and retrying EINTR,
;   `scribe`'s loop. Returns `n` on success. On any other -errno: the count
;   already written if it is not 0 (short means failed, as for `scribe`),
;   else the -errno itself, so a write that moved nothing still says why.
;   The partial-write and EINTR paths are [UNTESTED], as `scribe`'s are.
exsrt_archivum_scribe:
	push	rbp
	mov	rbp, rsp
	sub	rsp, 16
	mov	[rbp - 8], rdx			; the total requested -- NOT in r11
	mov	r8, rsi
	mov	r9, rdx				; remaining
  .ansa:
	test	r9, r9
	jz	.facta
	mov	rsi, r8
	mov	rdx, r9
	mov	eax, 1				; write
	syscall
	cmp	rax, -4096
	ja	.error
	add	r8, rax
	sub	r9, rax
	jmp	.ansa
  .error:
	cmp	rax, -EXS_ARCHIVUM_EINTR
	je	.ansa
	mov	rcx, [rbp - 8]
	cmp	rcx, r9
	je	.exi				; nothing written: -errno in rax
  .facta:
	mov	rax, [rbp - 8]
	sub	rax, r9
  .exi:
	mov	rsp, rbp
	pop	rbp
	ret

end if	; EXS_POTESTAS_ARCHIVUM
