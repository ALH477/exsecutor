; tests/unit/chk_ty_discerne.asm
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
; checker fixture -- a `casus` pattern is typed AGAINST THE SCRUTINEE (spec
; §8.5, §8.6's `Pattern ::= Literal | Path`), exactly as the right operand
; of `s eq p` is: checker/types/stmt.inc's `.discerne`, `__chk_ty_want`.
;
; WHY IT EXISTS. The pattern used to be skipped -- only the scrutinee and
; each arm's block were typed -- so a literal pattern reached the lowering
; with `Node.ty` 0 and no `konst`, and every `discerne` with one killed
; `exsc aedifica -o` with SIGILL (`__lwr_lit`'s `__lwr_nty` assert) while the
; same source checked clean without `-o`. And, the other half, a pattern
; that did not fit or did not match was accepted: `casus 256` on a `u8`,
; `casus K` with `K: u64`. tests/programs/discerne/ runs the accepted shapes.
;
; EVERY REJECTED ROW PINS THE COUNT, THE CODE AND THE OFFSET, and the rules
; have accepted twins, so a checker that rejected every pattern fails the
; twins and one that typed none fails the rest:
;
;   rule                             rejected              accepted twin
;   a literal takes the scrutinee's  u8 casus 256          u8 casus 255
;     width (E0308) ... and sign     i8 casus 200          i8 casus 127
;   a constant is the scrutinee's    u8 casus K (K: u64)   u8 casus K (K: u8)
;     type (E0303) ... mensura is    mensura casus K       mensura casus 8
;     its own type                     (K: u64)
;   a `textus` literal (E0303)       u64 casus "a"
;   a function (E0303)               u64 casus g
;   every arm judged, in arm order   u8 casus 256, casus K (K: u16) -- two
;   u1 and a hex edge                                      u1 casus 1, casus 0;
;                                                          u32 casus 0xFFFFFFFF
;   the literal's type, read back                          u16 casus 5
;
; THE LAST CHECK READS THE TYPES, not the diagnostics: in the accepted
; `discerne x { casus 5 { } }` with `x: u16`, the literal's `Node.ty` must be
; `u16` -- kind int, unsigned, width 16. "No diagnostic" alone was also the
; answer of the checker that never looked at the pattern, which is the bug.
;
; NOT JUDGED HERE, because nothing in spec §13 can be said about them:
; duplicate `casus` values, a `discerne` with no `aliter` (spec §8.5 says
; the arms are exhaustive; §8.6 lists that as presupposing an enumeration
; that is `[OPEN]`), and a `Path` pattern naming a non-constant (a
; `mutabilis` binding, a parameter -- lower/stmt.inc's `__lwr_pattern` traps
; on those, for want of a code).
;
; Exit 0 = every check passed; 10+N = table row N failed (the diagnostics the
; row did produce are rendered first); 41 = the pattern literal has no type,
; 42 = its type is not u16; 99 = setup.
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

FX_ROW = 40		; dq src, len; dd count, code0, off0, code1, off1, pad
FX_ANY = -1		; "do not check this offset"

segment readable executable
  start:
	call	fx_setup
	test	eax, eax
	jnz	.fail99

	xor	r12, r12		; row index
  .row:
	cmp	r12, FX_NROWS
	jae	.rows_done
	mov	rax, r12
	imul	rax, FX_ROW
	lea	r13, [fx_tab]
	add	r13, rax
	mov	rdi, [r13]
	mov	rsi, [r13 + 8]
	call	fx_run
	mov	r14, rax
	; render whatever the row produced, so a failure shows it
	xor	ebx, ebx
  .show:
	cmp	rbx, r14
	jae	.shown
	mov	rdi, rbx
	call	fx_code
	inc	rbx
	jmp	.show
  .shown:
	mov	ecx, [r13 + 16]
	cmp	r14, rcx
	jne	.row_bad
	test	r14, r14
	jz	.row_ok
	xor	edi, edi
	mov	esi, [r13 + 20]
	mov	edx, [r13 + 24]
	call	fx_diag_is
	test	eax, eax
	jz	.row_bad
	cmp	r14, 2
	jb	.row_ok
	mov	edi, 1
	mov	esi, [r13 + 28]
	mov	edx, [r13 + 32]
	call	fx_diag_is
	test	eax, eax
	jz	.row_bad
  .row_ok:
	inc	r12
	jmp	.row
  .row_bad:
	lea	rdi, [r12 + 11]
	call	sys_exit_group
  .rows_done:


	; ---- the pattern literal has the scrutinee's type --------------------
	lea	rdi, [fx_a01]
	mov	rsi, fx_a01_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail41
	mov	edi, AST_LIT
	call	fx_first_ty
	test	rax, rax
	jz	.fail41
	lea	rdi, [fx_tree]
	mov	rsi, rax
	call	ast_type_at
	cmp	byte [rax + AstType.kind], AST_TY_INT
	jne	.fail42
	cmp	byte [rax + AstType.sign], AST_SIGN_U
	jne	.fail42
	cmp	dword [rax + AstType.width], 16
	jne	.fail42

	xor	edi, edi
	call	sys_exit_group
  .fail41:
	mov	edi, 41
	call	sys_exit_group
  .fail42:
	mov	edi, 42
	call	sys_exit_group
  .fail99:
	mov	edi, 99
	call	sys_exit_group



include '../../compiler/x86_64/cst/cst.inc'
include '../../compiler/x86_64/ast/ast.inc'
include '../../compiler/x86_64/checker/checker.inc'

; ---- harness ---------------------------------------------------------------
; Plain labels, as in tests/unit/cst_error_tolerance.asm: a `proc` argument
; name is an unmangled global. Each helper pushes an odd number of registers,
; so `rsp` is 16-aligned at every call inside it.

; fx_run(rdi = source, rsi = length) -> rax = the diagnostics `chk_run`
; appended; -1 if the lexer or the parser said anything (the row's source is
; then wrong, and no count can match it).
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
	cmp	qword [fx_diags + Vec.len], 0
	jne	.gate
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

; fx_code(rdi = i) -- render diagnostic `i` to stdout.
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

; fx_diag_is(edi = i, esi = code, edx = span start or FX_ANY) -> eax = 1 if
; diagnostic `i` has that code at that offset.
  fx_diag_is:
	push	rbx
	push	r12
	push	r13
	mov	r12d, esi
	mov	r13d, edx
	mov	esi, edi
	lea	rdi, [fx_diags]
	call	vec_get
	cmp	[rax + Diag.code_num], r12d
	jne	.no
	cmp	r13d, FX_ANY
	je	.yes
	cmp	[rax + Diag.span.start], r13d
	jne	.no
  .yes:
	mov	eax, 1
	jmp	.out
  .no:
	xor	eax, eax
  .out:
	pop	r13
	pop	r12
	pop	rbx
	ret

; fx_first_ty(edi = node kind) -> rax = `Node.ty` of the first node of that
; kind in the last tree, 0 if there is none.
  fx_first_ty:
	push	rbx
	push	r12
	push	r13
	mov	r12d, edi
	mov	r13, 1
  .scan:
	lea	rdi, [fx_tree]
	call	ast_node_count
	cmp	r13, rax
	ja	.none
	lea	rdi, [fx_tree]
	mov	rsi, r13
	call	ast_node_at
	movzx	ecx, word [rax + AstNode.kind]
	cmp	ecx, r12d
	je	.hit
	inc	r13
	jmp	.scan
  .hit:
	mov	eax, [rax + AstNode.ty]
	jmp	.out
  .none:
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

  fx_path	db 'chk_ty_discerne.exsc'
  FX_PATH_LEN = $ - fx_path

  ; Offsets are into each row's own source: a first line and its `\n` (the
  ; constant or function the row needs, or a `// -` comment), the signature
  ; line and its `\n`, then four spaces and the `discerne` line. Computed,
  ; then confirmed by the render.
  ;
  ; THE CONSTANT COMES FIRST ON PURPOSE. A module `firma` named above its
  ; declaration has `Decl.ty` 0 when the body is checked, and
  ; `__chk_ty_path_expr` answers the ERROR type for it with no diagnostic --
  ; so `casus K` against a forward `K: u64` is accepted, as `x eq K` is. That
  ; is a checker ordering bug, not `discerne`'s; declared first, `K` has its
  ; type and these rows test the pattern rule rather than that hole.
  macro fx_src name, pre, sig, body
	name: db pre, 10, sig, 10, '    ', body, 10, '    redde 0;', 10, '}', 10
	name#_LEN = $ - name
  end macro

  fx_src fx_c01, '// -',              'publica functio f(x: u8) -> u8 {',      'discerne x { casus 256 { } }'
  fx_src fx_c02, '// -',              'publica functio f(x: i8) -> u8 {',      'discerne x { casus 200 { } }'
  fx_src fx_c03, 'firma K: u64 = 3;', 'publica functio f(x: u8) -> u8 {',      'discerne x { casus K { } }'
  fx_src fx_c04, 'firma K: u64 = 3;', 'publica functio f(x: mensura) -> u8 {', 'discerne x { casus K { } }'
  fx_src fx_c05, '// -',              'publica functio f(x: u64) -> u8 {',     'discerne x { casus "a" { } }'
  fx_src fx_c06, 'functio g() -> u64 { redde 1; }', \
		 'publica functio f(x: u64) -> u8 {',     'discerne x { casus g { } }'
  fx_src fx_c07, 'firma K: u16 = 3;', 'publica functio f(x: u8) -> u8 {',      'discerne x { casus 256 { } casus K { } }'
  fx_src fx_a01, '// -',              'publica functio f(x: u16) -> u8 {',     'discerne x { casus 5 { } }'
  fx_src fx_a02, '// -',              'publica functio f(x: u8) -> u8 {',      'discerne x { casus 255 { } }'
  fx_src fx_a03, '// -',              'publica functio f(x: i8) -> u8 {',      'discerne x { casus 127 { } }'
  fx_src fx_a04, 'firma K: u8 = 3;',  'publica functio f(x: u8) -> u8 {',      'discerne x { casus K { } aliter { } }'
  fx_src fx_a05, '// -',              'publica functio f(x: mensura) -> u8 {', 'discerne x { casus 8 { } }'
  fx_src fx_a06, '// -',              'publica functio f(x: u1) -> u8 {',      'discerne x { casus 1 { } casus 0 { } }'
  fx_src fx_a07, '// -',              'publica functio f(x: u32) -> u8 {',     'discerne x { casus 0xFFFFFFFF { } }'

  fx_tab:
	; u8 casus 256: the literal took the scrutinee's u8 and does not fit it
	dq fx_c01, fx_c01_LEN
	dd 1, 308, 61, 0, 0, 0
	; i8 casus 200: nor does 200 fit i8
	dq fx_c02, fx_c02_LEN
	dd 1, 308, 61, 0, 0, 0
	; u8 casus K, K: u64 -- E0303 at the pattern
	dq fx_c03, fx_c03_LEN
	dd 1, 303, 74, 0, 0, 0
	; mensura casus K, K: u64 -- two unsigned integers, two types
	dq fx_c04, fx_c04_LEN
	dd 1, 303, 79, 0, 0, 0
	; u64 casus "a": a textus is not a u64
	dq fx_c05, fx_c05_LEN
	dd 1, 303, 62, 0, 0, 0
	; u64 casus g, g a function: not a u64 either
	dq fx_c06, fx_c06_LEN
	dd 1, 303, 89, 0, 0, 0
	; two wrong arms: one diagnostic each, in arm order
	dq fx_c07, fx_c07_LEN
	dd 2, 308, 74, 303, 88, 0
	; the accepted twins
	dq fx_a01, fx_a01_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_a02, fx_a02_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_a03, fx_a03_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_a04, fx_a04_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_a05, fx_a05_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_a06, fx_a06_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_a07, fx_a07_LEN
	dd 0, 0, 0, 0, 0, 0
  FX_NROWS = ($ - fx_tab) / FX_ROW

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
