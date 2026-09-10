; compiler/x86_64/checker/lexicon/lexicon.inc
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
; PASS 5 -- LEXICON. `docs/design/checker.md` section 2.2's sixth deliverable,
; section 6's `checker-lexicon` row: `chk_lexicon(chk_c) -> diagnostics
; buffered`, over PUBLIC declarations only (spec §3.1), raising `EXS-E0601`
; (no decomposition), `EXS-E0602` (suffix/declared-kind disagreement),
; `EXS-E0603` (prefix signature law violated) and `EXS-E0610` (more than two
; affixes, spec §3.8).
;
; BUILT, TESTED, AND NOT ENABLED. `checker/checker.inc`'s `chk_run` does not
; call this pass -- its call is commented out there, with the reason spec
; §3.3 amended in: "§3.3's table is illustrative, and it is not the morpheme
; table." `morphemes.inc` (this directory) is generated from that illustrative
; table by `tools/gen-lexicon.py` and inherits its incompleteness exactly:
; `applic-`, `salut-` and `imprim-` are absent because §3.3 lacks them, and
; §3.3's own prose says as much ("`saluta`, `construe`, `imprime`, `textus`
; and `grapha` are all correct Latin and none decomposes over them"). Running
; this pass against a real module today would reject the spec's own examples.
; `tests/unit/chk_lx_e0601_saluta.asm` asserts exactly that, as the load-
; bearing NEGATIVE result: `saluta`, `imprime_gutenbergio` and `initium` all
; fail `EXS-E0601` under this table, which is the measured reason the pass
; stays off. See checker.md section 2.4's table and finding 16.
;
; ---------------------------------------------------------------------------
; THE ALGORITHM. Spec §3.1: "prefixes + root + suffix drawn from a declared,
; versioned morpheme table." No search: at most two affix-stripping rounds
; (§3.8's own ceiling), longest-match on prefixes (§3.6: no assimilation, so
; matching is byte-literal concatenation, and longest-match is the only
; ambiguity a literal scheme can have), and the suffix table is tried in a
; fixed order with the first (root, suffix) pair that fits winning -- see
; `__chk_lx_match_rs`, below.
;
;   1. Strip one qualifier after the first `_` (spec §3.1); only the part
;      before it decomposes.
;   2. Try (root, suffix) directly against the whole base name (0 prefixes).
;   3. Failing that, strip the longest-matching prefix and retry (1 prefix).
;   4. Failing that, strip a second prefix from the remainder and retry. A
;      match here is `EXS-E0610` (three affixes: two prefixes plus the
;      suffix, over §3.8's ceiling of two) -- reported and nothing else is
;      checked on an over-composed name (one source fact, one code, the same
;      reasoning `resolve.inc`'s `__chk_bind` gives for `E0302` vs `E0422`).
;   5. No match anywhere: `EXS-E0601`.
;   6. A match: the suffix's declared-kind class (§3.4's "must declare"
;      column) is compared against the declaration's ACTUAL `AST_D_*` kind
;      -- disagreement is `EXS-E0602`.
;   7. With exactly one prefix AND a functio-like declaration (§3.5: "Prefix
;      laws constrain functio signatures ... on a structura, typus, or
;      interfacies declaration a prefix is positional and semantic only"):
;      the prefix's law is checked IF it is one of the two checkable without
;      types (checker.md section 2.2) -- `re-` (same signature as the bare
;      form: the two `Sig` nodes compared structurally, ignoring parameter
;      names and the capability row) and `con-` (arity >= 2). The other five
;      laws (`de- ex- in- trans-`, and `prae-`/`sub-`'s "positional only",
;      which is never a violation) are `[OPEN]` here and raise nothing.
;
; FINDING 18 (checker.md section 9): `re-`'s law needs a BASE declaration to
; compare against, and §3.5 does not say where it must be declared. Checked
; only when the base is a PUBLIC declaration of THIS module -- a two-step
; lookup, `__chk_lx_find_base`: first, does the candidate base TEXT exist as
; any interned identifier at all (a raw, read-only probe of the interner's
; own map, exactly `chk_init`'s "initium" lookup); second, is that name a
; public functio-like declaration (`__chk_lx_index_pub`'s own map, built once
; per run). Either miss is `[OPEN]`, not a diagnostic.
;
; ---------------------------------------------------------------------------
; WHY A SEPARATE MAP, NOT `ChkCtx.mods`. `ChkCtx.mods` is `chk_resolve`'s
; (pass 1) and this pass's own fixtures call `chk_lexicon` directly on a
; hand-built tree with NO earlier pass run (checker.md section 6's
; prescribed arrangement for testing a pass whose predecessors may not have
; run against this exact tree) -- exactly the shape `checker/rows/`'s
; fixtures already use. Depending on `ChkCtx.mods` would make this pass's
; fixtures depend on `chk_resolve` having populated it, which is another
; agent's module and not always true of a fixture built for this pass alone.
; `__chk_lx_index_pub` is therefore self-contained: it scans `Ast.decls`
; itself, once per run, and needs nothing pass 1 may or may not have filled.
;
; ---------------------------------------------------------------------------
; STRUCTURAL TYPE EQUALITY, GENERICALLY, OVER `ast_roles` (ast/kinds.inc).
; `__chk_lx_ty_eq` walks any two type-expression subtrees (`TyPath` `TyDyn`
; `TyBit` `TyPtr` `TyFn` `TySuffix` `Path` `Seg` `Row` `RowItem`
; `GenericArgs`) by comparing `.kind`/`.aux` and then each of the four
; operand slots per its role: `NODE`/`NODEQ` recurse, `LIST`+`COUNT` compare
; counts and recurse element-wise, `NAME` compares interned ids directly
; (safe because both sides come from the SAME tree and therefore the SAME
; interner -- identical text always yields identical ids), `ROWI` branches on
; `AST_ROW_SICUT` the same way ast/verify.inc does, and `DECL`/`ANY` are
; skipped (resolved/opaque data, not source structure). This is reused
; UNMODIFIED for `functio(...)`-typed parameters (a `TyFn`'s row compares
; structurally too), which is more than spec §3.5's "same signature" strictly
; needs but costs nothing extra to get right once written generically.
;
; `__chk_lx_sig_eq` does NOT call the generic walker on the whole `Sig`:
; a `Param`'s NAME (`v` vs `x`) is not part of a signature and must not be
; compared, so it reads each `Param`'s TYPE slot directly and compares only
; that, plus the result type -- the capability row (`Sig.d`) is deliberately
; never read here, because a row difference is pass 3's `EXS-E0421`/`E0510`
; territory, not a lexicon law.
;
; ---------------------------------------------------------------------------
; WHAT THIS FILE DOES NOT INCLUDE. Same contract as every other pass in this
; chain (resolve.inc, rows/rowdefs.inc): a consumer includes
; `lexer/lexer.inc` and `ast/ast.inc` FIRST. This file itself includes
; `../rows/rows.inc` (which includes `../resolve/resolve.inc`), so
; `ChkCtx`, `chk_raise`, `__chk_at_decl`, `CHK_S_LEXICON` and `CHK_P_LEXICON`
; are already in scope by the time this file's own code runs.
;
; NOT WIRED INTO THE CHAIN. `checker/verify.inc` (owned by `checker-resolve`)
; still includes `rows/rows.inc` as its last link; wiring this pass in means
; changing that one line to `include 'lexicon/lexicon.inc'` (which itself
; opens `include '../rows/rows.inc'`, preserving the strict-chain-not-diamond
; shape verify.inc's header requires) and un-commenting `checker/checker.inc`'s
; `chk_lexicon` call in `chk_run`. Both are outside this agent's write scope
; (CLAUDE.md, Scope) and are reported, not made, in checker-lexicon's report.
; Every fixture in `tests/unit/chk_lx_*.asm` therefore includes this file
; directly and calls `chk_lexicon` itself, exactly as `checker/rows/`'s
; fixtures call `chk_rows`/`chk_layout` directly.
;
; ALL PROC ARGUMENTS AND `slot` NAMES BELOW ARE PREFIXED `lx_` (or carry an
; already-distinctive module prefix such as `chkx_`/`chkl_`/`wk`-derived
; field names on `ChkLxWork`). docs/asm-conventions.md §4.1: fasmg has one
; flat namespace and a `proc` argument or `slot` name is an unmangled GLOBAL
; -- a short name like `rp`, `i`, `n` or `depth` silently collides with an
; identically-named argument/slot burned somewhere else under `compiler/`.
; Found by running this file's own first fixture, not by inspection:
; `rp = rbp - __proc_frame_off` failed to assemble ("invalid expression")
; the moment the full `ast/`+`cst/` include chain was present, even though
; nothing under `compiler/` or `vendor/` greps for the bare word `rp` --
; some already-included file's macro expansion binds it to something this
; module cannot see or purge. Renaming everything here rather than chasing
; that one collision down is the same call docs/asm-conventions.md 4.1
; itself describes for `Str`/`STR` and `start`: the burned-name list is a
; reason to prefix on principle, not a promise that every collision will be
; a readable error at its own site.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §3.1, §3.3, §3.4, §3.5, §3.6, §3.7,
; §3.8, §13, §14 entry 14.
; -----------------------------------------------------------------------------

include '../rows/rows.inc'
include 'morphemes.inc'

CHK_LX_DEPTH_MAX = 32
CHK_LX_MAP_BUCKETS_MIN = 16

; =============================================================================
; byte helpers -- local, not `rt/str.inc` or `rt/map.inc`'s `__map_bytes_equal`
; =============================================================================
; `rt/map.inc`'s own header says `__map_bytes_equal` is "not a primitive other
; modules should reach for"; `rt/str.inc` is the file that would be, but it is
; not reachable from this include graph without re-processing `struct Arena`
; a second time (the same diamond `resolve.inc`'s `__chk_dsort` header
; documents for `rt/sort.inc`) -- both `ast/ast.inc` (already required before
; this file) and `rt/str.inc` reach `arena.inc` by different paths, and fasmg
; has one flat namespace. Three small byte routines, reimplemented here,
; avoid the diamond entirely.

; __chk_lx_beq(lx_ap, lx_bp, lx_n) -> eax = 1 if the `lx_n` bytes at `lx_ap`
; and `lx_bp` are identical, else 0.
proc __chk_lx_beq, lx_ap, lx_bp, lx_n
	locals
	endl
	mov	rsi, [lx_ap]
	mov	rdi, [lx_bp]
	mov	rcx, [lx_n]
	test	rcx, rcx
	jz	.eq
	cld
	repe	cmpsb
	jne	.ne
  .eq:
	mov	eax, 1
	return
  .ne:
	xor	eax, eax
	return
endp

; __chk_lx_starts(lx_ptr, lx_len, lx_pptr, lx_plen) -> eax = 1 if the first
; `lx_plen` bytes of (lx_ptr,lx_len) equal (lx_pptr,lx_plen); 0 if
; lx_len < lx_plen or the bytes differ.
proc __chk_lx_starts, lx_ptr, lx_len, lx_pptr, lx_plen
	locals
	endl
	mov	rax, [lx_len]
	cmp	rax, [lx_plen]
	jb	.no
	mov	rdi, [lx_ptr]
	mov	rsi, [lx_pptr]
	mov	rdx, [lx_plen]
	call	__chk_lx_beq
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_lx_ends(lx_ptr, lx_len, lx_sptr, lx_slen) -> eax = 1 if the LAST
; `lx_slen` bytes of (lx_ptr,lx_len) equal (lx_sptr,lx_slen); 0 if
; lx_len < lx_slen or the bytes differ.
proc __chk_lx_ends, lx_ptr, lx_len, lx_sptr, lx_slen
	uses	rbx
	locals
	endl
	mov	rax, [lx_len]
	cmp	rax, [lx_slen]
	jb	.no
	mov	rbx, [lx_ptr]
	add	rbx, [lx_len]
	sub	rbx, [lx_slen]
	mov	rdi, rbx
	mov	rsi, [lx_sptr]
	mov	rdx, [lx_slen]
	call	__chk_lx_beq
	return
  .no:
	xor	eax, eax
	return
endp

; __chk_lx_find_us(lx_ptr, lx_len) -> rax = byte offset of the first '_' in
; (lx_ptr,lx_len), or -1 if there is none. Linear; a public name is short and
; this runs once per declaration (spec §3.1's qualifier rule).
proc __chk_lx_find_us, lx_ptr, lx_len
	locals
		slot	lx_i, dq
	endl
	mov	qword [lx_i], 0
  .loop:
	mov	rax, [lx_i]
	cmp	rax, [lx_len]
	jae	.miss
	mov	rcx, [lx_ptr]
	movzx	edx, byte [rcx + rax]
	cmp	edx, 95			; '_'
	je	.hit
	inc	qword [lx_i]
	jmp	.loop
  .hit:
	mov	rax, [lx_i]
	return
  .miss:
	mov	rax, -1
	return
endp

; =============================================================================
; decomposition: prefixes (longest-match) and (root, suffix) matching
; =============================================================================

; __chk_lx_match_prefix(lx_ptr, lx_len) -> eax = 1-based `lex_prefix_text`
; index of the LONGEST prefix that is a literal byte-prefix of
; (lx_ptr,lx_len), or 0 if none matches; edx = that prefix's matched byte
; length (0 on no match). Ties keep the earliest (lowest-index, i.e. §3.5's
; own table order) entry, since the running best is only replaced by a
; STRICTLY longer match.
proc __chk_lx_match_prefix, lx_ptr, lx_len
	uses	rbx, r12, r13
	locals
		slot	lx_besti, dq
		slot	lx_bestn, dq
		slot	lx_i,     dq
		slot	lx_pp,    dq
		slot	lx_pl,    dq
	endl
	mov	qword [lx_besti], 0
	mov	qword [lx_bestn], 0
	mov	qword [lx_i], 1
  .loop:
	mov	rax, [lx_i]
	cmp	rax, LEX_PREFIX_COUNT
	ja	.done
	lea	rdi, [lex_prefix_text]
	mov	rsi, [lx_i]
	call	ast_name_at		; rax = ptr (e.g. "re-"), rdx = len (incl '-')
	mov	r12, rax
	mov	r13, rdx
	dec	r13			; strip the trailing '-'
	mov	[lx_pp], r12
	mov	[lx_pl], r13
	mov	rdi, [lx_ptr]
	mov	rsi, [lx_len]
	mov	rdx, [lx_pp]
	mov	rcx, [lx_pl]
	call	__chk_lx_starts
	test	eax, eax
	jz	.next
	mov	rax, [lx_pl]
	cmp	rax, [lx_bestn]
	jbe	.next
	mov	[lx_bestn], rax
	mov	rax, [lx_i]
	mov	[lx_besti], rax
  .next:
	inc	qword [lx_i]
	jmp	.loop
  .done:
	mov	rax, [lx_besti]
	mov	rdx, [lx_bestn]
	return
endp

; __chk_lx_match_rs(lx_ptr, lx_len, lx_root_out, lx_suf_out) -> eax = 1 if a
; (root, suffix) decomposition of the WHOLE span (lx_ptr,lx_len) exists;
; lx_root_out/lx_suf_out (dd pointers) receive the 1-based table indices.
; First match in table order (suffix outer, root inner) wins -- §3.4's six
; suffixes and §3.3's fourteen roots do not overlap in a way that makes a
; later match differ in kind from an earlier one (this file's header).
;
; THE `-e` SUFFIX IS SPECIAL (finding 17): its `lex_suffix_stem_sel` is
; `LEX_STEM_IMPERATIVE`, and for it this routine compares the WHOLE
; remainder against a root's precomputed imperative form directly, rather
; than stripping suffix bytes and comparing a stem -- see morphemes.inc's
; header. A root with no known imperative (`lex_root_imperative` length 0)
; is never a candidate for `-e`.
proc __chk_lx_match_rs, lx_ptr, lx_len, lx_root_out, lx_suf_out
	uses	rbx, r12, r13, r14
	locals
		slot	lx_si,    dq
		slot	lx_sp,    dq
		slot	lx_sl,    dq
		slot	lx_sel,   dq
		slot	lx_ri,    dq
		slot	lx_rp,    dq
		slot	lx_rl,    dq
		slot	lx_stemn, dq
		slot	lx_stemp, dq
		slot	lx_poolp, dq
	endl
	mov	qword [lx_si], 1
  .sloop:
	mov	rax, [lx_si]
	cmp	rax, LEX_SUFFIX_COUNT
	ja	.miss
	lea	rdi, [lex_suffix_text]
	mov	rsi, [lx_si]
	call	ast_name_at		; rax = ptr (e.g. "-e"), rdx = len (incl '-')
	lea	r12, [rax + 1]		; skip the leading '-'
	mov	r13, rdx
	dec	r13			; bare suffix length, no leading '-'
	mov	[lx_sp], r12
	mov	[lx_sl], r13
	mov	rax, [lx_si]
	dec	rax
	lea	rdi, [lex_suffix_stem_sel]
	movzx	eax, byte [rdi + rax]
	mov	[lx_sel], rax

	cmp	qword [lx_sel], LEX_STEM_IMPERATIVE
	jne	.notimp

	; -- imperative branch: whole-word compare, never strip --
	mov	qword [lx_ri], 1
  .iloop:
	mov	rax, [lx_ri]
	cmp	rax, LEX_ROOT_COUNT
	ja	.snext
	lea	rdi, [lex_root_imperative]
	mov	rsi, [lx_ri]
	call	ast_name_at
	mov	[lx_rp], rax
	mov	[lx_rl], rdx
	cmp	qword [lx_rl], 0
	je	.inext			; unknown -- never a candidate
	mov	rax, [lx_len]
	cmp	rax, [lx_rl]
	jne	.inext
	mov	rdi, [lx_ptr]
	mov	rsi, [lx_rp]
	mov	rdx, [lx_rl]
	call	__chk_lx_beq
	test	eax, eax
	jz	.inext
	jmp	.hit
  .inext:
	inc	qword [lx_ri]
	jmp	.iloop

  .notimp:
	; -- present/supine branch: strip the literal suffix bytes, compare
	; what remains against the selected stem, exactly --
	mov	rdi, [lx_ptr]
	mov	rsi, [lx_len]
	mov	rdx, [lx_sp]
	mov	rcx, [lx_sl]
	call	__chk_lx_ends
	test	eax, eax
	jz	.snext
	mov	rax, [lx_len]
	sub	rax, [lx_sl]
	mov	[lx_stemn], rax
	mov	rax, [lx_ptr]
	mov	[lx_stemp], rax

	mov	qword [lx_ri], 1
  .rloop:
	mov	rax, [lx_ri]
	cmp	rax, LEX_ROOT_COUNT
	ja	.snext
	cmp	qword [lx_sel], LEX_STEM_PRESENT
	je	.usepresent
	lea	rax, [lex_root_supine]
	jmp	.havepool
  .usepresent:
	lea	rax, [lex_root_present]
  .havepool:
	mov	[lx_poolp], rax
	mov	rdi, [lx_poolp]
	mov	rsi, [lx_ri]
	call	ast_name_at
	mov	[lx_rp], rax
	mov	[lx_rl], rdx
	mov	rax, [lx_stemn]
	cmp	rax, [lx_rl]
	jne	.rnext
	mov	rdi, [lx_stemp]
	mov	rsi, [lx_rp]
	mov	rdx, [lx_rl]
	call	__chk_lx_beq
	test	eax, eax
	jz	.rnext
	jmp	.hit
  .rnext:
	inc	qword [lx_ri]
	jmp	.rloop

  .snext:
	inc	qword [lx_si]
	jmp	.sloop

  .hit:
	mov	rdi, [lx_root_out]
	mov	eax, [lx_ri]
	mov	[rdi], eax
	mov	rdi, [lx_suf_out]
	mov	eax, [lx_si]
	mov	[rdi], eax
	mov	eax, 1
	return
  .miss:
	mov	rdi, [lx_root_out]
	mov	dword [rdi], 0
	mov	rdi, [lx_suf_out]
	mov	dword [rdi], 0
	xor	eax, eax
	return
endp

; =============================================================================
; structural type/signature equality
; =============================================================================

; __chk_lx_ty_eq(lx_tree, lx_na, lx_nb, lx_depth) -> eax = 1/0. See this
; file's header.
proc __chk_lx_ty_eq, lx_tree, lx_na, lx_nb, lx_depth
	uses	rbx, r12, r13, r14
	locals
		slot	lx_slotidx, dq
		slot	lx_role,    dq
		slot	lx_cnta,    dq
		slot	lx_cntb,    dq
		slot	lx_offa,    dq
		slot	lx_offb,    dq
		slot	lx_ix,      dq
		slot	lx_ela,     dq
		slot	lx_elb,     dq
		slot	lx_kindv,   dq
	endl
	mov	rax, [lx_na]
	mov	rcx, [lx_nb]
	test	rax, rax
	jnz	.a_nonzero
	test	rcx, rcx
	jz	.eq
	jmp	.ne
  .a_nonzero:
	test	rcx, rcx
	jz	.ne

	mov	rax, [lx_depth]
	mov	rcx, CHK_LX_DEPTH_MAX
	rassert	rax le rcx

	mov	rdi, [lx_tree]
	mov	rsi, [lx_na]
	call	ast_node_at
	mov	r12, rax
	mov	rdi, [lx_tree]
	mov	rsi, [lx_nb]
	call	ast_node_at
	mov	r13, rax

	movzx	eax, word [r12 + AstNode.kind]
	movzx	ecx, word [r13 + AstNode.kind]
	cmp	eax, ecx
	jne	.ne
	mov	[lx_kindv], rax
	movzx	eax, word [r12 + AstNode.aux]
	movzx	ecx, word [r13 + AstNode.aux]
	cmp	eax, ecx
	jne	.ne

	mov	qword [lx_slotidx], 0
  .slot:
	mov	rax, [lx_slotidx]
	cmp	rax, 4
	jae	.eq
	mov	rdi, [lx_kindv]
	mov	rsi, rax
	call	ast_role_of
	mov	[lx_role], rax

	cmp	qword [lx_role], AST_R_NONE
	je	.skip1
	cmp	qword [lx_role], AST_R_DECL
	je	.skip1
	cmp	qword [lx_role], AST_R_ANY
	je	.skip1
	cmp	qword [lx_role], AST_R_COUNT
	je	.skip1
	cmp	qword [lx_role], AST_R_NAME
	je	.do_name
	cmp	qword [lx_role], AST_R_NODE
	je	.do_node
	cmp	qword [lx_role], AST_R_NODEQ
	je	.do_node
	cmp	qword [lx_role], AST_R_ROWI
	je	.do_rowi
	cmp	qword [lx_role], AST_R_LIST
	je	.do_list
	; anything else (AST_R_LIST2) cannot appear inside a type/row subtree
	; per spec §8.6's grammar -- a compiler bug, not a source fact.
	xor	eax, eax
	rassert	eax ne 0

  .do_name:
	mov	rax, [lx_slotidx]
	mov	ecx, [r12 + AstNode.a + rax*4]
	cmp	ecx, [r13 + AstNode.a + rax*4]
	jne	.ne
	jmp	.skip1

  .do_node:
	mov	rax, [lx_slotidx]
	mov	eax, [r12 + AstNode.a + rax*4]
	mov	[lx_ela], rax
	mov	rax, [lx_slotidx]
	mov	eax, [r13 + AstNode.a + rax*4]
	mov	[lx_elb], rax
	mov	rdi, [lx_tree]
	mov	rsi, [lx_ela]
	mov	rdx, [lx_elb]
	mov	rcx, [lx_depth]
	inc	rcx
	call	__chk_lx_ty_eq
	test	eax, eax
	jz	.ne
	jmp	.skip1

  .do_rowi:
	movzx	eax, word [r12 + AstNode.aux]
	test	eax, AST_ROW_SICUT
	jz	.do_node
	jmp	.do_name

  .do_list:
	mov	rax, [lx_slotidx]
	mov	ecx, [r12 + AstNode.a + rax*4]
	mov	[lx_offa], rcx
	mov	ecx, [r13 + AstNode.a + rax*4]
	mov	[lx_offb], rcx
	inc	rax
	mov	ecx, [r12 + AstNode.a + rax*4]
	mov	[lx_cnta], rcx
	mov	ecx, [r13 + AstNode.a + rax*4]
	mov	[lx_cntb], rcx
	mov	rax, [lx_cnta]
	cmp	rax, [lx_cntb]
	jne	.ne

	mov	qword [lx_ix], 0
  .lloop:
	mov	rax, [lx_ix]
	cmp	rax, [lx_cnta]
	jae	.ldone
	mov	rdi, [lx_tree]
	mov	rax, [lx_offa]
	add	rax, [lx_ix]
	mov	rsi, rax
	call	ast_extra_at
	mov	[lx_ela], rax
	mov	rdi, [lx_tree]
	mov	rax, [lx_offb]
	add	rax, [lx_ix]
	mov	rsi, rax
	call	ast_extra_at
	mov	[lx_elb], rax
	mov	rdi, [lx_tree]
	mov	rsi, [lx_ela]
	mov	rdx, [lx_elb]
	mov	rcx, [lx_depth]
	inc	rcx
	call	__chk_lx_ty_eq
	test	eax, eax
	jz	.ne
	inc	qword [lx_ix]
	jmp	.lloop
  .ldone:
	add	qword [lx_slotidx], 2
	jmp	.slot

  .skip1:
	inc	qword [lx_slotidx]
	jmp	.slot

  .eq:
	mov	eax, 1
	return
  .ne:
	xor	eax, eax
	return
endp

; __chk_lx_sig_eq(lx_tree, lx_siga, lx_sigb) -> eax = 1/0. Compares parameter
; TYPES (never names, never declarations) and the result type only; the
; capability row (`Sig.d`) is never read -- see this file's header.
proc __chk_lx_sig_eq, lx_tree, lx_siga, lx_sigb
	uses	rbx, r12, r13
	locals
		slot	lx_cnta, dq
		slot	lx_cntb, dq
		slot	lx_offa, dq
		slot	lx_offb, dq
		slot	lx_ix,   dq
		slot	lx_pa,   dq
		slot	lx_pb,   dq
		slot	lx_ta,   dq
		slot	lx_tb,   dq
	endl
	mov	rdi, [lx_tree]
	mov	rsi, [lx_siga]
	call	ast_node_at
	mov	r12, rax
	mov	rdi, [lx_tree]
	mov	rsi, [lx_sigb]
	call	ast_node_at
	mov	r13, rax

	mov	eax, [r12 + AstNode.b]
	mov	[lx_cnta], rax
	mov	eax, [r13 + AstNode.b]
	mov	[lx_cntb], rax
	mov	rax, [lx_cnta]
	cmp	rax, [lx_cntb]
	jne	.ne

	mov	eax, [r12 + AstNode.a]
	mov	[lx_offa], rax
	mov	eax, [r13 + AstNode.a]
	mov	[lx_offb], rax

	mov	qword [lx_ix], 0
  .loop:
	mov	rax, [lx_ix]
	cmp	rax, [lx_cnta]
	jae	.result
	mov	rdi, [lx_tree]
	mov	rax, [lx_offa]
	add	rax, [lx_ix]
	mov	rsi, rax
	call	ast_extra_at
	mov	[lx_pa], rax
	mov	rdi, [lx_tree]
	mov	rax, [lx_offb]
	add	rax, [lx_ix]
	mov	rsi, rax
	call	ast_extra_at
	mov	[lx_pb], rax

	; each element is a Param NODE id; compare its TYPE slot (.b) only --
	; NOT its name (.a) or its declaration (.d): §3.5's "same signature"
	; is a claim about types, and parameter identifiers are free to differ.
	mov	rdi, [lx_tree]
	mov	rsi, [lx_pa]
	call	ast_node_at
	mov	eax, [rax + AstNode.b]
	mov	[lx_ta], rax
	mov	rdi, [lx_tree]
	mov	rsi, [lx_pb]
	call	ast_node_at
	mov	eax, [rax + AstNode.b]
	mov	[lx_tb], rax

	mov	rdi, [lx_tree]
	mov	rsi, [lx_ta]
	mov	rdx, [lx_tb]
	xor	rcx, rcx
	call	__chk_lx_ty_eq
	test	eax, eax
	jz	.ne

	inc	qword [lx_ix]
	jmp	.loop

  .result:
	; the result type: Sig.c, 0 = unit/none on both sides is equal.
	mov	eax, [r12 + AstNode.c]
	mov	[lx_ta], rax
	mov	eax, [r13 + AstNode.c]
	mov	[lx_tb], rax
	mov	rdi, [lx_tree]
	mov	rsi, [lx_ta]
	mov	rdx, [lx_tb]
	xor	rcx, rcx
	call	__chk_lx_ty_eq
	test	eax, eax
	jz	.ne
	mov	eax, 1
	return
  .ne:
	xor	eax, eax
	return
endp

; =============================================================================
; the public-declaration name -> decl-id index (for the `re-` law's base
; lookup, finding 18)
; =============================================================================

struct ChkLxWork
	field lxw_tree,   dq
	field lxw_ctx,    dq
	lxw_pubmap Map		; public functio-like decl name id -> decl id
	field lxw_pubkeys, dq	; -> u32[decls+1], stable (rt/map.inc keys must be)
end struct

proc chk_lx_work_init, lx_wk, lx_ctx
	uses	rbx, r12, r13, r14
	locals
	endl
	mov	rbx, [lx_wk]
	mov	rdi, rbx
	mov	rcx, sizeof.ChkLxWork
	xor	eax, eax
	cld
	rep	stosb
	mov	rbx, [lx_wk]
	mov	r12, [lx_ctx]
	mov	rax, [r12 + ChkCtx.tree]
	mov	[rbx + ChkLxWork.lxw_tree], rax
	mov	[rbx + ChkLxWork.lxw_ctx], r12

	mov	rdi, [r12 + ChkCtx.tree]
	call	ast_decl_count
	mov	r13, rax
	inc	r13
	mov	rdi, [r12 + ChkCtx.arena]
	mov	rsi, r13
	shl	rsi, 2
	call	arena_alloc
	mov	[rbx + ChkLxWork.lxw_pubkeys], rax

	mov	r14, CHK_LX_MAP_BUCKETS_MIN
	mov	rcx, r13
	shl	rcx, 1
  .grow:
	cmp	r14, rcx
	jae	.have
	shl	r14, 1
	jmp	.grow
  .have:
	lea	rdi, [rbx + ChkLxWork.lxw_pubmap]
	mov	rsi, [r12 + ChkCtx.arena]
	mov	rdx, r14
	call	map_init
	return
endp

; __chk_lx_index_pub(lx_wk) -- one scan of `Ast.decls`, filling `lxw_pubmap`:
; every PUBLIC `Fn`/`ExternFn`/`Member` declaration's name id -> its decl
; id. The FIRST such declaration of a given name wins (decl order); a later
; duplicate is a class-B error elsewhere (`EXS-E0302`) and is not this
; pass's to re-report.
proc __chk_lx_index_pub, lx_wk
	uses	rbx, r12, r13
	locals
		slot	lx_n,      dq
		slot	lx_d,      dq
		slot	lx_nameid, dd
	endl
	mov	rbx, [lx_wk]
	mov	rdi, [rbx + ChkLxWork.lxw_tree]
	call	ast_decl_count
	mov	[lx_n], rax
	mov	qword [lx_d], 1
  .loop:
	mov	rax, [lx_d]
	cmp	rax, [lx_n]
	ja	.done
	mov	rdi, [rbx + ChkLxWork.lxw_tree]
	mov	rsi, [lx_d]
	call	ast_decl_at
	mov	r12, rax
	movzx	ecx, byte [r12 + AstDecl.flags]
	test	ecx, AST_F_PUBLICA
	jz	.next
	movzx	ecx, byte [r12 + AstDecl.kind]
	cmp	ecx, AST_D_FN
	je	.isfn
	cmp	ecx, AST_D_EXTERNFN
	je	.isfn
	cmp	ecx, AST_D_MEMBER
	je	.isfn
	jmp	.next
  .isfn:
	mov	eax, [r12 + AstDecl.name]
	test	eax, eax
	jz	.next
	mov	[lx_nameid], eax

	mov	rax, [rbx + ChkLxWork.lxw_pubkeys]
	mov	rcx, [lx_d]
	mov	edx, [lx_nameid]
	mov	[rax + rcx*4], edx

	lea	rdi, [rbx + ChkLxWork.lxw_pubmap]
	mov	rax, [rbx + ChkLxWork.lxw_pubkeys]
	mov	rcx, [lx_d]
	lea	rsi, [rax + rcx*4]
	mov	rdx, 4
	call	map_get
	test	rax, rax
	jnz	.next
	lea	rdi, [rbx + ChkLxWork.lxw_pubmap]
	mov	rax, [rbx + ChkLxWork.lxw_pubkeys]
	mov	rcx, [lx_d]
	lea	rsi, [rax + rcx*4]
	mov	rdx, 4
	mov	rcx, [lx_d]
	call	map_insert
  .next:
	inc	qword [lx_d]
	jmp	.loop
  .done:
	return
endp

; __chk_lx_find_base(lx_wk, lx_ptr, lx_len) -> eax = decl id of a public
; functio-like declaration named EXACTLY the `lx_len` bytes at `lx_ptr`, or
; 0. See this file's header, "FINDING 18".
proc __chk_lx_find_base, lx_wk, lx_ptr, lx_len
	uses	rbx
	locals
		slot	lx_nid, dd
	endl
	mov	rbx, [lx_wk]
	mov	rdi, [rbx + ChkLxWork.lxw_ctx]
	mov	rdi, [rdi + ChkCtx.tree]
	mov	rdi, [rdi + Ast.names]
	test	rdi, rdi
	jz	.none
	mov	rdi, [rdi + Interner.map]
	mov	rsi, [lx_ptr]
	mov	rdx, [lx_len]
	call	map_get
	test	rax, rax
	jz	.none
	mov	[lx_nid], eax
	lea	rdi, [rbx + ChkLxWork.lxw_pubmap]
	lea	rsi, [lx_nid]
	mov	rdx, 4
	call	map_get
	return
  .none:
	xor	eax, eax
	return
endp

; =============================================================================
; pass 5 itself
; =============================================================================

; __chk_lx_check_decl(lx_wk, lx_d) -- one public declaration, decl-order.
proc __chk_lx_check_decl, lx_wk, lx_d
	uses	rbx, r12, r13, r14
	locals
		slot	lx_dnode,   dq
		slot	lx_nameid,  dd
		slot	lx_nptr,    dq
		slot	lx_nlen,    dq
		slot	lx_uspos,   dq
		slot	lx_blen,    dq
		slot	lx_r0,      dd
		slot	lx_s0,      dd
		slot	lx_pidx,    dq
		slot	lx_plen,    dq
		slot	lx_remp,    dq
		slot	lx_reml,    dq
		slot	lx_r1,      dd
		slot	lx_s1,      dd
		slot	lx_pidx2,   dq
		slot	lx_plen2,   dq
		slot	lx_remp2,   dq
		slot	lx_reml2,   dq
		slot	lx_r2,      dd
		slot	lx_s2,      dd
		slot	lx_nprefix, dq
		slot	lx_fr,      dd
		slot	lx_fs,      dd
		slot	lx_dkind,   dq
		slot	lx_basesig, dq
	endl
	mov	rbx, [lx_wk]
	mov	rdi, [rbx + ChkLxWork.lxw_tree]
	mov	rsi, [lx_d]
	call	ast_decl_at
	mov	r12, rax

	movzx	ecx, byte [r12 + AstDecl.flags]
	test	ecx, AST_F_PUBLICA
	jz	.out
	movzx	ecx, byte [r12 + AstDecl.kind]
	mov	[lx_dkind], rcx
	cmp	ecx, AST_D_FN
	je	.applicable
	cmp	ecx, AST_D_EXTERNFN
	je	.applicable
	cmp	ecx, AST_D_MEMBER
	je	.applicable
	cmp	ecx, AST_D_STRUCT
	je	.applicable
	cmp	ecx, AST_D_IFACE
	je	.applicable
	cmp	ecx, AST_D_TYPUS
	je	.applicable
	jmp	.out
  .applicable:
	mov	eax, [r12 + AstDecl.name]
	test	eax, eax
	jz	.out
	mov	[lx_nameid], eax
	mov	eax, [r12 + AstDecl.node]
	mov	[lx_dnode], rax

	mov	rdi, [rbx + ChkLxWork.lxw_ctx]
	mov	rdi, [rdi + ChkCtx.tree]
	mov	rdi, [rdi + Ast.names]
	mov	esi, [lx_nameid]
	call	intern_bytes
	mov	[lx_nptr], rax
	mov	[lx_nlen], rdx

	; §3.1: one qualifier after the FIRST '_'; only the part before it
	; decomposes.
	mov	rdi, [lx_nptr]
	mov	rsi, [lx_nlen]
	call	__chk_lx_find_us
	mov	[lx_uspos], rax
	cmp	qword [lx_uspos], -1
	jne	.hasus
	mov	rax, [lx_nlen]
	mov	[lx_blen], rax
	jmp	.havebase
  .hasus:
	mov	rax, [lx_uspos]
	mov	[lx_blen], rax
  .havebase:

	; ---- try 0 prefixes ----
	mov	rdi, [lx_nptr]
	mov	rsi, [lx_blen]
	lea	rdx, [lx_r0]
	lea	rcx, [lx_s0]
	call	__chk_lx_match_rs
	test	eax, eax
	jz	.try1
	mov	qword [lx_nprefix], 0
	mov	eax, [lx_r0]
	mov	[lx_fr], eax
	mov	eax, [lx_s0]
	mov	[lx_fs], eax
	jmp	.found

  .try1:
	mov	rdi, [lx_nptr]
	mov	rsi, [lx_blen]
	call	__chk_lx_match_prefix
	mov	[lx_pidx], rax
	mov	[lx_plen], rdx
	cmp	qword [lx_pidx], 0
	je	.e0601

	mov	rax, [lx_nptr]
	add	rax, [lx_plen]
	mov	[lx_remp], rax
	mov	rax, [lx_blen]
	sub	rax, [lx_plen]
	mov	[lx_reml], rax

	mov	rdi, [lx_remp]
	mov	rsi, [lx_reml]
	lea	rdx, [lx_r1]
	lea	rcx, [lx_s1]
	call	__chk_lx_match_rs
	test	eax, eax
	jz	.try2
	mov	qword [lx_nprefix], 1
	mov	eax, [lx_r1]
	mov	[lx_fr], eax
	mov	eax, [lx_s1]
	mov	[lx_fs], eax
	jmp	.found

  .try2:
	mov	rdi, [lx_remp]
	mov	rsi, [lx_reml]
	call	__chk_lx_match_prefix
	mov	[lx_pidx2], rax
	mov	[lx_plen2], rdx
	cmp	qword [lx_pidx2], 0
	je	.e0601

	mov	rax, [lx_remp]
	add	rax, [lx_plen2]
	mov	[lx_remp2], rax
	mov	rax, [lx_reml]
	sub	rax, [lx_plen2]
	mov	[lx_reml2], rax

	mov	rdi, [lx_remp2]
	mov	rsi, [lx_reml2]
	lea	rdx, [lx_r2]
	lea	rcx, [lx_s2]
	call	__chk_lx_match_rs
	test	eax, eax
	jz	.e0601

	; a decomposition exists at THREE affixes -- over §3.8's ceiling.
	mov	rdi, [rbx + ChkLxWork.lxw_ctx]
	mov	rsi, CHK_P_LEXICON
	mov	rdx, 610
	mov	rcx, [lx_dnode]
	call	chk_raise
	mov	r14, rax
	mov	rdi, [rbx + ChkLxWork.lxw_ctx]
	mov	rsi, r14
	mov	rdx, [lx_d]
	call	__chk_at_decl
	jmp	.out

  .e0601:
	mov	rdi, [rbx + ChkLxWork.lxw_ctx]
	mov	rsi, CHK_P_LEXICON
	mov	rdx, 601
	mov	rcx, [lx_dnode]
	call	chk_raise
	mov	r14, rax
	mov	rdi, [rbx + ChkLxWork.lxw_ctx]
	mov	rsi, r14
	mov	rdx, [lx_d]
	call	__chk_at_decl
	jmp	.out

  .found:
	; ---- EXS-E0602: suffix's declared-kind class vs. the ACTUAL kind ----
	mov	eax, [lx_fs]
	dec	rax
	lea	rdi, [lex_suffix_kind_class]
	movzx	ecx, byte [rdi + rax]
	mov	rax, [lx_dkind]
	xor	r14, r14
	cmp	ecx, LEXKIND_FN
	jne	.notfnclass
	cmp	rax, AST_D_FN
	je	.classok
	cmp	rax, AST_D_EXTERNFN
	je	.classok
	cmp	rax, AST_D_MEMBER
	je	.classok
	jmp	.classdone
  .notfnclass:
	cmp	ecx, LEXKIND_STRUCT
	jne	.notstructclass
	cmp	rax, AST_D_STRUCT
	je	.classok
	jmp	.classdone
  .notstructclass:
	cmp	ecx, LEXKIND_IFACE
	jne	.notifaceclass
	cmp	rax, AST_D_IFACE
	je	.classok
	jmp	.classdone
  .notifaceclass:
	cmp	ecx, LEXKIND_TYPUS
	jne	.classdone
	cmp	rax, AST_D_TYPUS
	je	.classok
	jmp	.classdone
  .classok:
	mov	r14, 1
  .classdone:
	test	r14, r14
	jnz	.classpassed
	mov	rdi, [rbx + ChkLxWork.lxw_ctx]
	mov	rsi, CHK_P_LEXICON
	mov	rdx, 602
	mov	rcx, [lx_dnode]
	call	chk_raise
	mov	r14, rax
	mov	rdi, [rbx + ChkLxWork.lxw_ctx]
	mov	rsi, r14
	mov	rdx, [lx_d]
	call	__chk_at_decl
  .classpassed:

	; ---- EXS-E0603: a prefix law, only with exactly one prefix and only
	; on a functio-like declaration (spec §3.5) ----
	cmp	qword [lx_nprefix], 1
	jne	.out
	mov	rax, [lx_dkind]
	cmp	rax, AST_D_FN
	je	.isfn2
	cmp	rax, AST_D_EXTERNFN
	je	.isfn2
	cmp	rax, AST_D_MEMBER
	je	.isfn2
	jmp	.out
  .isfn2:
	mov	rax, [lx_pidx]
	dec	rax
	lea	rdi, [lex_prefix_law]
	movzx	ecx, byte [rdi + rax]
	cmp	ecx, LEXLAW_SAME_SIG
	je	.law_samesig
	cmp	ecx, LEXLAW_ARITY2
	je	.law_arity2
	jmp	.out		; every other law needs types -- [OPEN]

  .law_samesig:
	mov	rdi, rbx
	mov	rsi, [lx_remp]
	mov	rdx, [lx_reml]
	call	__chk_lx_find_base
	test	rax, rax
	jz	.out			; finding 18: no base in this module
	cmp	rax, [lx_d]
	je	.out			; never itself
	mov	r14, rax
	mov	rdi, [rbx + ChkLxWork.lxw_tree]
	mov	rsi, r14
	call	ast_decl_at
	mov	eax, [rax + AstDecl.node]
	mov	rdi, [rbx + ChkLxWork.lxw_tree]
	mov	rsi, rax
	call	ast_node_at
	mov	eax, [rax + AstNode.a]		; the base Fn's Sig node
	mov	[lx_basesig], rax
	mov	rdi, [rbx + ChkLxWork.lxw_tree]
	mov	rsi, [lx_dnode]
	call	ast_node_at
	mov	ecx, [rax + AstNode.a]		; this decl's own Sig node
	mov	rdi, [rbx + ChkLxWork.lxw_tree]
	mov	rsi, rcx
	mov	rdx, [lx_basesig]
	call	__chk_lx_sig_eq
	test	eax, eax
	jnz	.out
	jmp	.e0603

  .law_arity2:
	mov	rdi, [rbx + ChkLxWork.lxw_tree]
	mov	rsi, [lx_dnode]
	call	ast_node_at
	mov	ecx, [rax + AstNode.a]		; this decl's Sig node
	mov	rdi, [rbx + ChkLxWork.lxw_tree]
	mov	rsi, rcx
	call	ast_node_at
	mov	eax, [rax + AstNode.b]		; param count
	cmp	eax, 2
	jae	.out
	jmp	.e0603

  .e0603:
	mov	rdi, [rbx + ChkLxWork.lxw_ctx]
	mov	rsi, CHK_P_LEXICON
	mov	rdx, 603
	mov	rcx, [lx_dnode]
	call	chk_raise
	mov	r14, rax
	mov	rdi, [rbx + ChkLxWork.lxw_ctx]
	mov	rsi, r14
	mov	rdx, [lx_d]
	call	__chk_at_decl

  .out:
	return
endp

; chk_lexicon(chkx_c) -> rax = the number of diagnostics THIS pass buffered.
; NOT called by `checker/checker.inc`'s `chk_run` (see this file's header).
proc chk_lexicon, chkx_c
	uses	rbx, r12, r13
	locals
		slot	lx_dn, dq
		slot	lx_di, dq
	endl
	mov	r13, [chkx_c]
	lea	rax, [r13 + ChkCtx.dbuf]
	mov	r12, [rax + Vec.len]

	mov	rdi, [r13 + ChkCtx.arena]
	mov	rsi, sizeof.ChkLxWork
	call	arena_alloc
	mov	rbx, rax
	mov	rdi, rbx
	mov	rsi, r13
	call	chk_lx_work_init
	mov	rdi, rbx
	call	__chk_lx_index_pub

	mov	rdi, [rbx + ChkLxWork.lxw_tree]
	call	ast_decl_count
	mov	[lx_dn], rax
	mov	qword [lx_di], 1
  .loop:
	mov	rax, [lx_di]
	cmp	rax, [lx_dn]
	ja	.done
	mov	rdi, rbx
	mov	rsi, [lx_di]
	call	__chk_lx_check_decl
	inc	qword [lx_di]
	jmp	.loop
  .done:
	mov	r13, [rbx + ChkLxWork.lxw_ctx]
	or	dword [r13 + ChkCtx.stages], CHK_S_LEXICON
	lea	rax, [r13 + ChkCtx.dbuf]
	mov	rax, [rax + Vec.len]
	sub	rax, r12
	return
endp
