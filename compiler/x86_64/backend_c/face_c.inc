; compiler/x86_64/backend_c/face_c.inc
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
; The FACE of a library unit -- `--emitte h` and `--emitte rs`,
; docs/design/c-backend.md D9. `bfc_emit_face` writes, from the SAME module
; and the SAME signature routine `--emitte c` uses, the declarations a host
; needs to call the unit: a C header, or a Rust `extern "C"` block.
;
; WHY IT EXISTS. The first two consumers of a library unit (examples/custos,
; linked by Punctim's web bridge; examples/arca, linked by Oligarchy's
; reliquary) each needed a HAND-WRITTEN C header and a hand-written Rust
; extern block, and a drifted signature was caught only by force-including
; the hand-written header into the generated unit. A face generated from the
; prototype the unit itself is built from cannot drift from it.
;
; ONE SIGNATURE ROUTINE. The C face's prototypes are `__bfc_emit_sig`'s
; output -- the routine that already writes both the unit's prototype and
; its definition line -- so header and unit cannot disagree about a
; parameter's C type, its order, or the mangled name. The Rust face
; re-spells the same `BfaSig` through a four-row table (below) and is held
; to the C face by compiling both against one unit (tests/run.sh).
;
; WHICH FUNCTIONS. A function is in the face iff it is DEFINED by the unit
; (not `externus`, not a prelude routine -- those are imports, not exports)
; and its declaration is `publica`. The IR does not carry `publica`; the
; driver reads it off the typed tree and hands this file a byte per IR
; function (`pubmask`, bits below). Order is `Module.funcs` order, the
; unit's own prototype order (D5), so the face is a function of the module
; and of exsc's bytes and nothing else: no path, no clock, no address.
;
; THE ONE IMPORT. The C face also declares `exsrt_abortus`, the trap hook the
; unit imports and the host supplies (D1). The Rust face names it in a
; comment instead of declaring it: a Rust host DEFINES that symbol, and a
; declaration beside a definition is noise at best.
;
; THE INCLUDE GUARD is derived from the declarations themselves: FNV-1a 64
; (rt/map.inc's `map_hash`) over the prototype block, as 16 uppercase hex
; digits -- `EXSECUTOR_FACIES_<hash>_H`. Two units with the same exported
; prototypes get the same guard, which is right: their declarations are the
; same text. Not the output path (D5: no path in emitted text), not a module
; name (there is no module name; spec 12's unit is a list of files).
;
; REFUSALS, BY NAME, exit 4 (`__bfc_die`): a parameter or result with no
; face spelling -- a vector float or anything of class 2 -- and two IR
; functions behind one name of which one is `publica` and one is not (the
; driver's name map could not then say which one the face means).
;
; NO SYSCALL. Bytes into an arena; the driver writes OUT.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md section 9.2 (library mode), 9.3,
; 12; docs/design/c-backend.md D1, D5, D9.
; -----------------------------------------------------------------------------

BFC_FACIES_H  = 0	; --emitte h:  a C11 header
BFC_FACIES_RS = 1	; --emitte rs: a Rust `extern "C"` block

; `pubmask` bits, one byte per IR function (0-based, Module.funcs order).
BFC_FACE_PUBLICA = 1	; a `publica` declaration carries this name
BFC_FACE_PRIVATA = 2	; a declaration WITHOUT `publica` carries this name

; __bfc_face_ctx(arena) -> rax = a fresh BfaPrintCtx with its own buffer.
proc __bfc_face_ctx, arena
	locals
		slot bfcf_c, dq
	endl
	mov	rdi, [arena]
	mov	rsi, sizeof.BfaPrintCtx
	call	arena_alloc
	mov	[bfcf_c], rax
	mov	dword [rax + BfaPrintCtx.curblk], 0
	mov	rdi, [arena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	rcx, [bfcf_c]
	mov	[rcx + BfaPrintCtx.obuf], rax
	mov	rdi, rax
	mov	rsi, [arena]
	mov	rdx, 1
	mov	rcx, 4096
	call	vec_init
	mov	rax, [bfcf_c]
	return
endp

; __bfc_face_refuse -- never returns: a parameter or result type with no
; spelling in either face. One message, shared by both faces.
proc __bfc_face_refuse
	locals
	endl
	jmp	.after_msg
  .msg db "face: a publica function's parameter or result has no spelling in a C or Rust face -- an integer (uN/iN), an address (ptr/ref/refc) or a float (f32/f64) crosses the boundary, and a vector float or a handle does not (c-backend.md D9)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfc_die
endp

; __bfc_face_rstype(ctx, module, ty) -- one parameter or result type in
; Rust: u64 / *mut u8 / f32 / f64, the C types `__bfc_emit_ctype` writes
; for the unit (uint64_t / unsigned char * / float / double) by the
; platform C ABI. A type of 0 (void) is only ever a result, and the caller
; spells it. Anything else is refused by name. (The C face needs no such
; table: it is `__bfc_emit_sig`'s own text.)
proc __bfc_face_rstype, ctx, module, ty
	locals
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfc_ty_class
	cmp	eax, 2
	je	.bad
	cmp	eax, 4
	je	.bad
	cmp	eax, 1
	je	.rsaddr
	cmp	eax, 3
	je	.rsfloat
	__bfc_emtxt "u64"
	return
  .rsaddr:
	__bfc_emtxt "*mut u8"
	return
  .rsfloat:
	mov	rdi, [module]
	mov	esi, [ty]
	call	__bfc_ty_info
	cmp	edx, BFA_TK_F32
	je	.rsf32
	__bfc_emtxt "f64"
	return
  .rsf32:
	__bfc_emtxt "f32"
	return
  .bad:
	call	__bfc_face_refuse
endp

; __bfc_face_rssig(ctx, module, fptr) -- one Rust declaration,
; "    pub fn exs_name(p0: *mut u8, p1: u64) -> u64;" -- the parameter names
; are the unit's own (`pI`, D5), so the two faces read alike.
proc __bfc_face_rssig, ctx, module, fptr
	uses	rbx
	locals
		slot bfcf_sp, dq
		slot bfcf_pn, dd
		slot bfcf_pf, dd
		slot bfcf_rt, dd
	endl
	mov	rax, [fptr]
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.sigs]
	mov	esi, [rax + BfaFunc.sig]	; 0-based, unlike every other id
	call	vec_get
	mov	[bfcf_sp], rax
	mov	ecx, [rax + BfaSig.p_count]
	mov	[bfcf_pn], ecx
	mov	ecx, [rax + BfaSig.p_first]
	mov	[bfcf_pf], ecx
	mov	ecx, [rax + BfaSig.ret_ty]
	mov	[bfcf_rt], ecx

	__bfc_emtxt "    pub fn "
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [fptr]
	call	__bfc_emit_fname
	__bfc_emtxt "("
	xor	ebx, ebx
  .ploop:
	cmp	ebx, [bfcf_pn]
	jae	.pdone
	test	ebx, ebx
	jz	.nocomma
	__bfc_emtxt ", "
  .nocomma:
	__bfc_emtxt "p"
	mov	rdi, [ctx]
	mov	esi, ebx
	call	__bfa_out_u64
	__bfc_emtxt ": "
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.sig_extra]
	mov	eax, [bfcf_pf]
	add	eax, ebx
	mov	esi, eax
	call	vec_get
	mov	edx, [rax]
	mov	rdi, [ctx]
	mov	rsi, [module]
	call	__bfc_face_rstype
	inc	ebx
	jmp	.ploop
  .pdone:
	__bfc_emtxt ")"
	cmp	dword [bfcf_rt], 0
	je	.semi			; `void`: Rust spells it by saying nothing
	__bfc_emtxt " -> "
	mov	edx, [bfcf_rt]
	mov	rdi, [ctx]
	mov	rsi, [module]
	call	__bfc_face_rstype
  .semi:
	__bfc_emtxt ";", 10
	return
endp

; __bfc_face_csig(ctx, module, fptr) -- one C prototype. Every type is
; checked first, so a type the face cannot carry is refused with the face's
; own message (`__bfc_face_refuse`) rather than spelled; then the line is
; `__bfc_emit_sig`'s, the unit's own.
proc __bfc_face_csig, ctx, module, fptr
	uses	rbx
	locals
		slot bfcf_sp, dq
		slot bfcf_pn, dd
		slot bfcf_pf, dd
	endl
	mov	rax, [fptr]
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.sigs]
	mov	esi, [rax + BfaFunc.sig]
	call	vec_get
	mov	[bfcf_sp], rax
	mov	ecx, [rax + BfaSig.p_count]
	mov	[bfcf_pn], ecx
	mov	ecx, [rax + BfaSig.p_first]
	mov	[bfcf_pf], ecx

	xor	ebx, ebx
  .chk:
	cmp	ebx, [bfcf_pn]
	jae	.chkret
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.sig_extra]
	mov	eax, [bfcf_pf]
	add	eax, ebx
	mov	esi, eax
	call	vec_get
	mov	esi, [rax]
	mov	rdi, [module]
	call	__bfc_ty_class
	cmp	eax, 2
	je	.refuse
	cmp	eax, 4
	je	.refuse
	inc	ebx
	jmp	.chk
  .chkret:
	mov	rax, [bfcf_sp]
	mov	esi, [rax + BfaSig.ret_ty]
	test	esi, esi
	jz	.spell
	mov	rdi, [module]
	call	__bfc_ty_class
	cmp	eax, 2
	je	.refuse
	cmp	eax, 4
	je	.refuse
  .spell:
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, [fptr]
	call	__bfc_emit_sig
	__bfc_emtxt ";", 10
	return
  .refuse:
	call	__bfc_face_refuse
endp

; __bfc_face_irsig(ctx, module, fptr) -- "(u64, u32) -> u64", the IR
; signature (print.inc's type names), which says what the C and Rust types
; cannot: the WIDTH each 64-bit carrier holds. The unit assumes a parameter
; arrives in canonical form (ssa-ir.md 2.2: a uN zero-extended, an iN
; sign-extended); `__bfc_emit_param` copies `pI` without normalising it, so
; a host passing 300 for a `u8` is outside the contract and gets whatever
; each operation makes of it (measured once: `x sicut u64` of such an `x`
; read back 44, the zero extension masking it -- not a promise any other
; operation keeps). This comment is the only place a host reading the face
; can learn that the parameter is a `u8`.
proc __bfc_face_irsig, ctx, module, fptr
	uses	rbx
	locals
		slot bfcf_sp, dq
		slot bfcf_pn, dd
		slot bfcf_pf, dd
	endl
	mov	rax, [fptr]
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.sigs]
	mov	esi, [rax + BfaFunc.sig]
	call	vec_get
	mov	[bfcf_sp], rax
	mov	ecx, [rax + BfaSig.p_count]
	mov	[bfcf_pn], ecx
	mov	ecx, [rax + BfaSig.p_first]
	mov	[bfcf_pf], ecx
	__bfc_emtxt "("
	xor	ebx, ebx
  .ploop:
	cmp	ebx, [bfcf_pn]
	jae	.pdone
	test	ebx, ebx
	jz	.nocomma
	__bfc_emtxt ", "
  .nocomma:
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.sig_extra]
	mov	eax, [bfcf_pf]
	add	eax, ebx
	mov	esi, eax
	call	vec_get
	mov	esi, [rax]
	mov	rdi, [ctx]
	call	__bfa_print_type
	inc	ebx
	jmp	.ploop
  .pdone:
	__bfc_emtxt ") -> "
	mov	rax, [bfcf_sp]
	mov	esi, [rax + BfaSig.ret_ty]
	mov	rdi, [ctx]
	call	__bfa_print_type
	return
endp

; __bfc_face_decls(ctx, module, pubmask, facies) -- one declaration per
; function that the unit defines and that is `publica`, in Module.funcs
; order.
proc __bfc_face_decls, ctx, module, pubmask, facies
	uses	rbx, r12
	locals
		slot bfcf_n, dd
	endl
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	rax, [rdi + Vec.len]
	mov	[bfcf_n], eax
	xor	ebx, ebx
  .loop:
	cmp	ebx, [bfcf_n]
	jae	.done
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	esi, ebx
	call	vec_get
	mov	r12, [rax]
	; an `externus` function or a prelude routine is an IMPORT: the unit
	; declares it and something else defines it, so it is no part of
	; what the unit offers a host
	test	dword [r12 + BfaFunc.attrs], BFA_FUNC_ATTR_EXTERNUS
	jnz	.next
	mov	rax, [pubmask]
	movzx	ecx, byte [rax + rbx]
	test	ecx, BFC_FACE_PUBLICA
	jz	.next
	test	ecx, BFC_FACE_PRIVATA
	jnz	.ambiguous
	cmp	dword [facies], BFC_FACIES_RS
	je	.rs
	__bfc_emtxt "/* "
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, r12
	call	__bfc_face_irsig
	__bfc_emtxt " */", 10
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, r12
	call	__bfc_face_csig
	jmp	.next
  .rs:
	__bfc_emtxt "    // "
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, r12
	call	__bfc_face_irsig
	__bfc_emtxt 10
	mov	rdi, [ctx]
	mov	rsi, [module]
	mov	rdx, r12
	call	__bfc_face_rssig
  .next:
	inc	ebx
	jmp	.loop
  .done:
	return
  .ambiguous:
	jmp	.after_msg
  .msg db "face: two functions of this unit share one name and only one of them is publica, so the face cannot say which one it exports (c-backend.md D9)"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfc_die
endp

; __bfc_face_guard(ctx, bfcf_h) -- `EXSECUTOR_FACIES_<16 hex>_H`, the hash
; most significant digit first, uppercase (program_c.inc's hex digit).
proc __bfc_face_guard, ctx, bfcf_h
	uses	rbx
	locals
	endl
	__bfc_emtxt "EXSECUTOR_FACIES_"
	mov	ebx, 60
  .gloop:
	mov	rax, [bfcf_h]
	mov	ecx, ebx
	shr	rax, cl
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfc_prog_hexnib
	sub	ebx, 4
	jns	.gloop
	__bfc_emtxt "_H"
	return
endp

; bfc_emit_face(module, arena, pubmask, facies) -> rax = the bytes, rdx = how
; many. `pubmask` is one byte per Module.funcs entry (BFC_FACE_* bits); the
; driver builds it from the typed tree. `facies` is BFC_FACIES_H or _RS.
proc bfc_emit_face, module, arena, pubmask, facies
	uses	rbx
	locals
		slot ctx,       dq	; the face itself (the name __bfc_emtxt reads)
		slot bfcf_body, dq	; the declarations alone, hashed for the guard
		slot bfcf_bp,   dq
		slot bfcf_bl,   dq
		slot bfcf_hash, dq
	endl
	mov	rdi, [arena]
	call	__bfc_face_ctx
	mov	[bfcf_body], rax
	mov	rcx, [module]
	mov	[rax + BfaPrintCtx.module], rcx
	mov	rdi, [arena]
	call	__bfc_face_ctx
	mov	[ctx], rax
	mov	rcx, [module]
	mov	[rax + BfaPrintCtx.module], rcx

	mov	rdi, [bfcf_body]
	mov	rsi, [module]
	mov	rdx, [pubmask]
	mov	ecx, [facies]
	call	__bfc_face_decls
	mov	rdi, [bfcf_body]
	mov	rdi, [rdi + BfaPrintCtx.obuf]
	mov	rax, [rdi + Vec.data]
	mov	[bfcf_bp], rax
	mov	rax, [rdi + Vec.len]
	mov	[bfcf_bl], rax

	cmp	dword [facies], BFC_FACIES_RS
	je	.rs

	; ---- C11 header ------------------------------------------------------
	__bfc_emtxt "/* exsecutor: the C face of a library unit (exsc --emitte h). Generated,", 10
	__bfc_emtxt " * never hand-edited: one prototype per `publica` function the unit defines,", 10
	__bfc_emtxt " * spelled exactly as the unit's own prototype, then the one symbol the unit", 10
	__bfc_emtxt " * imports. The host supplies exsrt_abortus; only a trap reaches it, and it", 10
	__bfc_emtxt " * must not return. Above each prototype, its IR signature: a uint64_t carries", 10
	__bfc_emtxt " * the named width in canonical form (a uN zero-extended, an iN sign-extended);", 10
	__bfc_emtxt " * a value outside that width is outside the contract, and what the unit does", 10
	__bfc_emtxt " * with one is unspecified. docs/design/c-backend.md D1, D5, D9. */", 10
	mov	rdi, [bfcf_bp]
	mov	rsi, [bfcf_bl]
	call	map_hash
	mov	[bfcf_hash], rax
	__bfc_emtxt "#ifndef "
	mov	rdi, [ctx]
	mov	rsi, [bfcf_hash]
	call	__bfc_face_guard
	__bfc_emtxt 10, "#define "
	mov	rdi, [ctx]
	mov	rsi, [bfcf_hash]
	call	__bfc_face_guard
	__bfc_emtxt 10, "#include <stdint.h>", 10, 10
	mov	rdi, [ctx]
	mov	rsi, [bfcf_bp]
	mov	rdx, [bfcf_bl]
	call	__bfa_out_bytes
	cmp	qword [bfcf_bl], 0
	je	.nodecl
	__bfc_emtxt 10
  .nodecl:
	__bfc_emtxt "_Noreturn void exsrt_abortus(unsigned kind);", 10
	__bfc_emtxt "#endif", 10
	jmp	.out

	; ---- Rust extern block ----------------------------------------------
  .rs:
	__bfc_emtxt "// exsecutor: the Rust face of a library unit (exsc --emitte rs). Generated,", 10
	__bfc_emtxt "// never hand-edited: one declaration per `publica` function the unit", 10
	__bfc_emtxt "// defines, in the unit's own C ABI -- uint64_t is u64, unsigned char * is", 10
	__bfc_emtxt "// *mut u8, float is f32, double is f64. The unit imports one symbol, which", 10
	__bfc_emtxt "// the host supplies and which must not return:", 10
	__bfc_emtxt "//     _Noreturn void exsrt_abortus(unsigned kind);", 10
	__bfc_emtxt "//     #[no_mangle] pub extern ", '"', "C", '"', " fn exsrt_abortus(kind: u32) -> ! { ... }", 10
	__bfc_emtxt "// Above each declaration, its IR signature: a u64 carries the named width in", 10
	__bfc_emtxt "// canonical form (a uN zero-extended, an iN sign-extended); a value outside", 10
	__bfc_emtxt "// that width is outside the contract, and what the unit does with one is", 10
	__bfc_emtxt "// unspecified. docs/design/c-backend.md D1, D5, D9.", 10
	__bfc_emtxt "unsafe extern ", '"', "C", '"', " {", 10
	mov	rdi, [ctx]
	mov	rsi, [bfcf_bp]
	mov	rdx, [bfcf_bl]
	call	__bfa_out_bytes
	__bfc_emtxt "}", 10

  .out:
	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.obuf]
	mov	rax, [rdi + Vec.data]
	mov	rdx, [rdi + Vec.len]
	return

endp
