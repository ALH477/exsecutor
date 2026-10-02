; tests/unit/bfc_face.asm
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
; backend_c fixture for the unit's FACE, `bfc_emit_face`
; (backend_c/face_c.inc, docs/design/c-backend.md D9): the C header and the
; Rust extern block, pinned byte for byte over a hand-written IR module and a
; hand-written `publica` mask, with no front end in the way. The driver's
; half -- reading `publica` off the typed tree -- is tests/run.sh's, against
; tests/c/facies/ (it needs a process and real files).
;
; WHAT EACH FUNCTION OF THE MODULE PROVES:
;
;   @importa     `externus`, and its mask byte says PUBLICA anyway. An
;                import is not an export: absent from both faces.
;   @aperta      u8, i16, ptr -> u32: three integer widths and an address.
;                Every integer is `uint64_t` / `u64` (the carrier, D4); the
;                IR-signature comment above it is what keeps the widths.
;   @clausa      mask PRIVATA only: absent.
;   @nulla       mask 0 (no declaration claimed it -- a prelude-shaped
;                name): absent.
;   @fluens      f32, f64 -> f64: `float`/`double`, `f32`/`f64`.
;   @vacua       no parameter, void result: `(void)` in C (never `()`, an
;                unprototyped declaration in C11) and no arrow in Rust.
;   @Typus.m     a name that needs D5's mangling: the face spells it
;                `exs_Typus_2Em`, exactly as the unit does.
;
; AND: the include guard is `EXSECUTOR_FACIES_` + FNV-1a 64 of the
; declaration block, so the expected text below pins the hash too; it was
; checked against an independent FNV-1a in Python when this fixture was
; written. Two emissions of each face must be byte-identical (D5).
;
; Mutation (run) -- each of these was tried and each failed here:
;   the EXTERNUS skip deleted                 -> 21 (@importa appears)
;   the PUBLICA test deleted                  -> 4: @clausa's PRIVATA bit
;                                                then reaches the ambiguity
;                                                refusal (bfc_face_ambigua.asm
;                                                pins that refusal itself)
;   `__bfc_face_rstype`'s f32 arm -> f64        -> 22
;
; Exit 0 = both faces match exactly and reproduce. 21 = the C header does
; not match (text to stderr), 22 = the Rust face does not, 23 = a second
; emission differs from the first, 11 = setup.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/span.inc'
; backend_c/ hangs off the reference backend's chain (emit_c.inc's header),
; so the consumer brings verify.inc; program_c.inc brings emit_c.inc and
; face_c.inc.
include '../../compiler/x86_64/backend_fasmg/verify.inc'
include '../../compiler/x86_64/backend_c/program_c.inc'

segment readable executable
  start:
	lea	rdi, [ar]
	mov	rsi, 16777216
	call	arena_init
	jc	.fail1
	lea	rdi, [scr]
	mov	rsi, 1048576
	call	arena_init
	jc	.fail1

	lea	rdi, [ar]
	lea	rsi, [scr]
	lea	rdx, [srctext]
	mov	rcx, srctext.len
	call	bfa_parse_module
	mov	[modout], rax
	mov	rdi, [rax + BfaModule.funcs]
	cmp	qword [rdi + Vec.len], FACE_NFUNCS
	jne	.fail1

	; ---- the C header, twice ---------------------------------------------
	mov	rdi, [modout]
	lea	rsi, [ar]
	lea	rdx, [mask]
	mov	ecx, BFC_FACIES_H
	call	bfc_emit_face
	mov	[outptr], rax
	mov	[outlen], rdx
	mov	rdi, [outptr]
	mov	rsi, [outlen]
	lea	rdx, [expected_h]
	mov	rcx, expected_h.len
	call	__bfa_streq
	test	eax, eax
	jz	.fail21
	mov	rdi, [modout]
	lea	rsi, [ar]
	lea	rdx, [mask]
	mov	ecx, BFC_FACIES_H
	call	bfc_emit_face
	mov	rdi, rax
	mov	rsi, rdx
	lea	rdx, [expected_h]
	mov	rcx, expected_h.len
	call	__bfa_streq
	test	eax, eax
	jz	.fail23

	; ---- the Rust face, twice --------------------------------------------
	mov	rdi, [modout]
	lea	rsi, [ar]
	lea	rdx, [mask]
	mov	ecx, BFC_FACIES_RS
	call	bfc_emit_face
	mov	[outptr], rax
	mov	[outlen], rdx
	mov	rdi, [outptr]
	mov	rsi, [outlen]
	lea	rdx, [expected_rs]
	mov	rcx, expected_rs.len
	call	__bfa_streq
	test	eax, eax
	jz	.fail22
	mov	rdi, [modout]
	lea	rsi, [ar]
	lea	rdx, [mask]
	mov	ecx, BFC_FACIES_RS
	call	bfc_emit_face
	mov	rdi, rax
	mov	rsi, rdx
	lea	rdx, [expected_rs]
	mov	rcx, expected_rs.len
	call	__bfa_streq
	test	eax, eax
	jz	.fail23

	mov	eax, 231
	xor	edi, edi
	syscall

  .fail1:
	mov	eax, 231
	mov	edi, 11
	syscall
  .fail21:
	mov	edi, 2
	mov	rsi, [outptr]
	mov	rdx, [outlen]
	call	sys_write
	mov	eax, 231
	mov	edi, 21
	syscall
  .fail22:
	mov	edi, 2
	mov	rsi, [outptr]
	mov	rdx, [outlen]
	call	sys_write
	mov	eax, 231
	mov	edi, 22
	syscall
  .fail23:
	mov	eax, 231
	mov	edi, 23
	syscall

segment readable writeable
  ar:     rb sizeof.Arena
  scr:    rb sizeof.Arena
  modout: dq 0
  outptr: dq 0
  outlen: dq 0

  ; one byte per function, Module.funcs order (the driver's
  ; __drv_publica_mask builds this from the typed tree)
  mask:
	db BFC_FACE_PUBLICA			; @importa (externus)
	db BFC_FACE_PUBLICA			; @aperta
	db BFC_FACE_PRIVATA			; @clausa
	db 0					; @nulla
	db BFC_FACE_PUBLICA			; @fluens
	db BFC_FACE_PUBLICA			; @vacua
	db BFC_FACE_PUBLICA			; @Typus.m
  FACE_NFUNCS = $ - mask

  srctext:
  db "functio @importa (ptr) -> u64 externus sysv_amd64 {", 10
  db "}", 10
  db "functio @aperta (u8 i16 ptr) -> u32 {", 10
  db "b0:", 10
  db "%0 = param u8 0", 10
  db "%1 = param i16 1", 10
  db "%2 = param ptr 2", 10
  db "%3 = zext u32 %0", 10
  db "ret %3", 10
  db "}", 10
  db "functio @clausa (u64) -> u64 {", 10
  db "b0:", 10
  db "%0 = param u64 0", 10
  db "ret %0", 10
  db "}", 10
  db "functio @nulla (u64) -> u64 {", 10
  db "b0:", 10
  db "%0 = param u64 0", 10
  db "ret %0", 10
  db "}", 10
  db "functio @fluens (f32 f64) -> f64 {", 10
  db "b0:", 10
  db "%0 = param f32 0", 10
  db "%1 = param f64 1", 10
  db "ret %1", 10
  db "}", 10
  db "functio @vacua () -> void {", 10
  db "b0:", 10
  db "ret", 10
  db "}", 10
  db "functio @Typus.m (ptr) -> void {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "ret", 10
  db "}", 10
  .len = $ - srctext

  expected_h:
  db "/* exsecutor: the C face of a library unit (exsc --emitte h). Generated,", 10
  db " * never hand-edited: one prototype per `publica` function the unit defines,", 10
  db " * spelled exactly as the unit's own prototype, then the one symbol the unit", 10
  db " * imports. The host supplies exsrt_abortus; only a trap reaches it, and it", 10
  db " * must not return. Above each prototype, its IR signature: a uint64_t carries", 10
  db " * the named width in canonical form (a uN zero-extended, an iN sign-extended);", 10
  db " * a value outside that width is outside the contract, and what the unit does", 10
  db " * with one is unspecified. docs/design/c-backend.md D1, D5, D9. */", 10
  db "#ifndef EXSECUTOR_FACIES_CD3313F2B98FBAFA_H", 10
  db "#define EXSECUTOR_FACIES_CD3313F2B98FBAFA_H", 10
  db "#include <stdint.h>", 10
  db 10
  db "/* (u8, i16, ptr) -> u32 */", 10
  db "uint64_t exs_aperta(uint64_t p0, uint64_t p1, unsigned char *p2);", 10
  db "/* (f32, f64) -> f64 */", 10
  db "double exs_fluens(float p0, double p1);", 10
  db "/* () -> void */", 10
  db "void exs_vacua(void);", 10
  db "/* (ptr) -> void */", 10
  db "void exs_Typus_2Em(unsigned char *p0);", 10
  db 10
  db "_Noreturn void exsrt_abortus(unsigned kind);", 10
  db "#endif", 10
  .len = $ - expected_h

  expected_rs:
  db "// exsecutor: the Rust face of a library unit (exsc --emitte rs). Generated,", 10
  db "// never hand-edited: one declaration per `publica` function the unit", 10
  db "// defines, in the unit's own C ABI -- uint64_t is u64, unsigned char * is", 10
  db "// *mut u8, float is f32, double is f64. The unit imports one symbol, which", 10
  db "// the host supplies and which must not return:", 10
  db "//     _Noreturn void exsrt_abortus(unsigned kind);", 10
  db "//     #[no_mangle] pub extern ", '"', "C", '"', " fn exsrt_abortus(kind: u32) -> ! { ... }", 10
  db "// Above each declaration, its IR signature: a u64 carries the named width in", 10
  db "// canonical form (a uN zero-extended, an iN sign-extended); a value outside", 10
  db "// that width is outside the contract, and what the unit does with one is", 10
  db "// unspecified. docs/design/c-backend.md D1, D5, D9.", 10
  db "unsafe extern ", '"', "C", '"', " {", 10
  db "    // (u8, i16, ptr) -> u32", 10
  db "    pub fn exs_aperta(p0: u64, p1: u64, p2: *mut u8) -> u64;", 10
  db "    // (f32, f64) -> f64", 10
  db "    pub fn exs_fluens(p0: f32, p1: f64) -> f64;", 10
  db "    // () -> void", 10
  db "    pub fn exs_vacua();", 10
  db "    // (ptr) -> void", 10
  db "    pub fn exs_Typus_2Em(p0: *mut u8);", 10
  db "}", 10
  .len = $ - expected_rs
