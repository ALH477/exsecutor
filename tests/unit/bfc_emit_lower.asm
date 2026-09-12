; tests/unit/bfc_emit_lower.asm
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
; backend_c fixture: the emitted C TEXT, pinned exactly, for one function per
; family of docs/design/c-backend.md D4's table. The mirror of
; bfa_emit_narrow.asm and bfa_emit_tier1.asm, and it exists for the same
; reason they do -- the differential phase proves the emitted C BEHAVES
; correctly, and behaviour is preserved by a great many rewrites of the text.
; This pins the text, so a change to a lowering is visible as a change here
; rather than as nothing at all.
;
; `bfc_emit_module`, not `bfc_emit_unit`: the prologue and the helpers are a
; fixed blob carried verbatim from prologue.c.in, and putting 130 lines of
; unchanging C in front of every expectation would bury what is being pinned.
; The blob's own text is checked where it matters -- by four C compilers, in
; the differential phase.
;
; What each function pins:
;
;   @arith    D4 rows 1-3, 6-10: the three SHAPES a binary integer opcode
;             takes. HELPER (`exsi_add_u(a, b, 8)`) for the trapping and
;             saturating rows; WRAP (`exsi_norm_u(a + b, 8)`) for the
;             wrapping ones, which need no helper because the low N bits of
;             a modular 64-bit sum are the low N bits of the true one; and
;             the signed/unsigned suffix chosen from the TYPE'S KIND, never
;             from `BfaType.sign`. Also that a u64 row carries NO
;             normalisation at all: exsi_norm_u(x, 64) is the identity, so
;             emitting it would be noise the reference does not emit either.
;   @bits     D4 rows 15-19: `&`/`|`/`^` bare (canonical in, canonical out);
;             `exsi_shl_u`/`exsi_shr_i`, where the trap on a count >= the
;             width lives in the helper.
;   @conv     D4 rows 21-23: zext/sext/trunc, and the ORDER that makes
;             `sext u16` of an `i8` holding -56 come out 65480 -- extend from
;             the SOURCE width first, then cut to the destination's form.
;             Two nested `exsi_norm_*` calls, never one.
;   @compare  D4 row 20: an unsigned compare bare; a SIGNED compare as an
;             unsigned compare of two sign-bit-flipped patterns, which is why
;             no signed C type appears in emitted arithmetic; a `ptr`
;             compare through `uintptr_t`, because C leaves `<` between
;             pointers into different objects undefined where the reference
;             merely compares two addresses.
;   @flow     D4 rows 53-57: a `br` with its two arms, the `goto`s, the
;             function-scope phi temporaries, and the PARALLEL COPY on each
;             edge -- every read (`tN = ...`) before every write
;             (`vN = tN`), which is what makes phi_swap.ir and
;             phi_lost_copy.ir correct with no cycle analysis. Also that the
;             ENTRY block gets no label (nothing can `goto` it, and an
;             unused label is a -Wall warning) while a block with a
;             predecessor does.
;   @memoria  D4 rows 39-48: `slot` as an `_Alignas` array declared at the
;             top and an assignment at the instruction; `load`/`store` in all
;             three byte orders through the byte-at-a-time helpers;
;             `loadbits`/`storebits` in `unsigned` arithmetic with the byte
;             cast BEFORE the shift; `chk`; `index` and `addr` as integer
;             arithmetic on the address rather than C pointer arithmetic;
;             `copy` as `__builtin_memcpy`.
;   @vocat    D4 row 51 and 58/59: a call to a DEFINED function (mangled,
;             `exs_`-prefixed) and to a BODILESS one (verbatim, because the
;             linker must find it); the narrow-result normalisation that
;             only an `externus` callee gets; `(void)vN;` after a result
;             nothing reads; `(void)pN;` for a parameter nothing reads.
;
; Exit 0 = the emitted text matches exactly. 12 = it does not, and the text
; goes to stderr first so a diff can be read off the run log.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/span.inc'
; backend_c/ hangs off the reference backend's chain rather than duplicating
; its tail (emit_c.inc's header), so the consumer brings verify.inc.
include '../../compiler/x86_64/backend_fasmg/verify.inc'
include '../../compiler/x86_64/backend_c/emit_c.inc'

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

	; ---- the one thing the emitted text CANNOT show -----------------------
	; `__bfc_ty_signed` must read `BfaType.KIND` and never `BfaType.sign`.
	; Every type in `srctext` came from the PARSER, which interns `iN` with
	; sign = 1 -- so for a parsed module the two fields agree and the text
	; below is identical either way. `lower/ty.inc` interns `iN` with
	; sign = 0 (its own comment records the defect that cost: a signed `+`
	; checking CF instead of OF, a signed `lt` emitting `setb`), so on the
	; path that matters -- exsc's -- reading `.sign` answers "unsigned" for
	; EVERY signed type. A mutation of `__bfc_ty_signed` from `.kind` to
	; `.sign` survived this fixture's text comparison, which is why this
	; check exists: it builds the lowering's spelling directly and asks.
	mov	rdi, [modout]
	mov	esi, BFA_TK_I
	xor	edx, edx		; sign 0 -- the LOWERING's spelling of i8
	mov	ecx, 8
	xor	r8, r8
	call	bfa_type_intern
	mov	rdi, [modout]
	mov	esi, eax
	call	__bfc_ty_signed
	test	eax, eax
	jz	.fail3			; answered "unsigned" for an i8

	mov	rdi, [modout]
	lea	rsi, [ar]
	call	bfc_emit_module
	mov	[outptr], rax
	mov	[outlen], rdx

	mov	rdi, [outptr]
	mov	rsi, [outlen]
	lea	rdx, [expected]
	mov	rcx, expected.len
	call	__bfa_streq
	test	eax, eax
	jz	.fail2

	mov	eax, 231
	xor	edi, edi
	syscall

  .fail1:
	mov	eax, 231
	mov	edi, 11
	syscall
  .fail2:
	mov	edi, 2
	mov	rsi, [outptr]
	mov	rdx, [outlen]
	call	sys_write
	mov	eax, 231
	mov	edi, 12
	syscall
  .fail3:
	; __bfc_ty_signed answered "unsigned" for an i8 interned the way
	; lower/ty.inc interns one. See the check itself for why that is fatal.
	mov	edi, 2
	lea	rsi, [m_sign]
	mov	rdx, m_sign.len
	call	sys_write
	mov	eax, 231
	mov	edi, 13
	syscall

segment readable writeable
  ar:     rb sizeof.Arena
  scr:    rb sizeof.Arena
  modout: dq 0
  outptr: dq 0
  outlen: dq 0
  m_sign: db "bfc_emit_lower: __bfc_ty_signed read BfaType.sign, not BfaType.kind: it answers `unsigned` for every signed type lower/ty.inc builds (emit.inc's own header records what that cost)", 10
  .len = $ - m_sign

  srctext:
  db "data $0 4 1 deadbeef", 10
  db "functio @externa (ptr) -> u8 externus sysv_amd64 {", 10
  db "}", 10
  db "functio @arith (u8 u8 u64) -> u8 {", 10
  db "b0:", 10
  db "%0 = param u8 0", 10
  db "%1 = param u8 1", 10
  db "%2 = param u64 2", 10
  db "%3 = add u8 %0 %1", 10
  db "%4 = addw u8 %3 %1", 10
  db "%5 = sub u8 %4 %1", 10
  db "%6 = mulw u8 %5 3", 10
  db "%7 = adds u8 %6 %1", 10
  db "%8 = subs u8 %7 %1", 10
  db "%9 = mul u8 %8 1", 10
  db "%10 = addw u64 %2 %2", 10
  db "ret %9", 10
  db "}", 10
  db "functio @bits (u8 u8) -> u8 {", 10
  db "b0:", 10
  db "%0 = param u8 0", 10
  db "%1 = param u8 1", 10
  db "%2 = and u8 %0 %1", 10
  db "%3 = or u8 %2 %1", 10
  db "%4 = xor u8 %3 %1", 10
  db "%5 = shl u8 %4 2", 10
  db "%6 = shr u8 %5 1", 10
  db "ret %6", 10
  db "}", 10
  db "functio @conv (i8) -> u16 {", 10
  db "b0:", 10
  db "%0 = param i8 0", 10
  db "%1 = sext u16 %0", 10
  db "%2 = zext u32 %1", 10
  db "%3 = trunc u16 %2", 10
  db "ret %3", 10
  db "}", 10
  db "functio @compare (i8 u8 ptr ptr) -> u1 {", 10
  db "b0:", 10
  db "%0 = param i8 0", 10
  db "%1 = param u8 1", 10
  db "%2 = param ptr 2", 10
  db "%3 = param ptr 3", 10
  db "%4 = cmp.lt u8 %1 %1", 10
  db "%5 = cmp.ge i8 %0 %0", 10
  db "%6 = cmp.lt ptr %2 %3", 10
  db "%7 = and u1 %4 %5", 10
  db "%8 = and u1 %7 %6", 10
  db "ret %8", 10
  db "}", 10
  db "functio @flow (u64) -> u64 {", 10
  db "b0:", 10
  db "%0 = param u64 0", 10
  db "%1 = iconst u64 0", 10
  db "jmp b1", 10
  db "b1:", 10
  db "%2 = phi u64 b0 %1 b2 %5", 10
  db "%3 = phi u64 b0 %0 b2 %2", 10
  db "%4 = cmp.lt u64 %2 %0", 10
  db "br %4 b2 b3", 10
  db "b2:", 10
  db "%5 = addw u64 %2 1", 10
  db "jmp b1", 10
  db "b3:", 10
  db "ret %3", 10
  db "}", 10
  db "functio @memoria (ptr u64) -> u32 {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = param u64 1", 10
  db "%2 = slot 16 4", 10
  db "%3 = iconst u64 4", 10
  db "chk %1 %3", 10
  db "%4 = index %0 %1 4", 10
  db "%5 = load u32 %4 0 nativus", 10
  db "%6 = load u32 %4 0 maior", 10
  db "%7 = load u32 %4 0 minor", 10
  db "store u32 %2 0 minor %6", 10
  db "%8 = addr %2 4", 10
  db "%9 = loadbits u3 %8 0 1", 10
  db "storebits u3 %8 0 1 %9", 10
  db "copy 8 %2 %0", 10
  db "%10 = gaddr 0", 10
  db "store ptr %2 8 nativus %10", 10
  db "ret %7", 10
  db "}", 10
  db "functio @vocat (ptr ptr) -> u8 {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = call u8 @externa %0", 10
  db "%2 = call u8 @arith %1 %1 %1", 10
  db "ret %2", 10
  db "}", 10
  .len = $ - srctext

  expected:
  db "uint64_t externa(unsigned char *p0);", 10
  db "uint64_t exs_arith(uint64_t p0, uint64_t p1, uint64_t p2);", 10
  db "uint64_t exs_bits(uint64_t p0, uint64_t p1);", 10
  db "uint64_t exs_conv(uint64_t p0);", 10
  db "uint64_t exs_compare(uint64_t p0, uint64_t p1, unsigned char *p2, unsigned char *p3);", 10
  db "uint64_t exs_flow(uint64_t p0);", 10
  db "uint64_t exs_memoria(unsigned char *p0, uint64_t p1);", 10
  db "uint64_t exs_vocat(unsigned char *p0, unsigned char *p1);", 10
  db 10
  db "uint64_t exs_arith(uint64_t p0, uint64_t p1, uint64_t p2)", 10
  db "{", 10
  db "    uint64_t v1;", 10
  db "    uint64_t v2;", 10
  db "    uint64_t v3;", 10
  db "    uint64_t v4;", 10
  db "    uint64_t v5;", 10
  db "    uint64_t v6;", 10
  db "    uint64_t v7;", 10
  db "    uint64_t v8;", 10
  db "    uint64_t v9;", 10
  db "    uint64_t v10;", 10
  db "    uint64_t v11;", 10
  db "    v1 = p0;", 10
  db "    v2 = p1;", 10
  db "    v3 = p2;", 10
  db "    v4 = exsi_add_u(v1, v2, 8);", 10
  db "    v5 = exsi_norm_u(v4 + v2, 8);", 10
  db "    v6 = exsi_sub_u(v5, v2, 8);", 10
  db "    v7 = exsi_norm_u(v6 * UINT64_C(3), 8);", 10
  db "    v8 = exsi_adds_u(v7, v2, 8);", 10
  db "    v9 = exsi_subs_u(v8, v2, 8);", 10
  db "    v10 = exsi_mul_u(v9, UINT64_C(1), 8);", 10
  db "    v11 = v3 + v3;", 10
  db "    (void)v11;", 10
  db "    return v10;", 10
  db "}", 10
  db 10
  db "uint64_t exs_bits(uint64_t p0, uint64_t p1)", 10
  db "{", 10
  db "    uint64_t v1;", 10
  db "    uint64_t v2;", 10
  db "    uint64_t v3;", 10
  db "    uint64_t v4;", 10
  db "    uint64_t v5;", 10
  db "    uint64_t v6;", 10
  db "    uint64_t v7;", 10
  db "    v1 = p0;", 10
  db "    v2 = p1;", 10
  db "    v3 = v1 & v2;", 10
  db "    v4 = v3 | v2;", 10
  db "    v5 = v4 ^ v2;", 10
  db "    v6 = exsi_shl_u(v5, UINT64_C(2), 8);", 10
  db "    v7 = exsi_shr_u(v6, UINT64_C(1), 8);", 10
  db "    return v7;", 10
  db "}", 10
  db 10
  db "uint64_t exs_conv(uint64_t p0)", 10
  db "{", 10
  db "    uint64_t v1;", 10
  db "    uint64_t v2;", 10
  db "    uint64_t v3;", 10
  db "    uint64_t v4;", 10
  db "    v1 = p0;", 10
  db "    v2 = exsi_norm_u(exsi_norm_i(v1, 8), 16);", 10
  db "    v3 = exsi_norm_u(exsi_norm_u(v2, 16), 32);", 10
  db "    v4 = exsi_norm_u(v3, 16);", 10
  db "    return v4;", 10
  db "}", 10
  db 10
  db "uint64_t exs_compare(uint64_t p0, uint64_t p1, unsigned char *p2, unsigned char *p3)", 10
  db "{", 10
  db "    uint64_t v1;", 10
  db "    uint64_t v2;", 10
  db "    unsigned char *v3;", 10
  db "    unsigned char *v4;", 10
  db "    uint64_t v5;", 10
  db "    uint64_t v6;", 10
  db "    uint64_t v7;", 10
  db "    uint64_t v8;", 10
  db "    uint64_t v9;", 10
  db "    v1 = p0;", 10
  db "    v2 = p1;", 10
  db "    v3 = p2;", 10
  db "    v4 = p3;", 10
  db "    v5 = (v2 < v2);", 10
  db "    v6 = ((v1 ^ UINT64_C(9223372036854775808)) >= (v1 ^ UINT64_C(9223372036854775808)));", 10
  db "    v7 = ((uintptr_t)v3 < (uintptr_t)v4);", 10
  db "    v8 = v5 & v6;", 10
  db "    v9 = v8 & v7;", 10
  db "    return v9;", 10
  db "}", 10
  db 10
  db "uint64_t exs_flow(uint64_t p0)", 10
  db "{", 10
  db "    uint64_t v1;", 10
  db "    uint64_t v2;", 10
  db "    uint64_t v4;", 10
  db "    uint64_t v5;", 10
  db "    uint64_t v6;", 10
  db "    uint64_t v8;", 10
  db "    uint64_t t4;", 10
  db "    uint64_t t5;", 10
  db "    v1 = p0;", 10
  db "    v2 = UINT64_C(0);", 10
  db "    t4 = v2;", 10
  db "    t5 = v1;", 10
  db "    v4 = t4;", 10
  db "    v5 = t5;", 10
  db "    goto b2;", 10
  db "b2:", 10
  db "    v6 = (v4 < v1);", 10
  db "    if (v6) {", 10
  db "        goto b3;", 10
  db "    } else {", 10
  db "        goto b4;", 10
  db "    }", 10
  db "b3:", 10
  db "    v8 = v4 + UINT64_C(1);", 10
  db "    t4 = v8;", 10
  db "    t5 = v4;", 10
  db "    v4 = t4;", 10
  db "    v5 = t5;", 10
  db "    goto b2;", 10
  db "b4:", 10
  db "    return v5;", 10
  db "}", 10
  db 10
  db "uint64_t exs_memoria(unsigned char *p0, uint64_t p1)", 10
  db "{", 10
  db "    unsigned char *v1;", 10
  db "    uint64_t v2;", 10
  db "    unsigned char *v3;", 10
  db "    uint64_t v4;", 10
  db "    unsigned char *v6;", 10
  db "    uint64_t v7;", 10
  db "    uint64_t v8;", 10
  db "    uint64_t v9;", 10
  db "    unsigned char *v11;", 10
  db "    uint64_t v12;", 10
  db "    unsigned char *v15;", 10
  db "    _Alignas(4) unsigned char s3[16];", 10
  db "    v1 = p0;", 10
  db "    v2 = p1;", 10
  db "    v3 = s3;", 10
  db "    v4 = UINT64_C(4);", 10
  db "    if (v2 >= v4) exsrt_abortus(1);", 10
  db "    v6 = (unsigned char *)((uintptr_t)v1 + (uintptr_t)v2 * 4);", 10
  db "    v7 = exsi_norm_u(exsi_ld_n(v6 + 0, 4), 32);", 10
  db "    (void)v7;", 10
  db "    v8 = exsi_norm_u(exsi_ld_be(v6 + 0, 4), 32);", 10
  db "    v9 = exsi_norm_u(exsi_ld_le(v6 + 0, 4), 32);", 10
  db "    exsi_st_le(v3 + 0, 4, v8);", 10
  db "    v11 = (unsigned char *)((uintptr_t)v3 + 4);", 10
  db "    v12 = (((unsigned)v11[0] >> 4) & 7u);", 10
  db "    v11[0] = (unsigned char)(((unsigned)v11[0] & ~(7u << 4)) | (((unsigned)v12 & 7u) << 4));", 10
  db "    __builtin_memcpy(v3, v1, 8);", 10
  db "    v15 = (unsigned char *)(uintptr_t)exsi_g0;", 10
  db "    exsi_st_n(v3 + 8, sizeof(void *), (uint64_t)(uintptr_t)v15);", 10
  db "    return v9;", 10
  db "}", 10
  db 10
  db "uint64_t exs_vocat(unsigned char *p0, unsigned char *p1)", 10
  db "{", 10
  db "    unsigned char *v1;", 10
  db "    uint64_t v2;", 10
  db "    uint64_t v3;", 10
  db "    (void)p1;", 10
  db "    v1 = p0;", 10
  db "    v2 = exsi_norm_u(externa(v1), 8);", 10
  db "    v3 = exs_arith(v2, v2, v2);", 10
  db "    return v3;", 10
  db "}", 10
  db 10
  .len = $ - expected
