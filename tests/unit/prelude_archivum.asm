; tests/unit/prelude_archivum.asm
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
; `archivum` beneath a root, run: compiler/x86_64/prelude/archivum.asm's
; routines called directly from a hand-written `bfausr_initium`, the way
; prelude_lege_octeto.asm calls the reader. ADR 0017 stage 1: no surface
; syntax reaches these routines yet (they wait on `eventus`), so this is the
; only caller they have.
;
; WHAT IT RE-ASSERTS, through the prelude's own four `open_how` constants on
; the kernel that runs it, of docs/design/archivum-beneath.md section 2's
; measurement (the probe passed the same constants from C; this passes them
; from the prelude's bytes):
;   F1  `..` out, an escaping symlink, an absolute path and an absolute
;       symlink are EXDEV; `sub/../x` and an in-root symlink open.
;   F2  a magic link beneath a root is refused (ELOOP: RESOLVE_NO_MAGICLINKS),
;       and how_radix refuses one in a root's own spelling.
;   F3  a mount crossing beneath a root is EXDEV (RESOLVE_NO_XDEV): `/` to
;       `proc/self/status`.
;   F7  a root derived by `infra` cannot climb to its parent.
;   F15 how_crea refuses every existing name, a symlink out included.
;   D2  a relative root is refused before any syscall; symlinks in a root's
;       own spelling are followed (nexus_absolutus -> `/`).
;   D4  lege_ex refuses a directory AFTER opening it and CLOSES the
;       descriptor (the next open reuses its number).
;   D10 a path of 4096 bytes, and a path with an interior NUL, are refused.
;   and the prelude's own refusal of a negative directory descriptor
;   (archivum.asm's header: AT_FDCWD as a run-time VALUE is ambient too).
; EVERY REFUSAL HAS ITS ANTI-VACUITY LEG: the same target reached legally --
; the escaping symlink and the `..` through a root one level up, the
; crossed mount's file through a root on that mount, the absolute symlink as
; a root's own spelling -- so a refusal cannot pass because the target was
; simply missing.
;
; THE TREE is tests/data/archivum/ (git-tracked, symlinks included):
;   foris.txt                     "FORIS\n"       -- outside the root
;   radix/intus.txt               "INTUS\n"
;   radix/sub/profundum.txt       "PROFUNDUM\n"
;   radix/nexus_intus      -> sub/profundum.txt   (stays beneath)
;   radix/nexus_foras      -> ../foris.txt        (escapes)
;   radix/nexus_absolutus  -> /                   (absolute)
; It is read-only to this fixture: every create aimed at it must fail, and
; does, before writing. Its ABSOLUTE path comes from fasmg's `__file__` at
; assembly time: tests/run.sh and tools/syscall-audit.sh --self-test both
; pass an absolute source path, and a relative one is an assembly error
; here rather than a binary that tests the wrong directory. The binary
; therefore embeds this checkout's path: a test artifact, not compiler
; output, and never reproduced.
;
; THE WRITES go to the directory holding this binary, taken from argv[0]
; (tests/run.sh runs it by absolute path, from a `mktemp -d` it deletes).
; One file, `prelude_archivum.novum`, created with how_crea, written, read
; back. There is no unlink in `archivum`'s row (design section 5), so the
; harness's cleanup is what removes it.
;
; WHAT IT CANNOT PROVE: the EAGAIN retry (needs a renaming racer -- the
; probe's section 2.4); ENOSYS/EPERM with no fallback (needs a seccomp
; filter, and `prctl`/`seccomp` are on no allowlist) -- that there IS no
; fallback is the audit's to prove instead: this binary contains no
; openat(257) and passes --potestates Mundus,archivum; the EINTR paths.
; All [UNTESTED] here.
;
; THE AUDIT. tests/run.sh audits unit fixtures against the COMPILER's nine,
; where openat2 is not admitted: `audit=fail` below is that. The per-atom
; audit is tools/syscall-audit.sh --self-test's: this binary must PASS
; under `--potestates Mundus,archivum` (every openat2 site proven by rules
; W, A1-A6) and FAIL under `--potestates Mundus` alone.
;
; Exit: 0 = every check. N = check N failed (the `gradus` numbers below).
;
; TEST: run=yes expect-exit=0 audit=fail

include 'format/format.inc'

format ELF64 executable 3
entry exsrt_start

; ---- what a wrapper must define (prelude/README.md) -------------------------
; {Mundus, archivum}: NOT ambitus. Every read and write below is archivum's.
EXS_POTESTAS_MUNDUS	= 1
EXS_POTESTAS_ALLOC	= 0
EXS_POTESTAS_SERMO	= 0
EXS_POTESTAS_HOROLOGIUM	= 0
EXS_POTESTAS_ARCHIVUM	= 1
EXS_POTESTAS_RETE	= 0
EXS_POTESTAS_FORTUNA	= 0
EXS_POTESTAS_AMBITUS	= 0
EXS_POTESTAS_FILUM	= 0
EXS_POTESTAS_MACHINA	= 0
EXS_POTESTAS_CRUDUM	= 0
EXS_MXCSR		= 0x1F80

EXSFX_SENTINEL_B	= 0x0123456789ABCDEF
EXSFX_SENTINEL_F	= 0xFEDCBA9876543210

; Linux errnos the kernel or the prelude must answer with.
EXSFX_ENOENT	= 2
EXSFX_EBADF	= 9
EXSFX_EEXIST	= 17
EXSFX_EXDEV	= 18
EXSFX_ENOTDIR	= 20
EXSFX_EINVAL	= 22
EXSFX_ENAMETOOLONG = 36
EXSFX_ELOOP	= 40

; ---- this source's directory, absolute, from __file__ -----------------------
virtual at 0
  exsfx_fons::
	db	__file__
  EXSFX_FONS_N = $
end virtual
load exsfx_c0:byte from exsfx_fons:0
if exsfx_c0 <> '/'
	err 'prelude_archivum.asm must be assembled from an ABSOLUTE path (tests/run.sh and --self-test do): the tree it opens is found from __file__'
end if
EXSFX_SL = 0
repeat EXSFX_FONS_N, i:0
	load exsfx_c:byte from exsfx_fons:i
	if exsfx_c = '/'
		EXSFX_SL = i
	end if
end repeat
load EXSFX_DIR:EXSFX_SL+1 from exsfx_fons:0	; ".../tests/unit/"

; textus NAME, 'bytes'...  -- { ptr @0, len @8 } (runtime.md 2.3) + bytes
macro textus nomen*, chorda&
	local b, e
	nomen:
	dq	b, e - b
	b:
	db	chorda
	e:
end macro

; textus_fontis NAME, 'suffix' -- the same, prefixed with this directory
macro textus_fontis nomen*, chorda&
	local b, e
	nomen:
	dq	b, e - b
	b:
	emit	EXSFX_SL + 1: EXSFX_DIR
	db	chorda
	e:
end macro

; gradus N -- the number this fixture exits with if the next check fails
macro gradus n*
	mov	r12d, n
end macro

; Frame slots below rbp (after the five pushes at -8..-40).
F_DR	= 48	; root: data/archivum/radix
F_DP	= 56	; root: data/archivum (one level up -- the anti-vacuity root)
F_DS	= 64	; infra(dR, "sub")
F_DF	= 72	; root: /proc/self/fd
F_D0	= 80	; root: /
F_DPS	= 88	; root: /proc/self
F_FA	= 96	; a descriptor, for the leak check
F_DW	= 104	; root: the directory holding this binary
F_W	= 112	; the created file
F_VIA	= 128	; textus { ptr, len } of that directory (16 bytes)
F_BUF	= 192	; 64-byte read buffer, [rbp-192, rbp-128)
F_SIZE	= 152	; below the pushes: 40 + 152 = 192; rsp 16-aligned at calls

segment readable executable

include '../../compiler/x86_64/prelude/prelude.asm'
include '../../compiler/x86_64/prelude/archivum.asm'

; exsfx_lege_totum(dirfd: i32, via: ptr textus, expectatum: ptr textus,
;                  buf: ptr) -> i64
;   lege_ex, then ONE lege into a 64-byte buffer, then a second lege that
;   must be end of file. 0 iff the bytes are exactly `expectatum`'s; 1 if
;   they differ or the file does not end there; the -errno if lege_ex
;   refused. Uses only the prelude's routines.
exsfx_lege_totum:
	push	rbp
	mov	rbp, rsp
	push	rbx
	push	r12
	push	r13
	push	r14
	mov	r12, rdx			; expectatum
	mov	r13, rcx			; buf
	call	exsrt_archivum_lege_ex
	test	rax, rax
	js	.exi
	mov	r14, rax			; fd
	mov	edi, r14d
	mov	rsi, r13
	mov	edx, 64
	call	exsrt_archivum_lege
	mov	rbx, rax			; n
	cmp	rbx, [r12 + 8]
	jne	.differt
	mov	rsi, [r12]
	xor	ecx, ecx
  .cmp:
	cmp	rcx, rbx
	jae	.finis
	mov	al, [r13 + rcx]
	cmp	al, [rsi + rcx]
	jne	.differt
	inc	rcx
	jmp	.cmp
  .finis:
	mov	edi, r14d
	mov	rsi, r13
	mov	edx, 64
	call	exsrt_archivum_lege		; must be end of file: 0
	test	rax, rax
	jnz	.differt
	xor	eax, eax
	jmp	.exi
  .differt:
	mov	eax, 1
  .exi:
	pop	r14
	pop	r13
	pop	r12
	pop	rbx
	pop	rbp
	ret

; ---- the module -------------------------------------------------------------
bfausr_initium:
	push	rbp
	mov	rbp, rsp
	push	rbx
	push	r12
	push	r13
	push	r14
	push	r15
	sub	rsp, F_SIZE

	mov	rbx, EXSFX_SENTINEL_B
	mov	r15, EXSFX_SENTINEL_F
	mov	r13, rdi			; the Mundus carrier: rsp0 at +0

	; ---- deriving roots (how_radix) ------------------------------------
	gradus 1				; the root
	lea	rdi, [t_radix]
	call	exsrt_archivum_radix
	test	rax, rax
	js	.malum
	mov	[rbp - F_DR], rax
	gradus 2				; its parent
	lea	rdi, [t_parens]
	call	exsrt_archivum_radix
	test	rax, rax
	js	.malum
	mov	[rbp - F_DP], rax

	; ---- in-root opens succeed (F1's admitted rows) --------------------
	gradus 3
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_intus]
	lea	rdx, [t_INTUS]
	lea	rcx, [rbp - F_BUF]
	call	exsfx_lege_totum
	test	rax, rax
	jnz	.malum
	gradus 4				; a `..` that stays beneath
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_sub_supra_intus]
	lea	rdx, [t_INTUS]
	lea	rcx, [rbp - F_BUF]
	call	exsfx_lege_totum
	test	rax, rax
	jnz	.malum
	gradus 5				; a symlink that stays beneath
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_nexus_intus]
	lea	rdx, [t_PROFUNDUM]
	lea	rcx, [rbp - F_BUF]
	call	exsfx_lege_totum
	test	rax, rax
	jnz	.malum

	; ---- escapes are EXDEV, each with its legal twin -------------------
	gradus 6				; `..` out
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_supra_foris]
	call	exsrt_archivum_lege_ex
	cmp	rax, -EXSFX_EXDEV
	jne	.malum
	gradus 7				; ... the same file, one root up
	mov	edi, [rbp - F_DP]
	lea	rsi, [t_radix_supra_foris]
	lea	rdx, [t_FORIS]
	lea	rcx, [rbp - F_BUF]
	call	exsfx_lege_totum
	test	rax, rax
	jnz	.malum
	gradus 8				; an escaping symlink
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_nexus_foras]
	call	exsrt_archivum_lege_ex
	cmp	rax, -EXSFX_EXDEV
	jne	.malum
	gradus 9				; ... the same link, one root up
	mov	edi, [rbp - F_DP]
	lea	rsi, [t_radix_nexus_foras]
	lea	rdx, [t_FORIS]
	lea	rcx, [rbp - F_BUF]
	call	exsfx_lege_totum
	test	rax, rax
	jnz	.malum
	gradus 10				; an absolute path to that file
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_foris_absoluta]
	call	exsrt_archivum_lege_ex
	cmp	rax, -EXSFX_EXDEV
	jne	.malum
	gradus 11				; ... which exists
	mov	edi, [rbp - F_DP]
	lea	rsi, [t_foris]
	lea	rdx, [t_FORIS]
	lea	rcx, [rbp - F_BUF]
	call	exsfx_lege_totum
	test	rax, rax
	jnz	.malum
	gradus 12				; an absolute symlink, to a directory
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_nexus_absolutus]
	call	exsrt_archivum_infra
	cmp	rax, -EXSFX_EXDEV
	jne	.malum
	gradus 13				; ... followed as a root's own spelling (D2)
	lea	rdi, [t_radix_nexus_absolutus]
	call	exsrt_archivum_radix
	test	rax, rax
	js	.malum

	; ---- attenuation (F7) ----------------------------------------------
	gradus 14
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_sub]
	call	exsrt_archivum_infra
	test	rax, rax
	js	.malum
	mov	[rbp - F_DS], rax
	gradus 15
	mov	edi, [rbp - F_DS]
	lea	rsi, [t_profundum]
	lea	rdx, [t_PROFUNDUM]
	lea	rcx, [rbp - F_BUF]
	call	exsfx_lege_totum
	test	rax, rax
	jnz	.malum
	gradus 16				; the parent's file, from the child root
	mov	edi, [rbp - F_DS]
	lea	rsi, [t_supra_intus]
	call	exsrt_archivum_lege_ex
	cmp	rax, -EXSFX_EXDEV
	jne	.malum

	; ---- magic links (F2) ----------------------------------------------
	gradus 17
	lea	rdi, [t_proc_self_fd]
	call	exsrt_archivum_radix
	test	rax, rax
	js	.malum
	mov	[rbp - F_DF], rax
	gradus 18				; fd/0 beneath that root: a magic link
	mov	edi, [rbp - F_DF]
	lea	rsi, [t_nulla]
	call	exsrt_archivum_lege_ex
	cmp	rax, -EXSFX_ELOOP
	jne	.malum
	gradus 19				; how_radix: a magic link in the spelling
	lea	rdi, [t_proc_self_cwd]
	call	exsrt_archivum_radix
	cmp	rax, -EXSFX_ELOOP
	jne	.malum
	gradus 20				; the absolute spelling, beneath a root
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_proc_self_fd_0]
	call	exsrt_archivum_lege_ex
	cmp	rax, -EXSFX_EXDEV
	jne	.malum

	; ---- a mount crossing (F3) -----------------------------------------
	gradus 21
	lea	rdi, [t_solum]
	call	exsrt_archivum_radix
	test	rax, rax
	js	.malum
	mov	[rbp - F_D0], rax
	gradus 22				; `/` -> proc: crosses into procfs
	mov	edi, [rbp - F_D0]
	lea	rsi, [t_proc_self_status]
	call	exsrt_archivum_lege_ex
	cmp	rax, -EXSFX_EXDEV
	jne	.malum
	gradus 23
	lea	rdi, [t_abs_proc_self]
	call	exsrt_archivum_radix
	test	rax, rax
	js	.malum
	mov	[rbp - F_DPS], rax
	gradus 24				; ... the same file from a root ON procfs
	mov	edi, [rbp - F_DPS]
	lea	rsi, [t_status]
	call	exsrt_archivum_lege_ex
	test	rax, rax
	js	.malum

	; ---- the prelude's own refusals (D2, D10, negative dirfd) ----------
	gradus 25
	lea	rdi, [t_relativa]
	call	exsrt_archivum_radix
	cmp	rax, -EXSFX_EINVAL
	jne	.malum
	gradus 26
	lea	rdi, [t_vacua]
	call	exsrt_archivum_radix
	cmp	rax, -EXSFX_EINVAL
	jne	.malum
	gradus 27
	lea	rdi, [t_nul_intra]
	call	exsrt_archivum_radix
	cmp	rax, -EXSFX_EINVAL
	jne	.malum
	gradus 28
	lea	rdi, [t_longa]
	call	exsrt_archivum_radix
	cmp	rax, -EXSFX_ENAMETOOLONG
	jne	.malum
	gradus 29
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_longa]
	call	exsrt_archivum_lege_ex
	cmp	rax, -EXSFX_ENAMETOOLONG
	jne	.malum
	gradus 30				; AT_FDCWD as a VALUE
	mov	edi, -100
	lea	rsi, [t_sub]
	call	exsrt_archivum_infra
	cmp	rax, -EXSFX_EBADF
	jne	.malum
	gradus 31
	mov	edi, -100
	lea	rsi, [t_intus]
	call	exsrt_archivum_lege_ex
	cmp	rax, -EXSFX_EBADF
	jne	.malum
	gradus 32
	mov	edi, -100
	lea	rsi, [t_intus]
	call	exsrt_archivum_crea
	cmp	rax, -EXSFX_EBADF
	jne	.malum

	; ---- the kernel's refusals of a root -------------------------------
	gradus 33				; not a directory
	lea	rdi, [t_foris_absoluta]
	call	exsrt_archivum_radix
	cmp	rax, -EXSFX_ENOTDIR
	jne	.malum
	gradus 34				; not there
	lea	rdi, [t_nusquam]
	call	exsrt_archivum_radix
	cmp	rax, -EXSFX_ENOENT
	jne	.malum

	; ---- lege_ex refuses a non-regular file and releases it (D4) -------
	gradus 35
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_intus]
	call	exsrt_archivum_lege_ex
	test	rax, rax
	js	.malum
	mov	[rbp - F_FA], rax
	gradus 36				; a directory opens O_RDONLY, then fstat
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_sub]
	call	exsrt_archivum_lege_ex
	cmp	rax, -EXSFX_EINVAL
	jne	.malum
	gradus 37				; its number came back: it was closed
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_intus]
	call	exsrt_archivum_lege_ex
	mov	rcx, [rbp - F_FA]
	inc	rcx
	cmp	rax, rcx
	jne	.malum

	; ---- how_crea refuses every existing name (F15), and `..` ----------
	gradus 38
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_intus]
	call	exsrt_archivum_crea
	cmp	rax, -EXSFX_EEXIST
	jne	.malum
	gradus 39
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_nexus_intus]
	call	exsrt_archivum_crea
	cmp	rax, -EXSFX_EEXIST
	jne	.malum
	gradus 40				; an existing link OUT: never followed
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_nexus_foras]
	call	exsrt_archivum_crea
	cmp	rax, -EXSFX_EEXIST
	jne	.malum
	gradus 41
	mov	edi, [rbp - F_DR]
	lea	rsi, [t_supra_nova]
	call	exsrt_archivum_crea
	cmp	rax, -EXSFX_EXDEV
	jne	.malum

	; ---- create, write, read back, in the harness's scratch directory --
	gradus 42				; argv[0], absolute, minus its last part
	mov	rax, [r13]			; rsp0
	mov	rsi, [rax + 8]			; argv[0]
	cmp	byte [rsi], '/'
	jne	.malum
	xor	ecx, ecx
	xor	edx, edx			; index of the last '/'
  .argv0:
	mov	al, [rsi + rcx]
	test	al, al
	jz	.argv0_finis
	cmp	al, '/'
	jne	.argv0_proximus
	mov	rdx, rcx
  .argv0_proximus:
	inc	rcx
	jmp	.argv0
  .argv0_finis:
	test	rdx, rdx
	jnz	.argv0_longitudo
	inc	rdx				; the binary is in `/`: "/" itself
  .argv0_longitudo:
	mov	[rbp - F_VIA], rsi
	mov	[rbp - F_VIA + 8], rdx
	gradus 43
	lea	rdi, [rbp - F_VIA]
	call	exsrt_archivum_radix
	test	rax, rax
	js	.malum
	mov	[rbp - F_DW], rax
	gradus 44
	mov	edi, [rbp - F_DW]
	lea	rsi, [t_novum]
	call	exsrt_archivum_crea
	test	rax, rax
	js	.malum
	mov	[rbp - F_W], rax
	gradus 45
	mov	edi, [rbp - F_W]
	mov	rsi, [t_SCRIPTUM]
	mov	rdx, [t_SCRIPTUM + 8]
	call	exsrt_archivum_scribe
	cmp	rax, [t_SCRIPTUM + 8]
	jne	.malum
	gradus 46				; once, and never again
	mov	edi, [rbp - F_DW]
	lea	rsi, [t_novum]
	call	exsrt_archivum_crea
	cmp	rax, -EXSFX_EEXIST
	jne	.malum
	gradus 47
	mov	edi, [rbp - F_DW]
	lea	rsi, [t_novum]
	lea	rdx, [t_SCRIPTUM]
	lea	rcx, [rbp - F_BUF]
	call	exsfx_lege_totum
	test	rax, rax
	jnz	.malum
	gradus 48
	mov	edi, [rbp - F_DW]
	lea	rsi, [t_supra_novum]
	call	exsrt_archivum_crea
	cmp	rax, -EXSFX_EXDEV
	jne	.malum

	; ---- the descriptor routines report errors as -errno ---------------
	gradus 49				; write to a read-only descriptor
	mov	edi, [rbp - F_FA]
	mov	rsi, [t_SCRIPTUM]
	mov	rdx, [t_SCRIPTUM + 8]
	call	exsrt_archivum_scribe
	cmp	rax, -EXSFX_EBADF
	jne	.malum
	gradus 50
	mov	edi, -1
	lea	rsi, [rbp - F_BUF]
	mov	edx, 64
	call	exsrt_archivum_lege
	cmp	rax, -EXSFX_EBADF
	jne	.malum

	gradus 51				; no callee-saved register clobbered
	mov	rax, EXSFX_SENTINEL_B
	cmp	rbx, rax
	jne	.malum
	mov	rax, EXSFX_SENTINEL_F
	cmp	r15, rax
	jne	.malum

	xor	eax, eax
	jmp	.exi
  .malum:
	mov	eax, r12d
  .exi:
	add	rsp, F_SIZE
	pop	r15
	pop	r14
	pop	r13
	pop	r12
	pop	rbx
	pop	rbp
	ret

segment readable
include '../../compiler/x86_64/prelude/archivum_rodata.asm'

textus_fontis	t_radix, '../data/archivum/radix'
textus_fontis	t_parens, '../data/archivum'
textus_fontis	t_foris_absoluta, '../data/archivum/foris.txt'
textus_fontis	t_radix_nexus_absolutus, '../data/archivum/radix/nexus_absolutus'
textus_fontis	t_nusquam, '../data/archivum/nusquam'
textus	t_intus, 'intus.txt'
textus	t_sub_supra_intus, 'sub/../intus.txt'
textus	t_nexus_intus, 'nexus_intus'
textus	t_supra_foris, '../foris.txt'
textus	t_radix_supra_foris, 'radix/../foris.txt'
textus	t_nexus_foras, 'nexus_foras'
textus	t_radix_nexus_foras, 'radix/nexus_foras'
textus	t_foris, 'foris.txt'
textus	t_nexus_absolutus, 'nexus_absolutus'
textus	t_sub, 'sub'
textus	t_profundum, 'profundum.txt'
textus	t_supra_intus, '../intus.txt'
textus	t_proc_self_fd, '/proc/self/fd'
textus	t_nulla, '0'
textus	t_proc_self_cwd, '/proc/self/cwd'
textus	t_proc_self_fd_0, '/proc/self/fd/0'
textus	t_solum, '/'
textus	t_proc_self_status, 'proc/self/status'
textus	t_abs_proc_self, '/proc/self'
textus	t_status, 'status'
textus	t_relativa, 'data/archivum/radix'
t_vacua:
	dq	t_intus, 0			; "": the empty path
textus	t_nul_intra, '/proc', 0, '/self'
textus	t_supra_nova, '../foris_nova.txt'
textus	t_novum, 'prelude_archivum.novum'
textus	t_supra_novum, '../prelude_archivum.novum'
textus	t_INTUS, 'INTUS', 10
textus	t_PROFUNDUM, 'PROFUNDUM', 10
textus	t_FORIS, 'FORIS', 10
textus	t_SCRIPTUM, 'SCRIPTUM', 10
; 4096 bytes is one too many (PATH_MAX counts the NUL). The length is all
; the prelude reads before refusing; the pointer is any valid one.
t_longa:
	dq	t_intus, 4096

segment readable writeable
include '../../compiler/x86_64/prelude/prelude_data.asm'
