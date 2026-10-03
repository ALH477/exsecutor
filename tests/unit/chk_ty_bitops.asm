; tests/unit/chk_ty_bitops.asm
; SPDX-License-Identifier: GPL-3.0-or-later
; Copyright (C) 2026 DeMoD LLC.
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
; checker fixture -- the typing rules of `aut`, `atque`, `sive`, `sursum`,
; `deorsum` (spec
; §5.4, Integers; docs/design/wire-codec.md D1; the spec cites this fixture
; as what exercises those rules), through the whole front end: checker/types/types.inc's `.bitop`
; arm and `__chk_ty_expect`'s `.bin` arm.
;
; `atque` AND `sive` JOINED THIS FIXTURE ON 2026-09-25, with §5.4's amendment
; that defines them as "arithmetic on unsigned integers under exactly the
; rules `aut` has above". THAT is what rows b01..b10 and g01..g07 below pin,
; and they pin it the only way the claim can be checked: each is `aut`'s own
; row with the word swapped, expecting the same count, the same code and the
; same offset. One rule, five words -- so a second copy of the rule that
; drifted from the first fails here rather than in a program.
;
; TWO OF THE NEW ROWS ARE A DEFECT'S GRAVE, not a restatement. Until the
; checker arm widened, `__chk_ty_expect`'s `.bin_aut` tested
; `AST_OP_AUT .. AST_OP_DEORSUM` (17..19) and fell through for anything above
; it -- and `atque`/`sive` are 22/23, because ast/kinds.inc is appended to and
; `/` (20) and `residuum` (21) sit between the two ranges. So the PENDING case
; of a bitwise word reached `.bin_push` with no admissibility test at all.
; Measured against the pre-fix compiler, 2026-09-26:
;   b05  `redde 1 atque 2;` -> i8   compiled CLEAN, exit 0, and emitted an
;                                   `and` on signed operands -- silently the
;                                   thing §5.4 forbids
;   b06  `redde 1 sive 2;`  -> i8   the same, exit 0
;   b07  `redde 1.5 atque 2.5;` -> f64
;                                   reached the BACKEND and died there:
;                                   `bfa: emitter: unsupported type (an
;                                   integer uN/iN, or a 64-bit address)`,
;                                   exit 4 -- a compiler failure where a
;                                   diagnostic was owed
; All three are now one `EXS-E0305`, and the loud half of the same gap (the
; `rassert` in `__chk_ty_binary`, which is what a SETTLED `atque` tripped) is
; gone with them. The quiet half outlived the loud one precisely because it
; was quiet; that is why these three rows exist and not merely the settled
; ones.
;
; EVERY REJECTED ROW PINS THE COUNT, THE CODE AND THE OFFSET, and every rule
; has an accepted twin, so a checker that rejected everything fails the
; twins and one that accepted everything fails the rest:
;
;   rule                                         rejected         accepted twin
;   unsigned operands only (E0305)               u8 aut i8        u8 aut u8
;     ... a pending literal is not blamed        i8 deorsum 1     u16 sursum 1
;     ... each operand judged on its own         i8 sursum i8 (2)
;     ... a float is not an unsigned integer     u32 aut f32
;     ... an EXPECTATION is judged too           1 aut 2 -> i8    1 aut 2 -> u8
;   one type both sides (E0303)                  u8 aut u16       u1 aut u1
;     ... the shift count included               u16 sursum u8
;     ... mensura is its own type                mensura aut u64  mensura deorsum 1
;   E0305 before E0303                           u8 aut i8 (one diagnostic, the E0305)
;   the result is the operand type               u8 aut u8 -> u16 u8 aut u8 -> u8
;   a literal takes the other side's type        u16 aut 65536    u16 aut 65535
;
; and the same table again for the two words §5.4 added on 2026-09-25, whose
; rows are `aut`'s with the word swapped and the expectation unchanged:
;
;   rule                                         rejected           accepted
;   unsigned operands only (E0305)               u8 atque i8 (b01)  u8 atque u8 (g01)
;     ... each operand judged on its own         i8 sive i8 (b02, 2) u8 sive u8 (g02)
;     ... a float is not an unsigned integer     u32 sive f32 (b04)
;     ... an EXPECTATION is judged too           1 atque 2 -> i8 (b05)  -> u8 (g06)
;                                                1 sive 2 -> i8 (b06)
;                                                1.5 atque 2.5 -> f64 (b07)
;   one type both sides (E0303)                  u8 atque u16 (b03) u1 atque u1 (g03)
;     ... mensura is its own type                mensura atque u64 (b10) mensura sive 1 (g04)
;   E0305 before E0303                           u8 atque i8 (b01, one diagnostic)
;   the result is the operand type               u8 sive u8 -> u16 (b09)
;   a literal takes the other side's type        u16 atque 65536 (b08) u16 atque 65535 (g05)
;
; g07 IS THE ONE ROW WITH NO `aut` TWIN: `(a atque b) sive 1`, two different
; bitwise words in one expression, PARENTHESISED. §5.4 refuses the bare chain
; `a atque b sive 1` as `EXS-E0201` at the second word -- but that is a PARSE
; refusal (cst/parse.inc's `__cst_xor` simply stops at a different word;
; tests/unit/cst_bitand_or.asm rows 3, 4 and 5 pin it), and a front-end
; diagnostic makes `fx_run` answer -1 rather than testing the checker. So the
; bare chain is deliberately NOT a row here, and g07 is the other half of
; §5.4's sentence: once the brackets are written, the checker types the nest
; like any other, and "parenthesise" is advice a program can actually take.
;
; `u8 aut i8` is BOTH the E0305 row and the ordering row: spec §5.4 says it is
; "`EXS-E0305` at the `i8`, not `EXS-E0303`; one diagnostic". A checker that
; paired first would report 303; one that did both would report two.
;
; THE LAST CHECK READS THE TYPES, not the diagnostics: in the accepted
; `c sursum 1` with `c: u16`, the literal's `Node.ty` and the `Binary`'s must
; be one id, and that id must be `u16` -- kind int, unsigned, width 16. "No
; diagnostic" alone would also be the answer of a checker that left the
; literal pending and never looked.
;
; MENSURA. Spec §5.2 calls it "`mensura` (`usize`)" -- an unsigned integer of
; the target's width -- and §5.4's rule is "unsigned operands only", so it is
; admitted (checker/types/types.inc, `__chk_ty_unsigned`, says why at length).
; It remains its own type, so pairing it with `u64` is E0303.
;
; Exit 0 = every check passed; 10+N = table row N failed (the diagnostics the
; row did produce are rendered first); 201 = the literal is not the Binary's
; type, 202 = that type is not u16; 99 = setup.
;
; THE TYPE-CHECK CODES WERE 41 AND 42 AND MOVED TO 201/202 when `atque` and
; `sive` brought the table to 34 rows. `10+N` then reaches 44, so 41 and 42 had
; become ambiguous between "row 30 failed" and "the literal took the wrong
; type" -- a fixture whose failure code does not say what failed. Any table
; growing past 31 rows has this collision; 201/202 clears it with room for 190
; rows, and an exit status is a `u8` so nothing above 255 is available anyway.
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

	; ---- the literal takes the other side's type -------------------------
	lea	rdi, [fx_a01]
	mov	rsi, fx_a01_LEN
	call	fx_run
	test	rax, rax
	jnz	.fail41
	mov	edi, AST_LIT
	call	fx_first_ty
	mov	r12, rax
	mov	edi, AST_BINARY
	call	fx_first_ty
	cmp	rax, r12
	jne	.fail41
	lea	rdi, [fx_tree]
	mov	rsi, r12
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
	mov	edi, 201
	call	sys_exit_group
  .fail42:
	mov	edi, 202
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

  fx_path	db 'chk_ty_bitops.exsc'
  FX_PATH_LEN = $ - fx_path

  ; Offsets are into each row's own source; line 1 ends with its `\n`, and
  ; `redde` is indented four spaces. Computed, then confirmed by the render.
  macro fx_src name, line1, line2
	name: db line1, 10, '    ', line2, 10, '}', 10
	name#_LEN = $ - name
  end macro

  fx_src fx_c01, 'publica functio f(a: u8, b: i8) -> u8 {',            'redde a aut b;'
  fx_src fx_c02, 'publica functio f(a: i8) -> i8 {',                   'redde a deorsum 1;'
  fx_src fx_c03, 'publica functio f(a: i8, b: i8) -> i8 {',            'redde a sursum b;'
  fx_src fx_c04, 'publica functio f(a: u8, b: u16) -> u8 {',           'redde a aut b;'
  fx_src fx_c05, 'publica functio f(a: u16, b: u8) -> u16 {',          'redde a sursum b;'
  fx_src fx_c06, 'publica functio f(x: f32, y: u32) -> u32 {',         'redde y aut x;'
  fx_src fx_c07, 'publica functio f() -> i8 {',                        'redde 1 aut 2;'
  fx_src fx_c08, 'publica functio f(a: u8, b: u8) -> u16 {',           'redde a aut b;'
  fx_src fx_c09, 'publica functio f(n: mensura, x: u64) -> mensura {', 'redde n aut x;'
  fx_src fx_c10, 'publica functio f(c: u16) -> u16 {',                 'redde c aut 65536;'
  fx_src fx_a01, 'publica functio f(c: u16) -> u16 {',                 'redde c sursum 1;'
  fx_src fx_a02, 'publica functio f(c: u16) -> u16 {',                 'redde 1 aut c;'
  fx_src fx_a03, 'publica functio f() -> u8 {',                        'redde 1 aut 2;'
  fx_src fx_a04, 'publica functio f(n: mensura) -> mensura {',         'redde n deorsum 1;'
  fx_src fx_a05, 'publica functio f(c: u16) -> u16 {',                 'redde c aut 65535;'
  fx_src fx_a06, 'publica functio f(a: u1, b: u1) -> u1 {',            'redde a aut b;'
  fx_src fx_a07, 'publica functio f(a: u8, b: u8) -> u8 {',            'redde a aut b;'

  ; `atque` and `sive` (§5.4, 2026-09-25) -- `aut`'s rows with the word
  ; swapped. Offsets were read off the real compiler's JSON spans
  ; (`--diagnostica json`, `span.start`) on these exact sources, not counted
  ; by hand.
  fx_src fx_b01, 'publica functio f(a: u8, b: i8) -> u8 {',            'redde a atque b;'
  fx_src fx_b02, 'publica functio f(a: i8, b: i8) -> i8 {',            'redde a sive b;'
  fx_src fx_b03, 'publica functio f(a: u8, b: u16) -> u8 {',           'redde a atque b;'
  fx_src fx_b04, 'publica functio f(x: f32, y: u32) -> u32 {',         'redde y sive x;'
  fx_src fx_b05, 'publica functio f() -> i8 {',                        'redde 1 atque 2;'
  fx_src fx_b06, 'publica functio f() -> i8 {',                        'redde 1 sive 2;'
  fx_src fx_b07, 'publica functio f() -> f64 {',                       'redde 1.5 atque 2.5;'
  fx_src fx_b08, 'publica functio f(c: u16) -> u16 {',                 'redde c atque 65536;'
  fx_src fx_b09, 'publica functio f(a: u8, b: u8) -> u16 {',           'redde a sive b;'
  fx_src fx_b10, 'publica functio f(n: mensura, x: u64) -> mensura {', 'redde n atque x;'
  fx_src fx_g01, 'publica functio f(a: u8, b: u8) -> u8 {',            'redde a atque b;'
  fx_src fx_g02, 'publica functio f(a: u8, b: u8) -> u8 {',            'redde a sive b;'
  fx_src fx_g03, 'publica functio f(a: u1, b: u1) -> u1 {',            'redde a atque b;'
  fx_src fx_g04, 'publica functio f(n: mensura) -> mensura {',         'redde n sive 1;'
  fx_src fx_g05, 'publica functio f(c: u16) -> u16 {',                 'redde c atque 65535;'
  fx_src fx_g06, 'publica functio f() -> u8 {',                        'redde 1 sive 2;'
  fx_src fx_g07, 'publica functio f(a: u8, b: u8) -> u8 {',            'redde (a atque b) sive 1;'

  fx_tab:
	; u8 aut i8: E0305 at the `i8` and NOTHING ELSE -- not E0303 (§5.4)
	dq fx_c01, fx_c01_LEN
	dd 1, 305, 56, 0, 0, 0
	; i8 deorsum 1: E0305 at `a`; the literal is not blamed for adopting i8
	dq fx_c02, fx_c02_LEN
	dd 1, 305, 43, 0, 0, 0
	; i8 sursum i8: both operands are wrong on their own -- one each
	dq fx_c03, fx_c03_LEN
	dd 2, 305, 50, 305, 59, 0
	; u8 aut u16: E0303 at the right operand
	dq fx_c04, fx_c04_LEN
	dd 1, 303, 57, 0, 0, 0
	; u16 sursum u8: the shift count is held to the same type
	dq fx_c05, fx_c05_LEN
	dd 1, 303, 61, 0, 0, 0
	; u32 aut f32: a float is not an unsigned integer
	dq fx_c06, fx_c06_LEN
	dd 1, 305, 59, 0, 0, 0
	; 1 aut 2 where i8 is expected: E0305 at the operator's node, once
	dq fx_c07, fx_c07_LEN
	dd 1, 305, 38, 0, 0, 0
	; u8 aut u8 is u8, so returning it as u16 is E0303 at the expression
	dq fx_c08, fx_c08_LEN
	dd 1, 303, 51, 0, 0, 0
	; mensura aut u64: two unsigned integers, two types
	dq fx_c09, fx_c09_LEN
	dd 1, 303, 67, 0, 0, 0
	; u16 aut 65536: the literal took u16 and does not fit it
	dq fx_c10, fx_c10_LEN
	dd 1, 308, 51, 0, 0, 0
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

	; ---- `atque` and `sive`: the same rules, the same codes, the same
	; offsets (§5.4, 2026-09-25) ----------------------------------------
	; u8 atque i8: E0305 at the `i8`, one diagnostic -- the ordering row
	dq fx_b01, fx_b01_LEN
	dd 1, 305, 58, 0, 0, 0
	; i8 sive i8: both operands wrong on their own -- one each
	dq fx_b02, fx_b02_LEN
	dd 2, 305, 50, 305, 57, 0
	; u8 atque u16: E0303 at the right operand
	dq fx_b03, fx_b03_LEN
	dd 1, 303, 59, 0, 0, 0
	; u32 sive f32: a float is not an unsigned integer
	dq fx_b04, fx_b04_LEN
	dd 1, 305, 60, 0, 0, 0
	; 1 atque 2 where i8 is expected: E0305 at the operator, once. THE
	; PENDING ROW -- exit 0 and a signed `and` before the fix
	dq fx_b05, fx_b05_LEN
	dd 1, 305, 38, 0, 0, 0
	; 1 sive 2 where i8 is expected: the second word, same answer
	dq fx_b06, fx_b06_LEN
	dd 1, 305, 38, 0, 0, 0
	; 1.5 atque 2.5 where f64 is expected: E0305 at the operator. Before
	; the fix this reached the emitter -- "unsupported type", exit 4
	dq fx_b07, fx_b07_LEN
	dd 1, 305, 39, 0, 0, 0
	; u16 atque 65536: the literal took u16 and does not fit it
	dq fx_b08, fx_b08_LEN
	dd 1, 308, 53, 0, 0, 0
	; u8 sive u8 is u8, so returning it as u16 is E0303
	dq fx_b09, fx_b09_LEN
	dd 1, 303, 51, 0, 0, 0
	; mensura atque u64: two unsigned integers, two types
	dq fx_b10, fx_b10_LEN
	dd 1, 303, 69, 0, 0, 0
	; the accepted twins
	dq fx_g01, fx_g01_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_g02, fx_g02_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_g03, fx_g03_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_g04, fx_g04_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_g05, fx_g05_LEN
	dd 0, 0, 0, 0, 0, 0
	dq fx_g06, fx_g06_LEN
	dd 0, 0, 0, 0, 0, 0
	; the parenthesised mix -- §5.4's "parenthesise", taken
	dq fx_g07, fx_g07_LEN
	dd 0, 0, 0, 0, 0, 0
  FX_NROWS = ($ - fx_tab) / FX_ROW
  ; a row of the wrong width would shift every row after it
  assert ($ - fx_tab) mod FX_ROW = 0

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
