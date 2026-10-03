; tests/unit/chk_e0422_alias.asm
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
; checker fixture -- `EXS-E0422` through a `typus` ALIAS of a capability atom.
; spec §4.3 / §4.5 and checker.md section 2.2 ("a `typus` is TRANSPARENT"):
; `typus Amb = ambitus;` makes `a: Amb` an `ambitus` exactly as `a: ambitus` is,
; so a `sub ambitus = a;` beside it is §4.5's "shadowing is an error" under
; either spelling.
;
; THE DEFECT THIS PINS. Pass 1 registers a capability-typed PARAMETER as the
; provider of its atom with `__chk_type_atom`, which read the type as WRITTEN
; and never followed an alias. `a: Amb` therefore registered nothing, so the
; `sub` (and a declared `poscit ambitus`) found no earlier provider and bound
; freely, while pass 2 -- which interns the alias -- already called `a` an
; `ambitus` in every judgement it makes (the `EXS-E0303` provider test among
; them). One fact, two answers. It was not an authority escape (the parameter
; holds the atom visibly under both spellings); it was the non-uniformity the
; reviewed ADR 0017 escapes had the shape of, and its mirror was a spurious
; `EXS-E0421` on a draw the alias parameter should have provided.
;
; THE TABLE. Each row is a whole source run through the real lexer, parser and
; checker in-process (this file's harness is chk_e0422_sub_twice.asm's), and is
; held to the EXACT number of diagnostics, the code of the first, and -- where
; the row has one -- the caret on the offending `sub` statement:
;
;	0     the direct `a: ambitus` control (what every alias row must equal)
;	1     `typus Amb = ambitus;`                         -> E0422
;	2     a chain, `B = A = ambitus`                     -> E0422
;	3     the same chain written AFTER the function      -> E0422 (nothing may
;	      depend on item order: the alias has not been walked yet)
;	4     `typus Amb<T> = ambitus;`, `a: Amb<u8>`        -> E0422 (pass 2
;	      instantiates no alias, so every `Amb<X>` IS `ambitus`)
;	5     alias parameter + a declared `poscit ambitus`  -> E0422 at the row
;	6     two alias parameters of one atom               -> E0422
;	7     `(a: ambitus, b: Amb)`, a mixed pair           -> E0422
;	8     another atom (`rete`), so the walk is not `ambitus`-shaped
;
; and the cases that must NOT become E0422, which are what keep the fix from
; being "treat every alias as an atom":
;
;	9     a generic FUNCTION parameter named `Amb` shadows the alias: `a`
;	      is a `T`, not an `ambitus`, and `sub ambitus = a` is E0303
;	10    an alias cycle -- one E0303 from pass 2, and the walk terminates
;	11    `typus A<T> = T;` with a module `typus T = ambitus;` -- the alias's
;	      own generic parameter shadows the module name: E0303 at the `sub`
;	12    an alias parameter and no `sub`                -> clean
;	13    an alias parameter PROVIDES the atom to a draw -> clean (it was
;	      E0421 before: the direct `a: ambitus` twin has always been clean)
;	14    a prelude type as a parameter (`s: Scriptor`): its `Seg.d` is a
;	      TAGGED row (bit 31), not an `Ast.decls` index, and must end the
;	      walk instead of being read as one
;
; DEPTH. Pass 2 resolves a legal alias chain of ANY length (`__chk_ty_typus`
; marks each `typus` BUSY then DONE and counts nothing), so pass 1 must too: a
; walk that gives up at a fixed depth answers "not a provider" for a chain pass
; 2 answers "ambitus" for, one hop past the constant. The first version of this
; fix stopped at 32 hops and this fixture was green while a 33-hop chain
; escaped EXS-E0422 (and its draw drew a spurious EXS-E0421). Rows 15-29 are the
; chains that bound could not see:
;
;	15    a 33-hop linear chain         -> E0422 (one past the old bound)
;	16    a 40-hop linear chain         -> E0422
;	17    a 40-hop GENERIC chain, `G<i><T> = G<i-1><T>`, `a: G40<u8>` -> E0422
;	18    the 33-hop chain's param PROVIDES a draw -> clean, no E0421
;	19    the 40-hop chain on two params -> E0422 (the double-bind, no `sub`)
;	20    a 33-hop chain ending in `u8`  -> exactly E0303, never E0422
;	21    a 40-hop generic chain ending in `T`, `a: G40<ambitus>` -> exactly
;	      E0303: the tail names the chain's own parameter, which is not the atom
;	22    a self-alias `typus C = C;`  -> exactly ONE E0303, and it ends
;	23    a three-cycle                -> exactly ONE E0303
;	24    a generic two-cycle          -> exactly ONE E0303
;	25    a 30-hop chain written AFTER the function, in reverse -> E0422
;	26    a 40-hop chain ending in `rete` with `sub ambitus` -> exactly E0303
;	27    the same chain with `sub rete` -> E0422
;	28    a 300-hop linear chain        -> E0422 (no constant the fix could
;	      pick -- 32, 64, 256 -- survives this row)
;	29    the 300-hop chain's param PROVIDES a draw -> clean, no E0421
;
; The walk has no depth limit of its own; the only way it can fail to end is a
; cycle, bounded by `ast_decl_count` (a chain of distinct `typus` declarations
; is no longer than that), and a cycle's answer is 0 because it names no atom.
; That 0 is not silent: pass 2 reports the cycle ONCE as E0303, which rows
; 22-24 hold to an exact count. Chains are two-digit numbered (A01..A40) so the
; sources can be generated with `repeat` and no number formatter.
;
; Exit 0 = every row passed; 10+N = row N failed (and the diagnostic that row
; produced is printed first).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	call	fx_setup
	test	eax, eax
	jnz	.fail99
	lea	r14, [fx_cases]
	xor	r15, r15
  .case:
	cmp	r15, FX_NCASES
	jae	.done
	mov	rdi, [r14 + FXC_SRC]
	mov	rsi, [r14 + FXC_LEN]
	call	fx_run
	mov	r13, rax		; how many diagnostics there were
	cmp	rax, [r14 + FXC_COUNT]
	jne	.bad
	test	rax, rax
	jz	.next
	lea	rdi, [fx_diags]
	xor	rsi, rsi
	call	vec_get
	mov	r12, rax
	mov	rcx, [r14 + FXC_CODE]
	cmp	dword [r12 + Diag.code_num], ecx
	jne	.bad
	mov	rcx, [r14 + FXC_AT]
	cmp	rcx, -1			; no caret check for this row
	je	.next
	cmp	dword [r12 + Diag.span.start], ecx
	jne	.bad
	cmp	dword [r12 + Diag.span.len], 16
	jne	.bad
	; the fix §8.3 promises `EXS-E0422` -- delete the second binding
	cmp	dword [r12 + Diag.fix.kind], DIAG_FIX_DELETE
	jne	.bad
  .next:
	add	r14, FXC_SIZE
	inc	r15
	jmp	.case
  .bad:
	; show what the failing row produced, then exit 10 + row
	test	r13, r13
	jz	.bad_exit
	xor	edi, edi
	call	fx_code
  .bad_exit:
	lea	rdi, [r15 + 10]
	call	sys_exit_group
  .done:
	xor	edi, edi
	call	sys_exit_group
  .fail99:
	mov	rdi, 99
	call	sys_exit_group

include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'

; ---- harness ---------------------------------------------------------------
; `tests/unit/chk_row_layout_src.asm`'s, with ONE change, and the change is the
; point: `fx_run` does NOT bail out when the lexer or the parser has said
; something, and it returns the TOTAL length of the diagnostic vector rather
; than `chk_run`'s own count. That is what lets a row assert a SET spanning two
; passes -- "exactly {EXS-E0201}, and nothing from pass 4" is the claim spec
; §5.2 rule 3 makes, and a harness that stopped at the parser could not make
; it. Plain labels: a `proc` argument name is an unmangled global
; (docs/asm-conventions.md 4.1). Each helper pushes an odd number of
; registers, so `rsp` is 16-aligned at every call inside it.

; fx_run(rdi = source, rsi = length) -> rax = how many diagnostics the WHOLE
; front end produced (lexer, parser, Stage 1, Stage 2); -1 if the lexer failed
; outright, which no row's source does.
  fx_run:
	push	rbx
	push	r12
	push	r13
	mov	r12, rdi
	mov	r13, rsi
	lea	rdi, [fx_arena]
	call	arena_reset
	lea	rdi, [fx_scratch]
	call	arena_reset
	lea	rdi, [fx_toks]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Tok
	mov	rcx, 256
	call	vec_init
	lea	rdi, [fx_diags]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.Diag
	mov	rcx, 32
	call	vec_init
	lea	rdi, [fx_lx]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	lea	rcx, [fx_toks]
	lea	r8,  [fx_diags]
	call	lex_init
	lea	rdi, [fx_lx]
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	lex_set_source
	lea	rdi, [fx_lx]
	call	lex_run
	jc	.gate
	lea	rdi, [fx_green]
	lea	rsi, [fx_arena]
	mov	rdx, sizeof.CstGreen
	mov	rcx, 512
	call	vec_init
	lea	rdi, [fx_work]
	lea	rsi, [fx_arena]
	mov	rdx, 4
	mov	rcx, 256
	call	vec_init
	lea	rdi, [fx_cmap]
	lea	rsi, [fx_arena]
	mov	rdx, 1024
	call	map_init
	lea	rdi, [fx_ctree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	lea	rcx, [fx_green]
	lea	r8,  [fx_work]
	lea	r9,  [fx_cmap]
	call	cst_tree_init
	lea	rdi, [fx_parser]
	lea	rsi, [fx_lx]
	lea	rdx, [fx_ctree]
	call	cst_parse
	lea	rdi, [fx_tree]
	lea	rsi, [fx_arena]
	lea	rdx, [fx_names]
	call	ast_init
	lea	rdi, [fx_tree]
	lea	rsi, [fx_ctree]
	call	ast_from_cst
	lea	rdi, [fx_tree]
	call	ast_verify_stage1
	lea	rdi, [fx_chk]
	lea	rsi, [fx_tree]
	lea	rdx, [fx_diags]
	lea	rcx, [fx_scratch]
	xor	r8, r8
	call	chk_init
	lea	rdi, [fx_chk]
	mov	rsi, 64			; --hospes x86_64-linux (spec §9.5)
	call	chk_set_target
	lea	rdi, [fx_chk]
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [fx_path]
	mov	r8, FX_PATH_LEN
	call	chk_set_source
	lea	rdi, [fx_chk]
	call	chk_run
	mov	rax, [fx_diags + Vec.len]
	pop	r13
	pop	r12
	pop	rbx
	ret
  .gate:
	mov	rax, -1
	pop	r13
	pop	r12
	pop	rbx
	ret

; fx_code(rdi = i) -- render diagnostic `i` to stdout, so a failing row prints
; what it actually got instead of only a number.
  fx_code:
	push	rbx
	mov	rsi, rdi
	lea	rdi, [fx_diags]
	call	vec_get
	mov	rbx, rax
	mov	rdi, 1
	mov	rsi, rbx
	lea	rdx, [fx_buf]
	mov	rcx, 8192
	mov	r8, DIAG_MODE_TEXT
	call	diag_emit
	pop	rbx
	ret

; fx_is(edi = i, esi = code) -> eax = 1 if diagnostic `i` carries that code.
  fx_is:
	push	rbx
	push	r12
	push	r13
	mov	r12d, esi
	mov	esi, edi
	lea	rdi, [fx_diags]
	call	vec_get
	cmp	[rax + Diag.code_num], r12d
	jne	.no
	mov	eax, 1
	jmp	.out
  .no:
	xor	eax, eax
  .out:
	pop	r13
	pop	r12
	pop	rbx
	ret

; fx_setup -> eax = 0 once the arenas and the interner exist.
  fx_setup:
	push	rbx
	lea	rdi, [fx_arena]
	mov	rsi, 32 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_iarena]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_scratch]
	mov	rsi, 4 * 1024 * 1024
	call	arena_init
	jc	.bad
	lea	rdi, [fx_names]
	lea	rsi, [fx_iarena]
	mov	rdx, 1024
	call	intern_init
	xor	eax, eax
	pop	rbx
	ret
  .bad:
	mov	eax, 1
	pop	rbx
	ret


segment readable
  include '../../compiler/shared/unicode/tables/tables.inc'

  fx_path	db 'chk_e0422_alias.exsc'
  FX_PATH_LEN = $ - fx_path

; ---- the rows ---------------------------------------------------------------
; Every source is `functio f(...) -> u8 poscit alloc { sub ambitus = a; ... }`
; or a close variant; `.at` is the offset of the `sub` statement's first byte.

  fx_s0		db 'publica functio f(a: ambitus) -> u8 poscit alloc {', 10
		db '    '
    .at = $ - fx_s0
		db 'sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S0_LEN = $ - fx_s0

  fx_s1		db 'typus Amb = ambitus;', 10
		db 'publica functio f(a: Amb) -> u8 poscit alloc {', 10
		db '    '
    .at = $ - fx_s1
		db 'sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S1_LEN = $ - fx_s1

  fx_s2		db 'typus A = ambitus;', 10
		db 'typus B = A;', 10
		db 'publica functio f(a: B) -> u8 poscit alloc {', 10
		db '    '
    .at = $ - fx_s2
		db 'sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S2_LEN = $ - fx_s2

  fx_s3		db 'publica functio f(a: B) -> u8 poscit alloc {', 10
		db '    '
    .at = $ - fx_s3
		db 'sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
		db 'typus B = A;', 10
		db 'typus A = ambitus;', 10
  FX_S3_LEN = $ - fx_s3

  fx_s4		db 'typus Amb<T> = ambitus;', 10
		db 'publica functio f(a: Amb<u8>) -> u8 poscit alloc {', 10
		db '    '
    .at = $ - fx_s4
		db 'sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S4_LEN = $ - fx_s4

  fx_s5		db 'typus Amb = ambitus;', 10
		db 'publica functio f(a: Amb) -> u8 poscit ambitus {', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S5_LEN = $ - fx_s5

  fx_s6		db 'typus Amb = ambitus;', 10
		db 'publica functio f(a: Amb, b: Amb) -> u8 {', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S6_LEN = $ - fx_s6

  fx_s7		db 'typus Amb = ambitus;', 10
		db 'publica functio f(a: ambitus, b: Amb) -> u8 {', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S7_LEN = $ - fx_s7

  fx_s8		db 'typus Rte = rete;', 10
		db 'publica functio f(b: Rte) -> u8 poscit alloc {', 10
		db '    '
    .at = $ - fx_s8
		db 'sub rete = b;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S8_LEN = $ - fx_s8

  fx_s9		db 'typus Amb = ambitus;', 10
		db 'publica functio f<Amb>(a: Amb) -> u8 poscit alloc {', 10
		db '    sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S9_LEN = $ - fx_s9

  fx_s10	db 'typus A = B;', 10
		db 'typus B = A;', 10
		db 'publica functio f(a: A) -> u8 poscit alloc {', 10
		db '    sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S10_LEN = $ - fx_s10

  fx_s11	db 'typus T = ambitus;', 10
		db 'typus A<T> = T;', 10
		db 'publica functio f(a: A<u8>) -> u8 poscit alloc {', 10
		db '    sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S11_LEN = $ - fx_s11

  fx_s12	db 'typus Amb = ambitus;', 10
		db 'publica functio f(a: Amb) -> u8 {', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S12_LEN = $ - fx_s12

  fx_s13	db 'typus Amb = ambitus;', 10
		db 'publica functio f(a: Amb) -> u8 {', 10
		db '    firma s = Scriptor.ad_exitum(ambitus);', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S13_LEN = $ - fx_s13

  fx_s14	db 'publica functio f(s: Scriptor) -> u8 {', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S14_LEN = $ - fx_s14

; ---- generated chains --------------------------------------------------------
; `fx_lin n, head`: `typus A01 = head;` then `typus A<i> = A<i-1>;` for i = 2..n.
; `fx_rev n`: the same links from A<n> down to A02, so the chain is written
; AFTER its use and in reverse. `fx_gen n, head`: the generic form,
; `typus G<i><T> = G<i-1><T>;`, head `ambitus` or `T`. `fx_lin3 n, head`: the
; linear chain with three-digit names (B001..), for lengths past 99.
  macro fx_lin n, head
	db 'typus A01 = ', head, ';', 10
	repeat n - 1, i:2
		db 'typus A', '0' + i / 10, '0' + i mod 10, ' = A', '0' + (i - 1) / 10, '0' + (i - 1) mod 10, ';', 10
	end repeat
  end macro
  macro fx_rev n
	repeat n - 1, k:1
		i = n + 1 - k
		db 'typus A', '0' + i / 10, '0' + i mod 10, ' = A', '0' + (i - 1) / 10, '0' + (i - 1) mod 10, ';', 10
	end repeat
  end macro
  macro fx_lin3 n, head
	db 'typus B001 = ', head, ';', 10
	repeat n - 1, i:2
		db 'typus B', '0' + i / 100, '0' + (i / 10) mod 10, '0' + i mod 10, ' = B', '0' + (i - 1) / 100, '0' + ((i - 1) / 10) mod 10, '0' + (i - 1) mod 10, ';', 10
	end repeat
  end macro
  macro fx_gen n, head
	db 'typus G01<T> = ', head, ';', 10
	repeat n - 1, i:2
		db 'typus G', '0' + i / 10, '0' + i mod 10, '<T> = G', '0' + (i - 1) / 10, '0' + (i - 1) mod 10, '<T>;', 10
	end repeat
  end macro

  fx_s15:
		fx_lin 33, 'ambitus'
		db 'publica functio f(a: A33) -> u8 poscit alloc {', 10
		db '    '
    .at = $ - fx_s15
		db 'sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S15_LEN = $ - fx_s15

  fx_s16:
		fx_lin 40, 'ambitus'
		db 'publica functio f(a: A40) -> u8 poscit alloc {', 10
		db '    '
    .at = $ - fx_s16
		db 'sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S16_LEN = $ - fx_s16

  fx_s17:
		fx_gen 40, 'ambitus'
		db 'publica functio f(a: G40<u8>) -> u8 poscit alloc {', 10
		db '    '
    .at = $ - fx_s17
		db 'sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S17_LEN = $ - fx_s17

  fx_s18:
		fx_lin 33, 'ambitus'
		db 'publica functio f(a: A33) -> u8 {', 10
		db '    firma s = Scriptor.ad_exitum(ambitus);', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S18_LEN = $ - fx_s18

  fx_s19:
		fx_lin 40, 'ambitus'
		db 'publica functio f(a: A40, b: A40) -> u8 {', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S19_LEN = $ - fx_s19

  fx_s20:
		fx_lin 33, 'u8'
		db 'publica functio f(a: A33) -> u8 poscit alloc {', 10
		db '    sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S20_LEN = $ - fx_s20

  fx_s21:
		fx_gen 40, 'T'
		db 'publica functio f(a: G40<ambitus>) -> u8 poscit alloc {', 10
		db '    sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S21_LEN = $ - fx_s21

  fx_s22	db 'typus C = C;', 10
		db 'publica functio f(a: C) -> u8 poscit alloc {', 10
		db '    sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S22_LEN = $ - fx_s22

  fx_s23	db 'typus C1 = C2;', 10
		db 'typus C2 = C3;', 10
		db 'typus C3 = C1;', 10
		db 'publica functio f(a: C1) -> u8 poscit alloc {', 10
		db '    sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S23_LEN = $ - fx_s23

  fx_s24	db 'typus C1<T> = C2<T>;', 10
		db 'typus C2<T> = C1<T>;', 10
		db 'publica functio f(a: C1<u8>) -> u8 poscit alloc {', 10
		db '    sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S24_LEN = $ - fx_s24

  fx_s25	db 'publica functio f(a: A30) -> u8 poscit alloc {', 10
		db '    '
    .at = $ - fx_s25
		db 'sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
		fx_rev 30
		db 'typus A01 = ambitus;', 10
  FX_S25_LEN = $ - fx_s25

  fx_s26:
		fx_lin 40, 'rete'
		db 'publica functio f(a: A40) -> u8 poscit alloc {', 10
		db '    sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S26_LEN = $ - fx_s26

  fx_s27:
		fx_lin 40, 'rete'
		db 'publica functio f(a: A40) -> u8 poscit alloc {', 10
		db '    sub rete = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S27_LEN = $ - fx_s27

  fx_s28:
		fx_lin3 300, 'ambitus'
		db 'publica functio f(a: B300) -> u8 poscit alloc {', 10
		db '    '
    .at = $ - fx_s28
		db 'sub ambitus = a;', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S28_LEN = $ - fx_s28

  fx_s29:
		fx_lin3 300, 'ambitus'
		db 'publica functio f(a: B300) -> u8 {', 10
		db '    firma s = Scriptor.ad_exitum(ambitus);', 10
		db '    redde 0;', 10
		db '}', 10
  FX_S29_LEN = $ - fx_s29

; one row: source, length, expected diagnostic count, the first one's code,
; the offset of its caret (-1: not checked). A row with a caret is held to the
; `sub` statement's own 16 bytes (`sub ambitus = a;`) and to the delete-the-
; second-binding fix; row 8's `sub rete = b;` is 13 bytes and rows 5-7 raise at
; a row item or a parameter, so those carry -1 and are held to count and code.
  FXC_SRC	= 0
  FXC_LEN	= 8
  FXC_COUNT	= 16
  FXC_CODE	= 24
  FXC_AT	= 32
  FXC_SIZE	= 40

  fx_cases:
	dq fx_s0,  FX_S0_LEN,  1, 422, fx_s0.at
	dq fx_s1,  FX_S1_LEN,  1, 422, fx_s1.at
	dq fx_s2,  FX_S2_LEN,  1, 422, fx_s2.at
	dq fx_s3,  FX_S3_LEN,  1, 422, fx_s3.at
	dq fx_s4,  FX_S4_LEN,  1, 422, fx_s4.at
	dq fx_s5,  FX_S5_LEN,  1, 422, -1
	dq fx_s6,  FX_S6_LEN,  1, 422, -1
	dq fx_s7,  FX_S7_LEN,  1, 422, -1
	dq fx_s8,  FX_S8_LEN,  1, 422, -1
	dq fx_s9,  FX_S9_LEN,  1, 303, -1
	dq fx_s10, FX_S10_LEN, 1, 303, -1
	dq fx_s11, FX_S11_LEN, 1, 303, -1
	dq fx_s12, FX_S12_LEN, 0, 0, -1
	dq fx_s13, FX_S13_LEN, 0, 0, -1
	dq fx_s14, FX_S14_LEN, 0, 0, -1
	dq fx_s15, FX_S15_LEN, 1, 422, fx_s15.at
	dq fx_s16, FX_S16_LEN, 1, 422, fx_s16.at
	dq fx_s17, FX_S17_LEN, 1, 422, fx_s17.at
	dq fx_s18, FX_S18_LEN, 0, 0, -1
	dq fx_s19, FX_S19_LEN, 1, 422, -1
	dq fx_s20, FX_S20_LEN, 1, 303, -1
	dq fx_s21, FX_S21_LEN, 1, 303, -1
	dq fx_s22, FX_S22_LEN, 1, 303, -1
	dq fx_s23, FX_S23_LEN, 1, 303, -1
	dq fx_s24, FX_S24_LEN, 1, 303, -1
	dq fx_s25, FX_S25_LEN, 1, 422, fx_s25.at
	dq fx_s26, FX_S26_LEN, 1, 303, -1
	dq fx_s27, FX_S27_LEN, 1, 422, -1
	dq fx_s28, FX_S28_LEN, 1, 422, fx_s28.at
	dq fx_s29, FX_S29_LEN, 0, 0, -1
  FX_NCASES = ($ - fx_cases) / FXC_SIZE

segment readable writeable
  fx_arena:	rb sizeof.Arena
  fx_iarena:	rb sizeof.Arena
  fx_scratch:	rb sizeof.Arena
  fx_names:	rb sizeof.Interner
  fx_toks:	rb sizeof.Vec
  fx_diags:	rb sizeof.Vec
  fx_lx:	rb sizeof.Lexer
  fx_green:	rb sizeof.Vec
  fx_work:	rb sizeof.Vec
  fx_cmap:	rb sizeof.Map
  fx_ctree:	rb sizeof.CstTree
  fx_parser:	rb sizeof.CstParser
  fx_tree:	rb sizeof.Ast
  fx_chk:	rb sizeof.ChkCtx
  fx_buf:	rb 8192
