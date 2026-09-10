; compiler/x86_64/backend_fasmg/parse.inc
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
; The IR text parser: docs/design/ssa-ir.md section 2.11. Line-oriented,
; whitespace-split, `;`-to-EOL comments, no commas/brackets/nesting in an
; instruction Line -- cheap on purpose. One `Vec` per numbered kind (`%N` ->
; internal value id), no symbol table; an out-of-sequence `%N` is a parse
; error by design. `phi`/`call`/`callind` read to end of line.
;
; NO STATIC DATA IN THIS FILE, ANYWHERE, INCLUDING STRING LITERALS. This is
; an `include`d library, exactly like every rt/ file, and rt/'s own files
; (grepped before writing this one) never declare a bare `db`/`dq`/`rb`
; outside a `struct` block either -- every existing fixture's data lives in
; ITS OWN, single, final `segment readable writeable` block, and a `proc`
; only ever emits CODE. Every fixture in this tree also proves that `proc`
; bodies emitted BEFORE any explicit `segment` directive (map_basic.asm:
; `record_cb` is defined between `include 'map.inc'` and the first
; `segment`) still land correctly -- but nothing establishes that a bare `db`
; would land somewhere WRITEABLE if this file emitted one the same way, and
; a mutable buffer that turns out to sit in a read-only area is a runtime
; segfault, not an assemble-time error. So every string constant this file
; needs (error messages, opcode/keyword spellings) is emitted INLINE, in a
; PROC body, via `jmp` over a `db` (the "jmp over literal" idiom used
; throughout, below) -- unambiguously code, and proven to work wherever a
; `proc` may be placed. Every SCRATCH buffer is a `locals`/`slot` (stack) or
; comes from an arena; nothing here is a static mutable global.
;
; TWO NUMBERING SPACES, deliberately not conflated (ir.inc's header explains
; why): INTERNAL value/block ids (1-based `Vec` index, dense over ALL
; instructions including void ones) versus TEXT `%N` (dense only over
; VALUE-PRODUCING lines). `p.val_map` is the one real translation table this
; file needs. Block ids need no such table: every block header is
; `bN:`-with-colon and produces exactly one internal block per text number,
; in order, so internal id = text N + 1 by construction once the PRESCAN
; below has created them.
;
; FORWARD REFERENCES: the ONE case (section 2.11) is a phi naming a `%N`
; defined later in the same function. Blocks never need this -- every block
; header is discovered by a PRESCAN over the function body before the real
; pass resolves a single operand. Phi VALUE operands cannot be prescanned
; this way, so those alone go through `p.fixups`, resolved once the
; function's `val_map` is complete.
;
; ERROR PHILOSOPHY (this file's own judgment call, an extension of
; docs/design/ssa-ir.md section 3's framing, not quoted spec text):
; malformed IR TEXT is never a source-level `EXS-E` diagnostic -- section 3
; already says as much for structurally-valid-but-semantically-wrong IR
; ("malformed IR is a compiler bug"), and text this parser cannot even
; tokenize into that structure is the same kind of bug, one level earlier.
; Every error here is FATAL: `__bfa_die` writes a message to stderr (fd 2,
; allowlisted) naming the line and the reason, then `sys_exit_group`
; (allowlisted) with a distinct nonzero code. Deliberately not the `CF`/`eax`
; protocol (docs/asm-conventions.md, "1.3") -- there is no caller left to
; hand a recoverable error to, the same reasoning that protocol's own fourth
; channel (`rassert`) already applies to internal contract violations; this
; is that channel with a message instead of a bare trap, because this tool's
; only input is hand-written text someone will get wrong.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md section 9.2. Design contract:
; docs/design/ssa-ir.md, section 2.11 (grammar), section 2.1 (seal-late
; already implies two literal passes is a legitimate reading of it, with no
; builder driving construction live).
; -----------------------------------------------------------------------------

include 'ir.inc'



; ---- BfaTok: a (ptr,len) token slice. Not `rt/str.inc`'s `Slice` -- that
; file cannot be reached without re-`include`-ing `arena.inc` a second time
; through a different path (the exact diamond rt/vec.inc's header warns
; about), so this module defines its own 16-byte pair instead of fighting
; the include graph for two fields' worth of savings.
struct BfaTok
	field tp,  dq		; NOT `ptr` -- fine as a proc arg (rt/map.inc uses
				; it), but this is a STRUCT field and `Tok.ptr`
				; reads no worse as `Tok.tp`; kept short to
				; match `len` beside it.
	field tl,  dq
end struct

; ---- BfaFixup: one pending phi-operand forward reference.
struct BfaFixup
	field extra_idx, dd
	field textn,     dd
end struct

; ---- BfaParser: all parser state lives here so every internal routine
; threads ONE pointer instead of a fistful of arguments (`proc` tops out at
; six, docs/asm-conventions.md section 3.1). `pending_result`/`pending_textn`
; and the current line's span bounds are set once per line, by the
; dispatcher, and read by whichever per-shape routine that line's opcode
; hands off to -- see `__bfa_ps_finish`.
struct BfaParser
	field module,          dq
	field ir_arena,        dq
	field scratch,         dq	; reset once per function
	field src_ptr,         dq
	field src_len,         dq
	field pos,             dq
	field line_ptr,        dq	; current line, comment-stripped
	field line_len,        dq
	field line_no,         dq	; 1-based
	field func,            dq	; current BfaFunc*, 0 between functions
	field curblock,        dq	; current internal block id, 0 = none yet
	field val_map,         dq	; -> Vec<dd>, index = text %N
	field next_valn,       dq	; next expected text %N
	field fixups,          dq	; -> Vec<BfaFixup>
	field optab,           dq	; -> Map, opcode text -> packed dispatch word
	field ntoks,           dq
	field tokpos,          dq
	field toks,            dq	; -> Vec<BfaTok>
	field pending_result,  dq	; 1 if this line had a `%N =` prefix
	field pending_textn,   dq	; that N, only meaningful if pending_result
	field pending_off,     dq	; current line's span start (byte offset in src)
	field pending_len,     dq	; current line's span length
end struct

; ============================================================================
; low-level: fatal error reporting, decimal formatting, byte-string equality
; ============================================================================

; __bfa_write_stderr(ptr, len) -- best-effort; a failed diagnostic write has
; nothing further to report to, so its own CF is not checked.
proc __bfa_write_stderr, ptr, len
	locals
	endl
	mov	edi, 2
	mov	rsi, [ptr]
	mov	rdx, [len]
	call	sys_write
	return
endp

; __bfa_fmt_udec(value, bufend) -> rax = ptr to first digit, rdx = length.
; Fills backward from `bufend` (exclusive) -- the caller supplies a scratch
; buffer at least 20 bytes ending at `bufend` (2^64-1 is 20 decimal digits).
proc __bfa_fmt_udec, value, bufend
	uses	rbx, r12
	locals
	endl
	mov	rax, [value]
	mov	r12, [bufend]
	mov	rbx, 10
	test	rax, rax
	jnz	.loop
	dec	r12
	mov	byte [r12], '0'
	jmp	.done
  .loop:
	test	rax, rax
	jz	.done
	xor	edx, edx
	div	rbx
	add	dl, '0'
	dec	r12
	mov	[r12], dl
	jmp	.loop
  .done:
	mov	rax, r12
	mov	rdx, [bufend]
	sub	rdx, r12
	return
endp

; __bfa_die(p, msg_ptr, msg_len) -- never returns. Writes
; "bfa: parse error at line N: MSG\n" to stderr, exit(3).
proc __bfa_die, p, msg_ptr, msg_len
	uses	rbx, r12, r13
	locals
		slot decbuf, 24
	endl
	mov	rbx, [p]
	mov	r12, [msg_ptr]
	mov	r13, [msg_len]

	jmp	.after_pfx
  .pfx db	"bfa: parse error at line "
  .after_pfx:
	lea	rdi, [.pfx]
	mov	rsi, .after_pfx - .pfx
	call	__bfa_write_stderr

	mov	rdi, [rbx + BfaParser.line_no]
	lea	rsi, [decbuf + 24]
	call	__bfa_fmt_udec
	mov	rdi, rax
	mov	rsi, rdx
	call	__bfa_write_stderr

	jmp	.after_mid
  .mid db	": "
  .after_mid:
	lea	rdi, [.mid]
	mov	rsi, .after_mid - .mid
	call	__bfa_write_stderr

	mov	rdi, r12
	mov	rsi, r13
	call	__bfa_write_stderr

	jmp	.after_nl
  .nl db	10
  .after_nl:
	lea	rdi, [.nl]
	mov	rsi, 1
	call	__bfa_write_stderr

	mov	rdi, 3
	call	sys_exit_group
	; never returns
endp

; __bfa_streq(a_ptr, a_len, b_ptr, b_len) -> eax = 1 if identical bytes and
; length, else 0. A private near-duplicate of rt/str.inc's `str_eq`
; (unreachable here without re-including arena.inc down a second path --
; see rt/map.inc's own `__map_bytes_equal` for the same, already-established
; tradeoff).
proc __bfa_streq, a_ptr, a_len, b_ptr, b_len
	locals
	endl
	mov	rax, [a_len]
	cmp	rax, [b_len]
	jne	.no
	mov	rsi, [a_ptr]
	mov	rdi, [b_ptr]
	mov	rcx, rax
	test	rcx, rcx
	jz	.yes
	cld
	repe	cmpsb
	jne	.no
  .yes:
	mov	eax, 1
	return
  .no:
	mov	eax, 0
	return
endp

; __bfa_is_alldigit(ptr,len) -> eax=1 if len>0 and every byte is '0'..'9'.
proc __bfa_is_alldigit, ptr, len
	locals
	endl
	mov	rsi, [ptr]
	mov	rcx, [len]
	test	rcx, rcx
	jz	.no
  .loop:
	movzx	eax, byte [rsi]
	cmp	al, '0'
	jl	.no
	cmp	al, '9'
	jg	.no
	inc	rsi
	dec	rcx
	jnz	.loop
	mov	eax, 1
	return
  .no:
	mov	eax, 0
	return
endp

; __bfa_parse_udec(p, ptr, len) -> rax = value. Dies on an empty or
; non-digit token -- every caller already knows, from grammar position, that
; a plain non-negative integer belongs here.
proc __bfa_parse_udec, p, ptr, len
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [ptr]
	mov	r12, [len]
	mov	rdi, rbx
	mov	rsi, r12
	call	__bfa_is_alldigit
	test	eax, eax
	jnz	.ok
	mov	rdi, [p]
	jmp	.after_msg
  .msg db	"expected a decimal integer"
  .after_msg:
	lea	rsi, [.msg]
	mov	rdx, .after_msg - .msg
	call	__bfa_die
  .ok:
	xor	r13, r13		; accumulator
  .loop:
	test	r12, r12
	jz	.done
	movzx	eax, byte [rbx]
	sub	al, '0'
	movzx	eax, al
	imul	r13, r13, 10
	add	r13, rax
	inc	rbx
	dec	r12
	jmp	.loop
  .done:
	mov	rax, r13
	return
endp

; __bfa_parse_imm64(p, ptr, len) -> rax = value, negated if the token had a
; leading '-'. The one place a sign is legal (`iconst`'s immediate).
proc __bfa_parse_imm64, p, ptr, len
	locals
	endl
	mov	rsi, [ptr]
	mov	rcx, [len]
	test	rcx, rcx
	jnz	.nonempty
	mov	rdi, [p]
	jmp	.after_msg
  .msg db	"expected an integer immediate"
  .after_msg:
	lea	rsi, [.msg]
	mov	rdx, .after_msg - .msg
	call	__bfa_die
  .nonempty:
	cmp	byte [rsi], '-'
	jne	.pos
	inc	rsi
	dec	rcx
	mov	rdi, [p]
	call	__bfa_parse_udec
	neg	rax
	return
  .pos:
	mov	rdi, [p]
	mov	rsi, [ptr]
	mov	rdx, [len]
	call	__bfa_parse_udec
	return
endp

; ============================================================================
; type-token parsing (docs/design/ssa-ir.md section 2.11's `Type` production)
; ============================================================================

; __bfa_parse_type(p, ptr, len) -> eax = type id. `red.`/`fn.` are checked as
; 4- and 3-byte PREFIXES (not exact matches) since both carry a trailing
; payload; everything else is checked by exact `__bfa_streq`. Recurses once
; for `red.` -- an ordinary `call`, not a macro invocation, so the
; macro-dialect's own no-self-recursion limit (macros/proc.inc's header)
; does not apply here at all.
proc __bfa_parse_type, p, ptr, len
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	r12, [ptr]
	mov	r13, [len]
	test	r13, r13
	jz	.bad

	mov	al, byte [r12]
	cmp	al, 'u'
	je	.uiwidth
	cmp	al, 'i'
	je	.uiwidth

	jmp	.after_f32
  .lit_f32 db "f32"
  .after_f32:
	mov	rdi, r12
	mov	rsi, r13
	lea	rdx, [.lit_f32]
	mov	rcx, .after_f32 - .lit_f32
	call	__bfa_streq
	test	eax, eax
	jz	.chk_f64
	mov	rdi, [rbx + BfaParser.module]
	mov	esi, BFA_TK_F32
	xor	edx, edx
	mov	ecx, 32
	xor	r8, r8
	call	bfa_type_intern
	return

  .chk_f64:
	jmp	.after_f64
  .lit_f64 db "f64"
  .after_f64:
	mov	rdi, r12
	mov	rsi, r13
	lea	rdx, [.lit_f64]
	mov	rcx, .after_f64 - .lit_f64
	call	__bfa_streq
	test	eax, eax
	jz	.chk_dptr
	mov	rdi, [rbx + BfaParser.module]
	mov	esi, BFA_TK_F64
	xor	edx, edx
	mov	ecx, 64
	xor	r8, r8
	call	bfa_type_intern
	return

  .chk_dptr:
	jmp	.after_dptr
  .lit_dptr db "dptr"
  .after_dptr:
	mov	rdi, r12
	mov	rsi, r13
	lea	rdx, [.lit_dptr]
	mov	rcx, .after_dptr - .lit_dptr
	call	__bfa_streq
	test	eax, eax
	jz	.chk_ptr
	mov	rdi, [rbx + BfaParser.module]
	mov	esi, BFA_TK_DPTR
	xor	edx, edx
	mov	ecx, 64
	xor	r8, r8
	call	bfa_type_intern
	return

  .chk_ptr:
	jmp	.after_ptr
  .lit_ptr db "ptr"
  .after_ptr:
	mov	rdi, r12
	mov	rsi, r13
	lea	rdx, [.lit_ptr]
	mov	rcx, .after_ptr - .lit_ptr
	call	__bfa_streq
	test	eax, eax
	jz	.chk_refc
	mov	rdi, [rbx + BfaParser.module]
	call	bfa_ty_ptr
	return

  .chk_refc:
	jmp	.after_refc
  .lit_refc db "refc"
  .after_refc:
	mov	rdi, r12
	mov	rsi, r13
	lea	rdx, [.lit_refc]
	mov	rcx, .after_refc - .lit_refc
	call	__bfa_streq
	test	eax, eax
	jz	.chk_ref
	mov	rdi, [rbx + BfaParser.module]
	mov	esi, BFA_TK_REFC
	xor	edx, edx
	mov	ecx, 64
	xor	r8, r8
	call	bfa_type_intern
	return

  .chk_ref:
	jmp	.after_ref
  .lit_ref db "ref"
  .after_ref:
	mov	rdi, r12
	mov	rsi, r13
	lea	rdx, [.lit_ref]
	mov	rcx, .after_ref - .lit_ref
	call	__bfa_streq
	test	eax, eax
	jz	.chk_red
	mov	rdi, [rbx + BfaParser.module]
	mov	esi, BFA_TK_REF
	xor	edx, edx
	mov	ecx, 64
	xor	r8, r8
	call	bfa_type_intern
	return

  .chk_red:
	cmp	r13, 4
	jle	.chk_fn
	jmp	.after_redp
  .lit_redp db "red."
  .after_redp:
	mov	rdi, r12
	mov	rsi, 4
	lea	rdx, [.lit_redp]
	mov	rcx, 4
	call	__bfa_streq
	test	eax, eax
	jz	.chk_fn
	mov	rdi, rbx
	lea	rsi, [r12 + 4]
	mov	rdx, r13
	sub	rdx, 4
	call	__bfa_parse_type		; recursive -- inner type id in eax
	mov	r8d, eax
	mov	rdi, [rbx + BfaParser.module]
	mov	esi, BFA_TK_RED
	xor	edx, edx
	xor	ecx, ecx
	call	bfa_type_intern
	return

  .chk_fn:
	cmp	r13, 3
	jle	.bad
	jmp	.after_fnp
  .lit_fnp db "fn."
  .after_fnp:
	mov	rdi, r12
	mov	rsi, 3
	lea	rdx, [.lit_fnp]
	mov	rcx, 3
	call	__bfa_streq
	test	eax, eax
	jz	.bad
	lea	rax, [r12 + 3]
	mov	rcx, r13
	sub	rcx, 3
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, rcx
	call	__bfa_parse_udec		; -> rax = signature index
	mov	r8d, eax
	mov	rdi, [rbx + BfaParser.module]
	mov	esi, BFA_TK_FN
	xor	edx, edx
	xor	ecx, ecx
	call	bfa_type_intern
	return

  .uiwidth:
	lea	rax, [r12 + 1]
	mov	rcx, r13
	dec	rcx
	mov	rdi, rbx
	mov	rsi, rax
	mov	rdx, rcx
	call	__bfa_parse_udec		; -> rax = width (digits after 'u'/'i')
	cmp	rax, 1
	jl	.bad
	cmp	rax, 64
	jg	.bad
	mov	r9, rax				; width, saved
	mov	al, byte [r12]
	cmp	al, 'i'
	je	.is_signed
	mov	rdi, [rbx + BfaParser.module]
	mov	esi, BFA_TK_U
	xor	edx, edx
	mov	ecx, r9d
	xor	r8, r8
	call	bfa_type_intern
	return
  .is_signed:
	mov	rdi, [rbx + BfaParser.module]
	mov	esi, BFA_TK_I
	mov	edx, 1
	mov	ecx, r9d
	xor	r8, r8
	call	bfa_type_intern
	return

  .bad:
	mov	rdi, rbx
	jmp	.after_badmsg
  .badmsg db "unrecognised type spelling"
  .after_badmsg:
	lea	rsi, [.badmsg]
	mov	rdx, .after_badmsg - .badmsg
	call	__bfa_die
endp

; ============================================================================
; line/token scanning
; ============================================================================

; __bfa_p_read_line(p) -> eax = 1 and p.line_ptr/line_len/line_no/pending_off/
; pending_len set, or eax = 0 at end of input. Strips a `;`-to-EOL comment
; (docs/design/ssa-ir.md section 2.11) before setting line_len; LF-only line
; splitting (CLAUDE.md section 8.1's own rule, applied to this project's own
; IR text same as to the language it compiles).
proc __bfa_p_read_line, p
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	rax, [rbx + BfaParser.pos]
	cmp	rax, [rbx + BfaParser.src_len]
	jl	.have
	mov	eax, 0
	return
  .have:
	mov	r12, rax			; line start
	mov	r13, [rbx + BfaParser.src_ptr]
	mov	rcx, rax
  .findnl:
	cmp	rcx, [rbx + BfaParser.src_len]
	jge	.noeol
	movzx	edx, byte [r13 + rcx]
	cmp	dl, 10
	je	.foundnl
	inc	rcx
	jmp	.findnl
  .foundnl:
	mov	rax, rcx			; line end, exclusive of the LF
	lea	rdx, [rcx + 1]			; next pos
	jmp	.gotbounds
  .noeol:
	mov	rax, [rbx + BfaParser.src_len]
	mov	rdx, rax
  .gotbounds:
	mov	[rbx + BfaParser.pos], rdx
	mov	rcx, r12
  .findsemi:
	cmp	rcx, rax
	jge	.nosemi
	movzx	edx, byte [r13 + rcx]
	cmp	dl, ';'
	je	.foundsemi
	inc	rcx
	jmp	.findsemi
  .foundsemi:
	mov	rax, rcx
  .nosemi:
	lea	rcx, [r13 + r12]
	mov	[rbx + BfaParser.line_ptr], rcx
	mov	rcx, rax
	sub	rcx, r12
	mov	[rbx + BfaParser.line_len], rcx
	mov	[rbx + BfaParser.pending_off], r12
	mov	[rbx + BfaParser.pending_len], rcx
	mov	rcx, [rbx + BfaParser.line_no]
	inc	rcx
	mov	[rbx + BfaParser.line_no], rcx
	mov	eax, 1
	return
endp

; __bfa_p_tokenize(p) -- splits the current (already comment-stripped) line
; on spaces/tabs/CR into p.toks, sets p.ntoks, resets p.tokpos to 0.
proc __bfa_p_tokenize, p
	uses	rbx, r12, r13, r14
	locals
		slot tokstart, dq
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + BfaParser.toks]
	mov	rsi, [rbx + BfaParser.scratch]
	mov	rdx, sizeof.BfaTok
	mov	rcx, 8
	call	vec_init

	mov	r14, [rbx + BfaParser.line_ptr]
	mov	r13, [rbx + BfaParser.line_len]
	xor	r12, r12
  .skipws:
	cmp	r12, r13
	jge	.alldone
	movzx	eax, byte [r14 + r12]
	cmp	al, ' '
	je	.ws1
	cmp	al, 9
	je	.ws1
	cmp	al, 13
	je	.ws1
	jmp	.havestart
  .ws1:
	inc	r12
	jmp	.skipws
  .havestart:
	mov	[tokstart], r12
  .scan:
	cmp	r12, r13
	jge	.emit
	movzx	eax, byte [r14 + r12]
	cmp	al, ' '
	je	.emit
	cmp	al, 9
	je	.emit
	cmp	al, 13
	je	.emit
	inc	r12
	jmp	.scan
  .emit:
	mov	rdi, [rbx + BfaParser.toks]
	call	vec_push			; rax = new BfaTok*
	mov	rcx, [tokstart]
	lea	rdx, [r14 + rcx]
	mov	[rax + BfaTok.tp], rdx
	mov	rdx, r12
	sub	rdx, rcx
	mov	[rax + BfaTok.tl], rdx
	jmp	.skipws
  .alldone:
	mov	rdi, [rbx + BfaParser.toks]
	mov	rax, [rdi + Vec.len]
	mov	[rbx + BfaParser.ntoks], rax
	mov	qword [rbx + BfaParser.tokpos], 0
	return
endp

; __bfa_p_next_nonblank(p) -> eax = 1 with a freshly tokenized nonblank line
; ready at p.toks/p.ntoks (p.tokpos = 0), or eax = 0 at end of input. Skips
; blank lines and comment-only lines (which tokenize to zero tokens) so
; every caller above this one never has to special-case them.
proc __bfa_p_next_nonblank, p
	locals
	endl
  .loop:
	mov	rdi, [p]
	call	__bfa_p_read_line
	test	eax, eax
	jz	.eof
	mov	rdi, [p]
	call	__bfa_p_tokenize
	mov	rax, [p]
	mov	rax, [rax + BfaParser.ntoks]
	test	rax, rax
	jz	.loop
	mov	eax, 1
	return
  .eof:
	mov	eax, 0
	return
endp

; __bfa_tok_at(p, idx) -> rax = ptr, rdx = len. `rassert`s (via vec_get)
; on an out-of-range index -- every caller here already checked count.
proc __bfa_tok_at, p, idx
	locals
	endl
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.toks]
	mov	rsi, [idx]
	call	vec_get
	mov	rdx, [rax + BfaTok.tl]
	mov	rax, [rax + BfaTok.tp]
	return
endp

; __bfa_tok_remaining(p) -> rax = tokens left (ntoks - tokpos).
proc __bfa_tok_remaining, p
	locals
	endl
	mov	rax, [p]
	mov	rcx, [rax + BfaParser.ntoks]
	sub	rcx, [rax + BfaParser.tokpos]
	mov	rax, rcx
	return
endp

; __bfa_tok_peek(p) -> rax = ptr, rdx = len, or 0,0 if no tokens remain.
proc __bfa_tok_peek, p
	locals
	endl
	mov	rdi, [p]
	call	__bfa_tok_remaining
	test	rax, rax
	jnz	.have
	xor	eax, eax
	xor	edx, edx
	return
  .have:
	mov	rdi, [p]
	mov	rsi, [p]
	mov	rsi, [rsi + BfaParser.tokpos]
	mov	rdi, [rdi + BfaParser.toks]
	call	vec_get
	mov	rdx, [rax + BfaTok.tl]
	mov	rax, [rax + BfaTok.tp]
	return
endp

; __bfa_tok_next(p) -> rax = ptr, rdx = len; consumes it. Dies if none
; remain -- every call site already knows, from grammar position, that a
; token is required here.
proc __bfa_tok_next, p
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, rbx
	call	__bfa_tok_remaining
	test	rax, rax
	jnz	.have
	mov	rdi, rbx
	jmp	.after_msg
  .msg db	"unexpected end of line"
  .after_msg:
	lea	rsi, [.msg]
	mov	rdx, .after_msg - .msg
	call	__bfa_die
  .have:
	mov	rdi, [rbx + BfaParser.toks]
	mov	rsi, [rbx + BfaParser.tokpos]
	call	vec_get
	mov	rcx, [rbx + BfaParser.tokpos]
	inc	rcx
	mov	[rbx + BfaParser.tokpos], rcx
	mov	rdx, [rax + BfaTok.tl]
	mov	rax, [rax + BfaTok.tp]
	return
endp

; __bfa_tok_expect(p, text, textlen) -- consumes the next token and dies
; unless it matches `text` exactly.
proc __bfa_tok_expect, p, text, textlen
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [p]
	mov	r12, [text]
	mov	r13, [textlen]
	mov	rdi, rbx
	call	__bfa_tok_next
	mov	rdi, rax
	mov	rsi, rdx
	mov	rdx, r12
	mov	rcx, r13
	call	__bfa_streq
	test	eax, eax
	jnz	.ok
	mov	rdi, rbx
	jmp	.after_msg
  .msg db	"unexpected token"
  .after_msg:
	lea	rsi, [.msg]
	mov	rdx, .after_msg - .msg
	call	__bfa_die
  .ok:
	return
endp

; ============================================================================
; opcode table: text spelling -> packed (op | shape<<8 | pred<<16)
; ============================================================================
; SH_BIN also carries the three overflow predicates (addov/subov/mulov) --
; identical token shape (`T %a %b`); only the produced value's TYPE differs
; (u1 instead of T), which is an EMIT-time fact (bfa_value_type, ir.inc), not
; a parse-time one. SH_UN also carries every convert/misc-float op
; (zext/sext/trunc/fext/ftrunc/itof/ftoi/bitcast) -- all `T %a`. SH_RETAIN
; also carries `release` -- both `%r`. Fewer shapes to dispatch on than
; ir.inc's BFA_SH_* enum has entries; the unused ones (BINU1, CONV, RELEASE)
; stay defined there for documentation fidelity to the opcode table's own
; groupings and cost nothing left unused.

macro bfa_opdef? nm, opc, shp
	local s, e
	jmp	e
	s db	nm
	e:
	mov	rdi, [rbx + BfaParser.optab]
	lea	rsi, [s]
	mov	rdx, e - s
	mov	rcx, (opc) or ((shp) shl 8)
	call	map_insert
end macro

macro bfa_opdefp? nm, opc, shp, pred
	local s, e
	jmp	e
	s db	nm
	e:
	mov	rdi, [rbx + BfaParser.optab]
	lea	rsi, [s]
	mov	rdx, e - s
	mov	rcx, (opc) or ((shp) shl 8) or ((pred) shl 16)
	call	map_insert
end macro

; __bfa_optab_init(p) -- allocates p.optab from the module's IR arena (not
; scratch: this table's content is pure static data, independent of any one
; function, and must survive the per-function scratch reset).
proc __bfa_optab_init, p
	uses	rbx
	locals
	endl
	mov	rbx, [p]
	mov	rdi, [rbx + BfaParser.ir_arena]
	mov	rsi, sizeof.Map
	call	arena_alloc
	mov	[rbx + BfaParser.optab], rax
	mov	rdi, rax
	mov	rsi, [rbx + BfaParser.ir_arena]
	mov	rdx, 128
	call	map_init

	bfa_opdef 'add',  BFA_OP_ADD,  BFA_SH_BIN
	bfa_opdef 'sub',  BFA_OP_SUB,  BFA_SH_BIN
	bfa_opdef 'mul',  BFA_OP_MUL,  BFA_SH_BIN
	bfa_opdef 'div',  BFA_OP_DIV,  BFA_SH_BIN
	bfa_opdef 'rem',  BFA_OP_REM,  BFA_SH_BIN
	bfa_opdef 'addw', BFA_OP_ADDW, BFA_SH_BIN
	bfa_opdef 'subw', BFA_OP_SUBW, BFA_SH_BIN
	bfa_opdef 'mulw', BFA_OP_MULW, BFA_SH_BIN
	bfa_opdef 'adds', BFA_OP_ADDS, BFA_SH_BIN
	bfa_opdef 'subs', BFA_OP_SUBS, BFA_SH_BIN
	bfa_opdef 'muls', BFA_OP_MULS, BFA_SH_BIN
	bfa_opdef 'addov',BFA_OP_ADDOV,BFA_SH_BIN
	bfa_opdef 'subov',BFA_OP_SUBOV,BFA_SH_BIN
	bfa_opdef 'mulov',BFA_OP_MULOV,BFA_SH_BIN
	bfa_opdef 'and',  BFA_OP_AND,  BFA_SH_BIN
	bfa_opdef 'or',   BFA_OP_OR,   BFA_SH_BIN
	bfa_opdef 'xor',  BFA_OP_XOR,  BFA_SH_BIN
	bfa_opdef 'shl',  BFA_OP_SHL,  BFA_SH_BIN
	bfa_opdef 'shr',  BFA_OP_SHR,  BFA_SH_BIN
	bfa_opdef 'fadd', BFA_OP_FADD, BFA_SH_BIN
	bfa_opdef 'fsub', BFA_OP_FSUB, BFA_SH_BIN
	bfa_opdef 'fmul', BFA_OP_FMUL, BFA_SH_BIN
	bfa_opdef 'fdiv', BFA_OP_FDIV, BFA_SH_BIN

	bfa_opdefp 'cmp.eq', BFA_OP_CMP, BFA_SH_CMP, BFA_PRED_EQ
	bfa_opdefp 'cmp.ne', BFA_OP_CMP, BFA_SH_CMP, BFA_PRED_NE
	bfa_opdefp 'cmp.lt', BFA_OP_CMP, BFA_SH_CMP, BFA_PRED_LT
	bfa_opdefp 'cmp.le', BFA_OP_CMP, BFA_SH_CMP, BFA_PRED_LE
	bfa_opdefp 'cmp.gt', BFA_OP_CMP, BFA_SH_CMP, BFA_PRED_GT
	bfa_opdefp 'cmp.ge', BFA_OP_CMP, BFA_SH_CMP, BFA_PRED_GE
	bfa_opdefp 'fcmp.eq', BFA_OP_FCMP, BFA_SH_FCMP, BFA_PRED_EQ
	bfa_opdefp 'fcmp.ne', BFA_OP_FCMP, BFA_SH_FCMP, BFA_PRED_NE
	bfa_opdefp 'fcmp.lt', BFA_OP_FCMP, BFA_SH_FCMP, BFA_PRED_LT
	bfa_opdefp 'fcmp.le', BFA_OP_FCMP, BFA_SH_FCMP, BFA_PRED_LE
	bfa_opdefp 'fcmp.gt', BFA_OP_FCMP, BFA_SH_FCMP, BFA_PRED_GT
	bfa_opdefp 'fcmp.ge', BFA_OP_FCMP, BFA_SH_FCMP, BFA_PRED_GE

	bfa_opdef 'zext',    BFA_OP_ZEXT,    BFA_SH_UN
	bfa_opdef 'sext',    BFA_OP_SEXT,    BFA_SH_UN
	bfa_opdef 'trunc',   BFA_OP_TRUNC,   BFA_SH_UN
	bfa_opdef 'fneg',    BFA_OP_FNEG,    BFA_SH_UN
	bfa_opdef 'fext',    BFA_OP_FEXT,    BFA_SH_UN
	bfa_opdef 'ftrunc',  BFA_OP_FTRUNC,  BFA_SH_UN
	bfa_opdef 'itof',    BFA_OP_ITOF,    BFA_SH_UN
	bfa_opdef 'ftoi',    BFA_OP_FTOI,    BFA_SH_UN
	bfa_opdef 'bitcast', BFA_OP_BITCAST, BFA_SH_UN

	bfa_opdef 'fma',     BFA_OP_FMA,     BFA_SH_FMA
	bfa_opdef 'redinit', BFA_OP_REDINIT, BFA_SH_REDINIT
	bfa_opdef 'contrib', BFA_OP_CONTRIB, BFA_SH_CONTRIB
	bfa_opdef 'redfin',  BFA_OP_REDFIN,  BFA_SH_REDFIN

	bfa_opdef 'slot',      BFA_OP_SLOT,      BFA_SH_SLOT
	bfa_opdef 'load',      BFA_OP_LOAD,      BFA_SH_LOAD
	bfa_opdef 'store',     BFA_OP_STORE,     BFA_SH_STORE
	bfa_opdef 'loadbits',  BFA_OP_LOADBITS,  BFA_SH_LOADBITS
	bfa_opdef 'storebits', BFA_OP_STOREBITS, BFA_SH_STOREBITS
	bfa_opdef 'copy',      BFA_OP_COPY,      BFA_SH_COPY
	bfa_opdef 'addr',      BFA_OP_ADDR,      BFA_SH_ADDR
	bfa_opdef 'index',     BFA_OP_INDEX,     BFA_SH_INDEX
	bfa_opdef 'gaddr',     BFA_OP_GADDR,     BFA_SH_GADDR
	bfa_opdef 'chk',       BFA_OP_CHK,       BFA_SH_CHK

	bfa_opdef 'retain',  BFA_OP_RETAIN,  BFA_SH_RETAIN
	bfa_opdef 'release', BFA_OP_RELEASE, BFA_SH_RETAIN

	bfa_opdef 'call',    BFA_OP_CALL,    BFA_SH_CALL
	bfa_opdef 'callind', BFA_OP_CALLIND, BFA_SH_CALLIND
	bfa_opdef 'ret',     BFA_OP_RET,     BFA_SH_RET
	bfa_opdef 'jmp',     BFA_OP_JMP,     BFA_SH_JMP
	bfa_opdef 'br',      BFA_OP_BR,      BFA_SH_BR
	bfa_opdef 'trap',    BFA_OP_TRAP,    BFA_SH_TRAP
	bfa_opdef 'phi',     BFA_OP_PHI,     BFA_SH_PHI

	bfa_opdef 'param',  BFA_OP_PARAM,  BFA_SH_PARAM
	bfa_opdef 'iconst', BFA_OP_ICONST, BFA_SH_ICONST
	bfa_opdef 'fconst', BFA_OP_FCONST, BFA_SH_FCONST

	return
endp

; __bfa_optab_lookup(p, ptr, len) -> eax = packed dispatch word. Dies on an
; unrecognised opcode spelling.
proc __bfa_optab_lookup, p, ptr, len
	locals
	endl
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.optab]
	mov	rsi, [ptr]
	mov	rdx, [len]
	call	map_get
	test	rax, rax
	jnz	.found
	mov	rdi, [p]
	jmp	.after_msg
  .msg db	"unrecognised opcode"
  .after_msg:
	lea	rsi, [.msg]
	mov	rdx, .after_msg - .msg
	call	__bfa_die
  .found:
	return
endp

; ============================================================================
; value / block reference resolution
; ============================================================================

; __bfa_val_now(p, ptr, len) -> eax = internal value id. Token must be
; `%N` with N already defined (N < val_map.len) -- every operand except a
; phi's value operand is resolved this way (docs/design/ssa-ir.md section
; 2.11: phi is the ONLY forward-reference case).
proc __bfa_val_now, p, ptr, len
	locals
		slot textn, dq
	endl
	mov	rax, [len]
	test	rax, rax
	jz	.bad
	mov	rax, [ptr]
	cmp	byte [rax], '%'
	jne	.bad
	mov	rax, [ptr]
	inc	rax
	mov	rcx, [len]
	dec	rcx
	mov	rdi, [p]
	mov	rsi, rax
	mov	rdx, rcx
	call	__bfa_parse_udec
	mov	[textn], rax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.val_map]
	mov	rsi, [rdi + Vec.len]
	cmp	[textn], rsi
	jl	.ok
	mov	rdi, [p]
	jmp	.after_msg
  .msg db	"value not yet defined"
  .after_msg:
	lea	rsi, [.msg]
	mov	rdx, .after_msg - .msg
	call	__bfa_die
  .ok:
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.val_map]
	mov	rsi, [textn]
	call	vec_get
	mov	eax, [rax]
	return
  .bad:
	mov	rdi, [p]
	jmp	.after_msg2
  .msg2 db	"expected a value (%N)"
  .after_msg2:
	lea	rsi, [.msg2]
	mov	rdx, .after_msg2 - .msg2
	call	__bfa_die
endp

; __bfa_val_maybe(p, ptr, len) -> rax = internal id (if edx=1) or the raw
; text N (if edx=0, meaning "not yet defined" -- a phi forward reference).
proc __bfa_val_maybe, p, ptr, len
	locals
		slot textn, dq
	endl
	mov	rax, [ptr]
	cmp	byte [rax], '%'
	je	.pct
	mov	rdi, [p]
	jmp	.after_bad
  .badmsg db	"expected a value (%N)"
  .after_bad:
	lea	rsi, [.badmsg]
	mov	rdx, .after_bad - .badmsg
	call	__bfa_die
  .pct:
	mov	rax, [ptr]
	inc	rax
	mov	rcx, [len]
	dec	rcx
	mov	rdi, [p]
	mov	rsi, rax
	mov	rdx, rcx
	call	__bfa_parse_udec
	mov	[textn], rax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.val_map]
	mov	rsi, [rdi + Vec.len]
	cmp	[textn], rsi
	jl	.resolved
	mov	rax, [textn]
	xor	edx, edx
	return
  .resolved:
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.val_map]
	mov	rsi, [textn]
	call	vec_get
	mov	eax, [rax]
	mov	edx, 1
	return
endp

; __bfa_tok_is_blockhdr(ptr, len) -> eax = 1 iff the token is exactly
; `b<digits>:` (a block HEADER; a block REFERENCE operand has no colon).
proc __bfa_tok_is_blockhdr, ptr, len
	locals
	endl
	mov	rax, [len]
	cmp	rax, 2
	jl	.no
	mov	rax, [ptr]
	cmp	byte [rax], 'b'
	jne	.no
	mov	rax, [ptr]
	mov	rcx, [len]
	movzx	edx, byte [rax + rcx - 1]
	cmp	dl, ':'
	jne	.no
	lea	rdi, [rax + 1]
	mov	rsi, rcx
	sub	rsi, 2
	test	rsi, rsi
	jz	.no
	call	__bfa_is_alldigit
	return
  .no:
	mov	eax, 0
	return
endp

; __bfa_blockhdr_n(p, ptr, len) -> rax = N, given `is_blockhdr` already true.
proc __bfa_blockhdr_n, p, ptr, len
	locals
	endl
	mov	rax, [ptr]
	inc	rax
	mov	rcx, [len]
	sub	rcx, 2
	mov	rdi, [p]
	mov	rsi, rax
	mov	rdx, rcx
	call	__bfa_parse_udec
	return
endp

; __bfa_blk_ref(p, ptr, len) -> eax = internal block id. Token must be `bN`
; (no colon -- a REFERENCE, not a header) with N < the current function's
; block count -- always true by construction once prescan has run (every
; header discovered by prescan; forward branches are exactly why prescan
; exists).
proc __bfa_blk_ref, p, ptr, len
	locals
		slot textn, dq
	endl
	mov	rax, [len]
	cmp	rax, 2
	jl	.bad
	mov	rax, [ptr]
	cmp	byte [rax], 'b'
	jne	.bad
	mov	rax, [ptr]
	inc	rax
	mov	rcx, [len]
	dec	rcx
	mov	rdi, rax
	mov	rsi, rcx
	call	__bfa_is_alldigit
	test	eax, eax
	jz	.bad
	mov	rax, [ptr]
	inc	rax
	mov	rcx, [len]
	dec	rcx
	mov	rdi, [p]
	mov	rsi, rax
	mov	rdx, rcx
	call	__bfa_parse_udec
	mov	[textn], rax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	rdi, [rdi + BfaFunc.blocks]
	mov	rsi, [rdi + Vec.len]
	cmp	[textn], rsi
	jl	.ok
	mov	rdi, [p]
	jmp	.after_msg
  .msg db	"block not defined in this function"
  .after_msg:
	lea	rsi, [.msg]
	mov	rdx, .after_msg - .msg
	call	__bfa_die
  .ok:
	mov	rax, [textn]
	inc	rax
	return
  .bad:
	mov	rdi, [p]
	jmp	.after_msg2
  .msg2 db	"expected a block reference (bN)"
  .after_msg2:
	lea	rsi, [.msg2]
	mov	rdx, .after_msg2 - .msg2
	call	__bfa_die
endp

; __bfa_valmap_push(p, internal_id) -- records the next text %N -> internal
; id mapping and advances p.next_valn. The one place either changes.
proc __bfa_valmap_push, p, internal_id
	locals
	endl
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.val_map]
	call	vec_push
	mov	ecx, [internal_id]
	mov	[rax], ecx
	mov	rdi, [p]
	mov	rcx, [rdi + BfaParser.next_valn]
	inc	rcx
	mov	[rdi + BfaParser.next_valn], rcx
	return
endp

; ============================================================================
; small per-token convenience wrappers (consume the next token, interpret it)
; ============================================================================

proc __bfa_next_type, p
	locals
	endl
	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rsi, rax
	mov	rdi, [p]
	call	__bfa_parse_type
	return
endp

proc __bfa_next_val, p
	locals
	endl
	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rsi, rax
	mov	rdi, [p]
	call	__bfa_val_now
	return
endp

; __bfa_val_or_imm(p, ptr, len) -> eax = value id or immediate, edx = 1 iff
; the token was a bare integer immediate rather than `%N`.
;
; FOUND BY RUNNING THE @dot ROUND-TRIP, NOT BY READING THE GRAMMAR TWICE
; ENOUGH: docs/design/ssa-ir.md section 2.11's own worked example has
; `%11 = addw u64 %5 1` -- a bare `1`, not `%something`, as an arithmetic
; operand. Section 2.11's prose ("Line ::= ... OP Tok*", arity by table)
; never says an operand token must be `%N`; every OTHER example in the
; table just happens to use one. This file's first draft assumed every
; value-shaped operand was a `%N` and the parser segfaulted (wrong
; assumption, not a wiring bug) walking off the type token as if it were a
; value; run against the ACTUAL worked example, not a hand-picked simpler
; one, is what caught it. `BFA_SH_BIN`/`BFA_SH_CMP`/`BFA_SH_FCMP` are the
; only shapes this file has evidence for (the worked example is the only
; evidence) -- every other value-shaped operand elsewhere in this file
; still requires `%N`, and stays that way until a fixture demonstrates
; otherwise. An inline immediate is stored in the SAME u32 `a`/`b` slot a
; value id would occupy, so it is limited to what fits there -- a fixture
; needing a wider inline constant should route it through an explicit
; `iconst` first, exactly as `%3`/`iconst u64 0` does two lines earlier in
; the very same example.
proc __bfa_val_or_imm, p, ptr, len
	locals
	endl
	mov	rax, [ptr]
	cmp	byte [rax], '%'
	je	.isval
	mov	rdi, [p]
	mov	rsi, [ptr]
	mov	rdx, [len]
	call	__bfa_parse_imm64
	mov	edx, 1
	return
  .isval:
	mov	rdi, [p]
	mov	rsi, [ptr]
	mov	rdx, [len]
	call	__bfa_val_now
	xor	edx, edx
	return
endp

proc __bfa_next_val_or_imm, p
	locals
	endl
	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rdi, [p]
	mov	rsi, rax
	mov	rdx, rdx
	call	__bfa_val_or_imm
	return
endp

proc __bfa_next_blk, p
	locals
	endl
	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rsi, rax
	mov	rdi, [p]
	call	__bfa_blk_ref
	return
endp

proc __bfa_next_udec, p
	locals
	endl
	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rsi, rax
	mov	rdi, [p]
	call	__bfa_parse_udec
	return
endp

proc __bfa_next_imm, p
	locals
	endl
	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rsi, rax
	mov	rdi, [p]
	call	__bfa_parse_imm64
	return
endp

; __bfa_parse_order(p) -> eax = BFA_ORDER_*. Closed 3-word vocabulary
; (docs/design/ssa-ir.md section 2.7) -- a fixed enum, never interned,
; because the grammar admits no fourth spelling.
proc __bfa_parse_order, p
	locals
		slot tp2, dq
		slot tl2, dq
	endl
	mov	rdi, [p]
	call	__bfa_tok_next
	mov	[tp2], rax
	mov	[tl2], rdx
	mov	rdi, [tp2]
	mov	rsi, [tl2]
	jmp	.after_nativus
  .lit_nativus db "nativus"
  .after_nativus:
	lea	rdx, [.lit_nativus]
	mov	rcx, .after_nativus - .lit_nativus
	call	__bfa_streq
	test	eax, eax
	jz	.chk_maior
	mov	eax, BFA_ORDER_NATIVUS
	return
  .chk_maior:
	mov	rdi, [tp2]
	mov	rsi, [tl2]
	jmp	.after_maior
  .lit_maior db "maior"
  .after_maior:
	lea	rdx, [.lit_maior]
	mov	rcx, .after_maior - .lit_maior
	call	__bfa_streq
	test	eax, eax
	jz	.chk_minor
	mov	eax, BFA_ORDER_MAIOR
	return
  .chk_minor:
	mov	rdi, [tp2]
	mov	rsi, [tl2]
	jmp	.after_minor
  .lit_minor db "minor"
  .after_minor:
	lea	rdx, [.lit_minor]
	mov	rcx, .after_minor - .lit_minor
	call	__bfa_streq
	test	eax, eax
	jnz	.ok
	mov	rdi, [p]
	jmp	.after_msg
  .msg db	"byte order must be nativus, maior or minor"
  .after_msg:
	lea	rsi, [.msg]
	mov	rdx, .after_msg - .msg
	call	__bfa_die
  .ok:
	mov	eax, BFA_ORDER_MINOR
	return
endp

; __bfa_hexnib(p, c) -> eax = 0..15. Dies on a non-hex-digit byte.
proc __bfa_hexnib, p, c
	locals
	endl
	mov	eax, [c]
	cmp	al, '0'
	jl	.bad
	cmp	al, '9'
	jg	.notdigit
	sub	al, '0'
	movzx	eax, al
	return
  .notdigit:
	cmp	al, 'a'
	jl	.notlower
	cmp	al, 'f'
	jg	.notlower
	sub	al, 'a'
	add	al, 10
	movzx	eax, al
	return
  .notlower:
	cmp	al, 'A'
	jl	.bad
	cmp	al, 'F'
	jg	.bad
	sub	al, 'A'
	add	al, 10
	movzx	eax, al
	return
  .bad:
	mov	rdi, [p]
	jmp	.after_msg
  .msg db	"invalid hex digit"
  .after_msg:
	lea	rsi, [.msg]
	mov	rdx, .after_msg - .msg
	call	__bfa_die
endp

; ============================================================================
; instruction construction: the common tail every per-shape parser calls
; ============================================================================

; __bfa_ps_finish(p, op, aux, ty, a, b) -- pushes the Inst, stamps its span
; from p.pending_off/len, links it into the current block's phi or body
; list (by OP, not by a flag -- `phi` is the only opcode that ever goes on
; the phi list), and, iff this line had a `%N =` prefix, records the new
; text-N -> internal-id mapping.
proc __bfa_ps_finish, p, op, aux, ty, a, b
	locals
		slot newid, dd
	endl
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [op]
	mov	edx, [aux]
	mov	ecx, [ty]
	mov	r8d, [a]
	mov	r9d, [b]
	call	bfa_inst_push
	mov	[newid], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [newid]
	mov	rdx, [p]
	mov	rdx, [rdx + BfaParser.pending_off]
	mov	r8, [p]
	mov	r8, [r8 + BfaParser.pending_len]
	xor	ecx, ecx
	call	bfa_inst_set_span

	mov	eax, [op]
	cmp	eax, BFA_OP_PHI
	je	.tophi
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	rsi, [p]
	mov	rsi, [rsi + BfaParser.curblock]
	mov	edx, [newid]
	call	bfa_block_link_body
	jmp	.linked
  .tophi:
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	rsi, [p]
	mov	rsi, [rsi + BfaParser.curblock]
	mov	edx, [newid]
	call	bfa_block_link_phi
  .linked:
	mov	rax, [p]
	cmp	qword [rax + BfaParser.pending_result], 0
	je	.done
	mov	rdi, [p]
	mov	esi, [newid]
	call	__bfa_valmap_push
  .done:
	return
endp

; ============================================================================
; per-shape operand parsers -- one per token shape (docs/design/ssa-ir.md
; section 2.11's Line grammar, arity by BFA_SH_* / ir.inc). Each consumes
; exactly its own operand tokens and calls __bfa_ps_finish once.
; ============================================================================

; "T %a %b" -- add/sub/mul/div/rem/addw/.../fdiv, and the three overflow
; predicates (whose result is u1, not T -- an emit-time fact, ir.inc's
; bfa_value_type, not a parse-time one).
proc __bfa_ps_bin, p, op
	locals
		slot ty, dd
		slot va, dd
		slot vb, dd
		slot auxv, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_type
	mov	[ty], eax
	mov	dword [auxv], 0
	mov	rdi, [p]
	call	__bfa_next_val_or_imm
	mov	[va], eax
	test	edx, edx
	jz	.anotimm
	or	dword [auxv], 0x100
  .anotimm:
	mov	rdi, [p]
	call	__bfa_next_val_or_imm
	mov	[vb], eax
	test	edx, edx
	jz	.bnotimm
	or	dword [auxv], 0x200
  .bnotimm:
	mov	rdi, [p]
	mov	esi, [op]
	mov	edx, [auxv]
	mov	ecx, [ty]
	mov	r8d, [va]
	mov	r9d, [vb]
	call	__bfa_ps_finish
	return
endp

; "T %a %b" with a predicate -- cmp.*/fcmp.* both, aux = pred.
proc __bfa_ps_cmp, p, op, pred
	locals
		slot ty, dd
		slot va, dd
		slot vb, dd
		slot auxv, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_type
	mov	[ty], eax
	mov	eax, [pred]
	mov	[auxv], eax
	mov	rdi, [p]
	call	__bfa_next_val_or_imm
	mov	[va], eax
	test	edx, edx
	jz	.anotimm
	or	dword [auxv], 0x100
  .anotimm:
	mov	rdi, [p]
	call	__bfa_next_val_or_imm
	mov	[vb], eax
	test	edx, edx
	jz	.bnotimm
	or	dword [auxv], 0x200
  .bnotimm:
	mov	rdi, [p]
	mov	esi, [op]
	mov	edx, [auxv]
	mov	ecx, [ty]
	mov	r8d, [va]
	mov	r9d, [vb]
	call	__bfa_ps_finish
	return
endp

; "T %a" -- fneg, zext/sext/trunc, fext/ftrunc/itof/ftoi/bitcast.
proc __bfa_ps_un, p, op
	locals
		slot ty, dd
		slot va, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_type
	mov	[ty], eax
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[va], eax
	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [ty]
	mov	r8d, [va]
	xor	r9d, r9d
	call	__bfa_ps_finish
	return
endp

; "F %a %b %c" -- fma. 3 operands -> extra pool.
proc __bfa_ps_fma, p, op
	locals
		slot ty, dd
		slot va, dd
		slot vb, dd
		slot vc, dd
		slot eidx, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_type
	mov	[ty], eax
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[va], eax
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vb], eax
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vc], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [va]
	call	bfa_extra_push
	mov	[eidx], eax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [vb]
	call	bfa_extra_push
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [vc]
	call	bfa_extra_push

	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [ty]
	mov	r8d, [eidx]
	mov	r9d, 3
	call	__bfa_ps_finish
	return
endp

; "F op shape w" -- redinit. `op` is an OPEN vocabulary (only `fadd` is
; lowered by this pass's emitter; interned so any spelling round-trips).
; `shape` is the closed {ordinata, arborea} pair -> BFA_RSHAPE_*.
proc __bfa_ps_redinit, p, op
	locals
		slot fty, dd
		slot redty, dd
		slot opid, dd
		slot shpenum, dd
		slot w, dd
		slot tp2, dq
		slot tl2, dq
	endl
	mov	rdi, [p]
	call	__bfa_next_type
	mov	[fty], eax

	mov	rdi, [p]
	call	__bfa_tok_next
	mov	[tp2], rax
	mov	[tl2], rdx
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.names]
	mov	rsi, [tp2]
	mov	rdx, [tl2]
	call	intern_id
	mov	[opid], eax

	mov	rdi, [p]
	call	__bfa_tok_next
	mov	[tp2], rax
	mov	[tl2], rdx
	mov	rdi, [tp2]
	mov	rsi, [tl2]
	jmp	.after_ordinata
  .lit_ordinata db "ordinata"
  .after_ordinata:
	lea	rdx, [.lit_ordinata]
	mov	rcx, .after_ordinata - .lit_ordinata
	call	__bfa_streq
	test	eax, eax
	jz	.chk_arborea
	mov	dword [shpenum], BFA_RSHAPE_ORDINATA
	jmp	.shapedone
  .chk_arborea:
	mov	rdi, [tp2]
	mov	rsi, [tl2]
	jmp	.after_arborea
  .lit_arborea db "arborea"
  .after_arborea:
	lea	rdx, [.lit_arborea]
	mov	rcx, .after_arborea - .lit_arborea
	call	__bfa_streq
	test	eax, eax
	jnz	.is_arborea
	mov	rdi, [p]
	jmp	.after_shpmsg
  .shpmsg db "redinit: shape must be ordinata or arborea"
  .after_shpmsg:
	lea	rsi, [.shpmsg]
	mov	rdx, .after_shpmsg - .shpmsg
	call	__bfa_die
  .is_arborea:
	mov	dword [shpenum], BFA_RSHAPE_ARBOREA
  .shapedone:

	mov	rdi, [p]
	call	__bfa_next_udec
	mov	[w], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	esi, BFA_TK_RED
	xor	edx, edx
	xor	ecx, ecx
	mov	r8d, [fty]
	call	bfa_type_intern
	mov	[redty], eax

	mov	rdi, [p]
	mov	esi, [op]
	mov	edx, [opid]
	mov	ecx, [redty]
	mov	r8d, [shpenum]
	mov	r9d, [w]
	call	__bfa_ps_finish
	return
endp

; "%h %v" -- contrib. No type token, no result.
proc __bfa_ps_contrib, p, op
	locals
		slot vh, dd
		slot vv, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vh], eax
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vv], eax
	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	xor	ecx, ecx
	mov	r8d, [vh]
	mov	r9d, [vv]
	call	__bfa_ps_finish
	return
endp

; "F %h" -- redfin.
proc __bfa_ps_redfin, p, op
	locals
		slot ty, dd
		slot vh, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_type
	mov	[ty], eax
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vh], eax
	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [ty]
	mov	r8d, [vh]
	xor	r9d, r9d
	call	__bfa_ps_finish
	return
endp

; "size align" -- slot. No type token; result is always `ptr`.
proc __bfa_ps_slot, p, op
	locals
		slot tyv, dd
		slot sz, dd
		slot al, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_udec
	mov	[sz], eax
	mov	rdi, [p]
	call	__bfa_next_udec
	mov	[al], eax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	call	bfa_ty_ptr
	mov	[tyv], eax
	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [tyv]
	mov	r8d, [sz]
	mov	r9d, [al]
	call	__bfa_ps_finish
	return
endp

; "T %p off order" -- load. 3 pieces -> extra pool.
proc __bfa_ps_load, p, op
	locals
		slot ty, dd
		slot vp, dd
		slot off, dd
		slot ordv, dd
		slot eidx, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_type
	mov	[ty], eax
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vp], eax
	mov	rdi, [p]
	call	__bfa_next_udec
	mov	[off], eax
	mov	rdi, [p]
	call	__bfa_parse_order
	mov	[ordv], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [vp]
	call	bfa_extra_push
	mov	[eidx], eax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [off]
	call	bfa_extra_push
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [ordv]
	call	bfa_extra_push

	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [ty]
	mov	r8d, [eidx]
	mov	r9d, 3
	call	__bfa_ps_finish
	return
endp

; "T %p off order %v" -- store. 4 pieces -> extra pool. No result.
proc __bfa_ps_store, p, op
	locals
		slot ty, dd
		slot vp, dd
		slot off, dd
		slot ordv, dd
		slot vv, dd
		slot eidx, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_type
	mov	[ty], eax
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vp], eax
	mov	rdi, [p]
	call	__bfa_next_udec
	mov	[off], eax
	mov	rdi, [p]
	call	__bfa_parse_order
	mov	[ordv], eax
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vv], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [vp]
	call	bfa_extra_push
	mov	[eidx], eax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [off]
	call	bfa_extra_push
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [ordv]
	call	bfa_extra_push
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [vv]
	call	bfa_extra_push

	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [ty]
	mov	r8d, [eidx]
	mov	r9d, 4
	call	__bfa_ps_finish
	return
endp

; "T %p byteoff bitoff" -- loadbits.
proc __bfa_ps_loadbits, p, op
	locals
		slot ty, dd
		slot vp, dd
		slot byo, dd
		slot bio, dd
		slot eidx, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_type
	mov	[ty], eax
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vp], eax
	mov	rdi, [p]
	call	__bfa_next_udec
	mov	[byo], eax
	mov	rdi, [p]
	call	__bfa_next_udec
	mov	[bio], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [vp]
	call	bfa_extra_push
	mov	[eidx], eax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [byo]
	call	bfa_extra_push
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [bio]
	call	bfa_extra_push

	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [ty]
	mov	r8d, [eidx]
	mov	r9d, 3
	call	__bfa_ps_finish
	return
endp

; "T %p byteoff bitoff %v" -- storebits. No result.
proc __bfa_ps_storebits, p, op
	locals
		slot ty, dd
		slot vp, dd
		slot byo, dd
		slot bio, dd
		slot vv, dd
		slot eidx, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_type
	mov	[ty], eax
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vp], eax
	mov	rdi, [p]
	call	__bfa_next_udec
	mov	[byo], eax
	mov	rdi, [p]
	call	__bfa_next_udec
	mov	[bio], eax
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vv], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [vp]
	call	bfa_extra_push
	mov	[eidx], eax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [byo]
	call	bfa_extra_push
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [bio]
	call	bfa_extra_push
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [vv]
	call	bfa_extra_push

	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [ty]
	mov	r8d, [eidx]
	mov	r9d, 4
	call	__bfa_ps_finish
	return
endp

; "n %d %s" -- copy. No type token, no result.
proc __bfa_ps_copy, p, op
	locals
		slot n, dd
		slot vd, dd
		slot vs, dd
		slot eidx, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_udec
	mov	[n], eax
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vd], eax
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vs], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [n]
	call	bfa_extra_push
	mov	[eidx], eax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [vd]
	call	bfa_extra_push
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [vs]
	call	bfa_extra_push

	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	xor	ecx, ecx
	mov	r8d, [eidx]
	mov	r9d, 3
	call	__bfa_ps_finish
	return
endp

; "%p off" -- addr. Result is always `ptr`.
proc __bfa_ps_addr, p, op
	locals
		slot tyv, dd
		slot vp, dd
		slot off, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vp], eax
	mov	rdi, [p]
	call	__bfa_next_udec
	mov	[off], eax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	call	bfa_ty_ptr
	mov	[tyv], eax
	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [tyv]
	mov	r8d, [vp]
	mov	r9d, [off]
	call	__bfa_ps_finish
	return
endp

; "%p %i stride" -- index. 3 pieces -> extra pool. Result is `ptr`.
proc __bfa_ps_index, p, op
	locals
		slot tyv, dd
		slot vp, dd
		slot vi, dd
		slot stride, dd
		slot eidx, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vp], eax
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vi], eax
	mov	rdi, [p]
	call	__bfa_next_udec
	mov	[stride], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [vp]
	call	bfa_extra_push
	mov	[eidx], eax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [vi]
	call	bfa_extra_push
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [stride]
	call	bfa_extra_push

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	call	bfa_ty_ptr
	mov	[tyv], eax

	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [tyv]
	mov	r8d, [eidx]
	mov	r9d, 3
	call	__bfa_ps_finish
	return
endp

; "N" -- gaddr. Result is `ptr`; `N` is a raw global index, not a value.
proc __bfa_ps_gaddr, p, op
	locals
		slot tyv, dd
		slot n, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_udec
	mov	[n], eax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	call	bfa_ty_ptr
	mov	[tyv], eax
	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [tyv]
	mov	r8d, [n]
	xor	r9d, r9d
	call	__bfa_ps_finish
	return
endp

; "%i %n" -- chk. No type token, no result.
proc __bfa_ps_chk, p, op
	locals
		slot vi, dd
		slot vn, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vi], eax
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vn], eax
	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	xor	ecx, ecx
	mov	r8d, [vi]
	mov	r9d, [vn]
	call	__bfa_ps_finish
	return
endp

; "%r" -- retain and release both (no result).
proc __bfa_ps_retain, p, op
	locals
		slot vr, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vr], eax
	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	xor	ecx, ecx
	mov	r8d, [vr]
	xor	r9d, r9d
	call	__bfa_ps_finish
	return
endp

; "[%v]" -- ret. 0 or 1 trailing token.
proc __bfa_ps_ret, p, op
	locals
		slot vv, dd
	endl
	mov	dword [vv], 0
	mov	rdi, [p]
	call	__bfa_tok_remaining
	test	rax, rax
	jz	.noval
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vv], eax
  .noval:
	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	xor	ecx, ecx
	mov	r8d, [vv]
	xor	r9d, r9d
	call	__bfa_ps_finish
	return
endp

; "bN" -- jmp.
proc __bfa_ps_jmp, p, op
	locals
		slot btgt, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_blk
	mov	[btgt], eax
	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	xor	ecx, ecx
	mov	r8d, [btgt]
	xor	r9d, r9d
	call	__bfa_ps_finish
	return
endp

; "%c bT bF" -- br. 3 pieces -> extra pool.
proc __bfa_ps_br, p, op
	locals
		slot vc, dd
		slot btgt, dd
		slot bf, dd
		slot eidx, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_val
	mov	[vc], eax
	mov	rdi, [p]
	call	__bfa_next_blk
	mov	[btgt], eax
	mov	rdi, [p]
	call	__bfa_next_blk
	mov	[bf], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [vc]
	call	bfa_extra_push
	mov	[eidx], eax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [btgt]
	call	bfa_extra_push
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [bf]
	call	bfa_extra_push

	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	xor	ecx, ecx
	mov	r8d, [eidx]
	mov	r9d, 3
	call	__bfa_ps_finish
	return
endp

; "kind" -- trap. aux = interned kind word (an open vocabulary here by this
; file's own choice -- docs/design/ssa-ir.md section 2.3 says only that trap
; kinds "are an IR enum", never naming the actual spellings, so interning
; whatever text appears is what preserves round-trip fidelity regardless).
proc __bfa_ps_trap, p, op
	locals
		slot kid, dd
	endl
	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rsi, rax
	mov	rax, rdx
	push	rax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.names]
	pop	rdx
	call	intern_id
	mov	[kid], eax
	mov	rdi, [p]
	mov	esi, [op]
	mov	edx, [kid]
	xor	ecx, ecx
	xor	r8d, r8d
	xor	r9d, r9d
	call	__bfa_ps_finish
	return
endp

; "T (bP %v)+" -- phi. Variadic pairs to end of line; the ONE forward-
; reference case (docs/design/ssa-ir.md section 2.11).
proc __bfa_ps_phi, p, op
	locals
		slot ty, dd
		slot bpid, dd
		slot vvid, dd
		slot resolved, dd
		slot eidx, dd
		slot cnt, dd
		slot fidx, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_type
	mov	[ty], eax
	mov	dword [eidx], -1
	mov	dword [cnt], 0
  .loop:
	mov	rdi, [p]
	call	__bfa_tok_remaining
	test	rax, rax
	jz	.done

	mov	rdi, [p]
	call	__bfa_next_blk
	mov	[bpid], eax

	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rdi, [p]
	mov	rsi, rax
	call	__bfa_val_maybe
	mov	[vvid], eax
	mov	[resolved], edx

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [bpid]
	call	bfa_extra_push
	cmp	dword [eidx], -1
	jne	.have_eidx
	mov	[eidx], eax
  .have_eidx:
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	cmp	dword [resolved], 0
	jne	.push_resolved
	xor	esi, esi
	call	bfa_extra_push
	mov	[fidx], eax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.fixups]
	call	vec_push
	mov	ecx, [fidx]
	mov	[rax + BfaFixup.extra_idx], ecx
	mov	ecx, [vvid]
	mov	[rax + BfaFixup.textn], ecx
	jmp	.pushed
  .push_resolved:
	mov	esi, [vvid]
	call	bfa_extra_push
  .pushed:
	mov	eax, [cnt]
	add	eax, 2
	mov	[cnt], eax
	jmp	.loop
  .done:
	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [ty]
	mov	r8d, [eidx]
	mov	r9d, [cnt]
	call	__bfa_ps_finish
	return
endp

; "T i" -- param.
proc __bfa_ps_param, p, op
	locals
		slot ty, dd
		slot i, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_type
	mov	[ty], eax
	mov	rdi, [p]
	call	__bfa_next_udec
	mov	[i], eax
	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [ty]
	mov	r8d, [i]
	xor	r9d, r9d
	call	__bfa_ps_finish
	return
endp

; "T imm" -- iconst. 64-bit immediate, always spilled to extra as two words
; (docs/design/ssa-ir.md section 2.1's own call-out for 64-bit immediates).
proc __bfa_ps_iconst, p, op
	locals
		slot ty, dd
		slot lo, dd
		slot hi, dd
		slot eidx, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_type
	mov	[ty], eax
	mov	rdi, [p]
	call	__bfa_next_imm
	mov	ecx, eax
	mov	[lo], ecx
	shr	rax, 32
	mov	[hi], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [lo]
	call	bfa_extra_push
	mov	[eidx], eax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [hi]
	call	bfa_extra_push

	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [ty]
	mov	r8d, [eidx]
	mov	r9d, 2
	call	__bfa_ps_finish
	return
endp

; "F bits" -- fconst. Raw bit pattern, unsigned, spilled the same way.
proc __bfa_ps_fconst, p, op
	locals
		slot ty, dd
		slot lo, dd
		slot hi, dd
		slot eidx, dd
	endl
	mov	rdi, [p]
	call	__bfa_next_type
	mov	[ty], eax
	mov	rdi, [p]
	call	__bfa_next_udec
	mov	ecx, eax
	mov	[lo], ecx
	shr	rax, 32
	mov	[hi], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [lo]
	call	bfa_extra_push
	mov	[eidx], eax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [hi]
	call	bfa_extra_push

	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [ty]
	mov	r8d, [eidx]
	mov	r9d, 2
	call	__bfa_ps_finish
	return
endp

; "T @f args..." -- call. Variadic; T may be `void` (then NO `%N =`).
proc __bfa_ps_call, p, op
	locals
		slot ty, dd
		slot isvoid, dd
		slot fnidx, dd
		slot eidx, dd
		slot cnt, dd
		slot tp2, dq
		slot tl2, dq
	endl
	mov	rdi, [p]
	call	__bfa_tok_next
	mov	[tp2], rax
	mov	[tl2], rdx
	mov	rdi, [tp2]
	mov	rsi, [tl2]
	jmp	.after_void
  .lit_void db "void"
  .after_void:
	lea	rdx, [.lit_void]
	mov	rcx, .after_void - .lit_void
	call	__bfa_streq
	mov	[isvoid], eax
	test	eax, eax
	jnz	.gottype
	mov	rdi, [p]
	mov	rsi, [tp2]
	mov	rdx, [tl2]
	call	__bfa_parse_type
	mov	[ty], eax
	jmp	.typedone
  .gottype:
	mov	dword [ty], 0
  .typedone:

	mov	rax, [p]
	mov	rax, [rax + BfaParser.pending_result]
	cmp	dword [isvoid], 0
	je	.expectresult
	test	rax, rax
	jz	.okmatch
	mov	rdi, [p]
	jmp	.after_voidmsg
  .voidmsg db "call: a void callee must not assign a result"
  .after_voidmsg:
	lea	rsi, [.voidmsg]
	mov	rdx, .after_voidmsg - .voidmsg
	call	__bfa_die
  .expectresult:
	test	rax, rax
	jnz	.okmatch
	mov	rdi, [p]
	jmp	.after_needmsg
  .needmsg db "call: a non-void callee requires %N ="
  .after_needmsg:
	lea	rsi, [.needmsg]
	mov	rdx, .after_needmsg - .needmsg
	call	__bfa_die
  .okmatch:

	mov	rdi, [p]
	call	__bfa_tok_next
	cmp	byte [rax], '@'
	je	.atok_ok
	mov	rdi, [p]
	jmp	.after_atmsg
  .atmsg db "call: expected @callee"
  .after_atmsg:
	lea	rsi, [.atmsg]
	mov	rdx, .after_atmsg - .atmsg
	call	__bfa_die
  .atok_ok:
	lea	rsi, [rax + 1]
	dec	rdx
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.func_by_name]
	call	map_get
	test	rax, rax
	jnz	.calleeok
	mov	rdi, [p]
	jmp	.after_nfmsg
  .nfmsg db "call: unknown function"
  .after_nfmsg:
	lea	rsi, [.nfmsg]
	mov	rdx, .after_nfmsg - .nfmsg
	call	__bfa_die
  .calleeok:
	mov	[fnidx], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [fnidx]
	call	bfa_extra_push
	mov	[eidx], eax
	mov	dword [cnt], 1

  .argloop:
	mov	rdi, [p]
	call	__bfa_tok_remaining
	test	rax, rax
	jz	.argsdone
	mov	rdi, [p]
	call	__bfa_next_val
	mov	esi, eax
	push	rsi
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	pop	rsi
	call	bfa_extra_push
	mov	eax, [cnt]
	inc	eax
	mov	[cnt], eax
	jmp	.argloop
  .argsdone:

	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [ty]
	mov	r8d, [eidx]
	mov	r9d, [cnt]
	call	__bfa_ps_finish
	return
endp

; "T %fp args..." -- callind. Same as call, but the callee is a value.
proc __bfa_ps_callind, p, op
	locals
		slot ty, dd
		slot isvoid, dd
		slot fpid, dd
		slot eidx, dd
		slot cnt, dd
		slot tp2, dq
		slot tl2, dq
	endl
	mov	rdi, [p]
	call	__bfa_tok_next
	mov	[tp2], rax
	mov	[tl2], rdx
	mov	rdi, [tp2]
	mov	rsi, [tl2]
	jmp	.after_void
  .lit_void db "void"
  .after_void:
	lea	rdx, [.lit_void]
	mov	rcx, .after_void - .lit_void
	call	__bfa_streq
	mov	[isvoid], eax
	test	eax, eax
	jnz	.gottype
	mov	rdi, [p]
	mov	rsi, [tp2]
	mov	rdx, [tl2]
	call	__bfa_parse_type
	mov	[ty], eax
	jmp	.typedone
  .gottype:
	mov	dword [ty], 0
  .typedone:

	mov	rax, [p]
	mov	rax, [rax + BfaParser.pending_result]
	cmp	dword [isvoid], 0
	je	.expectresult
	test	rax, rax
	jz	.okmatch
	mov	rdi, [p]
	jmp	.after_voidmsg
  .voidmsg db "callind: a void callee must not assign a result"
  .after_voidmsg:
	lea	rsi, [.voidmsg]
	mov	rdx, .after_voidmsg - .voidmsg
	call	__bfa_die
  .expectresult:
	test	rax, rax
	jnz	.okmatch
	mov	rdi, [p]
	jmp	.after_needmsg
  .needmsg db "callind: a non-void callee requires %N ="
  .after_needmsg:
	lea	rsi, [.needmsg]
	mov	rdx, .after_needmsg - .needmsg
	call	__bfa_die
  .okmatch:

	mov	rdi, [p]
	call	__bfa_next_val
	mov	[fpid], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [fpid]
	call	bfa_extra_push
	mov	[eidx], eax
	mov	dword [cnt], 1

  .argloop:
	mov	rdi, [p]
	call	__bfa_tok_remaining
	test	rax, rax
	jz	.argsdone
	mov	rdi, [p]
	call	__bfa_next_val
	mov	esi, eax
	push	rsi
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	pop	rsi
	call	bfa_extra_push
	mov	eax, [cnt]
	inc	eax
	mov	[cnt], eax
	jmp	.argloop
  .argsdone:

	mov	rdi, [p]
	mov	esi, [op]
	xor	edx, edx
	mov	ecx, [ty]
	mov	r8d, [eidx]
	mov	r9d, [cnt]
	call	__bfa_ps_finish
	return
endp

; ============================================================================
; the line dispatcher
; ============================================================================

; __bfa_parse_line(p) -- one already-tokenized instruction Line (never a
; block header or '}' -- the real-pass loop filters those out first).
proc __bfa_parse_line, p
	locals
		slot op, dd
		slot shp, dd
		slot pred, dd
	endl
	mov	rdi, [p]
	xor	esi, esi
	call	__bfa_tok_at
	mov	rdi, [p]
	mov	qword [rdi + BfaParser.pending_result], 0
	cmp	byte [rax], '%'
	jne	.noresult

	mov	rdi, [p]
	call	__bfa_tok_next
	lea	rsi, [rax + 1]
	dec	rdx
	mov	rdi, [p]
	call	__bfa_parse_udec
	mov	rcx, [p]
	mov	rcx, [rcx + BfaParser.next_valn]
	cmp	rax, rcx
	je	.numok
	mov	rdi, [p]
	jmp	.after_seqmsg
  .seqmsg db "out-of-sequence %N"
  .after_seqmsg:
	lea	rsi, [.seqmsg]
	mov	rdx, .after_seqmsg - .seqmsg
	call	__bfa_die
  .numok:
	mov	rdi, [p]
	mov	qword [rdi + BfaParser.pending_result], 1

	mov	rdi, [p]
	jmp	.after_eqlit
  .eqlit db "="
  .after_eqlit:
	lea	rsi, [.eqlit]
	mov	rdx, .after_eqlit - .eqlit
	call	__bfa_tok_expect
  .noresult:

	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rsi, rax
	mov	rdi, [p]
	call	__bfa_optab_lookup
	mov	ecx, eax
	and	ecx, 0xFF
	mov	[op], ecx
	mov	ecx, eax
	shr	ecx, 8
	and	ecx, 0xFF
	mov	[shp], ecx
	mov	ecx, eax
	shr	ecx, 16
	and	ecx, 0xFF
	mov	[pred], ecx

	mov	eax, [shp]
	cmp	eax, BFA_SH_BIN
	je	.do_bin
	cmp	eax, BFA_SH_CMP
	je	.do_cmp
	cmp	eax, BFA_SH_FCMP
	je	.do_cmp
	cmp	eax, BFA_SH_UN
	je	.do_un
	cmp	eax, BFA_SH_FMA
	je	.do_fma
	cmp	eax, BFA_SH_REDINIT
	je	.do_redinit
	cmp	eax, BFA_SH_CONTRIB
	je	.do_contrib
	cmp	eax, BFA_SH_REDFIN
	je	.do_redfin
	cmp	eax, BFA_SH_SLOT
	je	.do_slot
	cmp	eax, BFA_SH_LOAD
	je	.do_load
	cmp	eax, BFA_SH_STORE
	je	.do_store
	cmp	eax, BFA_SH_LOADBITS
	je	.do_loadbits
	cmp	eax, BFA_SH_STOREBITS
	je	.do_storebits
	cmp	eax, BFA_SH_COPY
	je	.do_copy
	cmp	eax, BFA_SH_ADDR
	je	.do_addr
	cmp	eax, BFA_SH_INDEX
	je	.do_index
	cmp	eax, BFA_SH_GADDR
	je	.do_gaddr
	cmp	eax, BFA_SH_CHK
	je	.do_chk
	cmp	eax, BFA_SH_RETAIN
	je	.do_retain
	cmp	eax, BFA_SH_CALL
	je	.do_call
	cmp	eax, BFA_SH_CALLIND
	je	.do_callind
	cmp	eax, BFA_SH_RET
	je	.do_ret
	cmp	eax, BFA_SH_JMP
	je	.do_jmp
	cmp	eax, BFA_SH_BR
	je	.do_br
	cmp	eax, BFA_SH_TRAP
	je	.do_trap
	cmp	eax, BFA_SH_PHI
	je	.do_phi
	cmp	eax, BFA_SH_PARAM
	je	.do_param
	cmp	eax, BFA_SH_ICONST
	je	.do_iconst
	cmp	eax, BFA_SH_FCONST
	je	.do_fconst
	db	0x0F, 0x0B		; unreachable: optab only installs known shapes

  .do_bin:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_bin
	jmp	.end
  .do_cmp:
	mov	rdi, [p]
	mov	esi, [op]
	mov	edx, [pred]
	call	__bfa_ps_cmp
	jmp	.end
  .do_un:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_un
	jmp	.end
  .do_fma:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_fma
	jmp	.end
  .do_redinit:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_redinit
	jmp	.end
  .do_contrib:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_contrib
	jmp	.end
  .do_redfin:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_redfin
	jmp	.end
  .do_slot:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_slot
	jmp	.end
  .do_load:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_load
	jmp	.end
  .do_store:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_store
	jmp	.end
  .do_loadbits:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_loadbits
	jmp	.end
  .do_storebits:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_storebits
	jmp	.end
  .do_copy:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_copy
	jmp	.end
  .do_addr:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_addr
	jmp	.end
  .do_index:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_index
	jmp	.end
  .do_gaddr:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_gaddr
	jmp	.end
  .do_chk:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_chk
	jmp	.end
  .do_retain:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_retain
	jmp	.end
  .do_call:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_call
	jmp	.end
  .do_callind:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_callind
	jmp	.end
  .do_ret:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_ret
	jmp	.end
  .do_jmp:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_jmp
	jmp	.end
  .do_br:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_br
	jmp	.end
  .do_trap:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_trap
	jmp	.end
  .do_phi:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_phi
	jmp	.end
  .do_param:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_param
	jmp	.end
  .do_iconst:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_iconst
	jmp	.end
  .do_fconst:
	mov	rdi, [p]
	mov	esi, [op]
	call	__bfa_ps_fconst
  .end:
	return
endp

; ============================================================================
; function bodies: prescan (block density) then the real pass
; ============================================================================

; __bfa_prescan_blocks(p) -- p.func already set, p.pos/line_no sitting right
; after the `functio ... {` header line. Creates every block, densely, then
; rewinds pos/line_no so the real pass reads the same lines again.
proc __bfa_prescan_blocks, p
	locals
		slot savedpos, dq
		slot savedline, dq
		slot expectn, dd
	endl
	mov	rdi, [p]
	mov	rax, [rdi + BfaParser.pos]
	mov	[savedpos], rax
	mov	rax, [rdi + BfaParser.line_no]
	mov	[savedline], rax
	mov	dword [expectn], 0

  .loop:
	mov	rdi, [p]
	call	__bfa_p_next_nonblank
	test	eax, eax
	jnz	.gotline
	mov	rdi, [p]
	jmp	.after_eofmsg
  .eofmsg db "unterminated function (missing '}')"
  .after_eofmsg:
	lea	rsi, [.eofmsg]
	mov	rdx, .after_eofmsg - .eofmsg
	call	__bfa_die
  .gotline:
	mov	rdi, [p]
	xor	esi, esi
	call	__bfa_tok_at
	mov	rcx, [p]
	mov	rcx, [rcx + BfaParser.ntoks]
	cmp	rcx, 1
	jne	.notclosebrace
	cmp	rdx, 1
	jne	.notclosebrace
	cmp	byte [rax], '}'
	jne	.notclosebrace
	jmp	.prescandone
  .notclosebrace:
	mov	rdi, rax
	mov	rsi, rdx
	call	__bfa_tok_is_blockhdr
	test	eax, eax
	jz	.loop
	mov	rdi, [p]
	xor	esi, esi
	call	__bfa_tok_at
	mov	rdi, [p]
	mov	rsi, rax
	mov	rdx, rdx
	call	__bfa_blockhdr_n
	cmp	eax, [expectn]
	je	.blockseqok
	mov	rdi, [p]
	jmp	.after_seqmsg2
  .seqmsg2 db "out-of-sequence block bN"
  .after_seqmsg2:
	lea	rsi, [.seqmsg2]
	mov	rdx, .after_seqmsg2 - .seqmsg2
	call	__bfa_die
  .blockseqok:
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	call	bfa_block_new
	dec	eax
	cmp	eax, [expectn]
	je	.idok
	db	0x0F, 0x0B		; internal contract: bfa_block_new must
					; return expectn+1 here -- this file's own
					; bug if it ever fires, not a text error
  .idok:
	mov	eax, [expectn]
	inc	eax
	mov	[expectn], eax
	jmp	.loop

  .prescandone:
	mov	rdi, [p]
	mov	rax, [savedpos]
	mov	[rdi + BfaParser.pos], rax
	mov	rax, [savedline]
	mov	[rdi + BfaParser.line_no], rax
	return
endp

; __bfa_realpass_body(p) -- consumes lines until (and including) the
; function's closing `}`, dispatching block headers and instruction lines.
proc __bfa_realpass_body, p
	locals
	endl
  .loop:
	mov	rdi, [p]
	call	__bfa_p_next_nonblank
	test	eax, eax
	jnz	.gotline
	mov	rdi, [p]
	jmp	.after_eofmsg
  .eofmsg db "unterminated function (missing '}')"
  .after_eofmsg:
	lea	rsi, [.eofmsg]
	mov	rdx, .after_eofmsg - .eofmsg
	call	__bfa_die
  .gotline:
	mov	rdi, [p]
	xor	esi, esi
	call	__bfa_tok_at
	mov	rcx, [p]
	mov	rcx, [rcx + BfaParser.ntoks]
	cmp	rcx, 1
	jne	.notend
	cmp	rdx, 1
	jne	.notend
	cmp	byte [rax], '}'
	jne	.notend
	return
  .notend:
	mov	rdi, rax
	mov	rsi, rdx
	call	__bfa_tok_is_blockhdr
	test	eax, eax
	jz	.instline

	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rdi, [p]
	mov	rsi, rax
	mov	rdx, rdx
	call	__bfa_blockhdr_n
	inc	rax
	mov	rdi, [p]
	mov	[rdi + BfaParser.curblock], rax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	rsi, rax
	call	bfa_block_ptr
	mov	rcx, [p]
	mov	rcx, [rcx + BfaParser.pending_off]
	mov	[rax + BfaBlock.span_start], ecx
	mov	rcx, [p]
	mov	rcx, [rcx + BfaParser.pending_len]
	mov	[rax + BfaBlock.span_len], ecx
	mov	dword [rax + BfaBlock.span_file], 0

	mov	rdi, [p]
	call	__bfa_tok_remaining
	test	rax, rax
	jz	.hdrdone
	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rdi, rax
	mov	rsi, rdx
	jmp	.after_quisque
  .lit_quisque db "quisque"
  .after_quisque:
	lea	rdx, [.lit_quisque]
	mov	rcx, .after_quisque - .lit_quisque
	call	__bfa_streq
	test	eax, eax
	jnz	.isquisque
	mov	rdi, [p]
	jmp	.after_qmsg
  .qmsg db "unexpected token after block header"
  .after_qmsg:
	lea	rsi, [.qmsg]
	mov	rdx, .after_qmsg - .qmsg
	call	__bfa_die
  .isquisque:
	mov	rsi, [p]
	mov	rsi, [rsi + BfaParser.curblock]
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.flags]
	or	ecx, BFA_BLOCK_FLAG_QUISQUE
	mov	[rax + BfaBlock.flags], ecx
	mov	rdi, [p]
	call	__bfa_tok_remaining
	test	rax, rax
	jz	.hdrdone
	mov	rdi, [p]
	jmp	.after_qmsg2
  .qmsg2 db "unexpected token after block header"
  .after_qmsg2:
	lea	rsi, [.qmsg2]
	mov	rdx, .after_qmsg2 - .qmsg2
	call	__bfa_die
  .hdrdone:
	jmp	.loop

  .instline:
	mov	rax, [p]
	mov	rax, [rax + BfaParser.curblock]
	test	rax, rax
	jnz	.haveblock
	mov	rdi, [p]
	jmp	.after_nbmsg
  .nbmsg db "instruction outside any block"
  .after_nbmsg:
	lea	rsi, [.nbmsg]
	mov	rdx, .after_nbmsg - .nbmsg
	call	__bfa_die
  .haveblock:
	mov	rdi, [p]
	call	__bfa_parse_line
	jmp	.loop
endp

; ============================================================================
; function and parameter-list headers
; ============================================================================

; __bfa_parse_params(p) -> rax = start index into module.sig_extra,
; rdx = count. Handles the ONE place `(`/`)` are glued to an adjacent type
; token with no space (docs/design/ssa-ir.md section 2.11's own worked
; example: `(ptr ptr u64)`) -- stripped as leading/trailing punctuation on
; whichever token carries it, on either side independently, so `(u64)`,
; `( u64 )` and `()`/`( )` all parse the same way.
proc __bfa_parse_params, p
	locals
		slot cnt, dd
		slot pstart0, dd
		slot first, dd
		slot closes, dd
		slot tp2, dq
		slot tl2, dq
	endl
	mov	dword [cnt], 0
	mov	dword [first], 1

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.sig_extra]
	mov	rax, [rdi + Vec.len]
	mov	[pstart0], eax

  .loop:
	mov	rdi, [p]
	call	__bfa_tok_next
	mov	[tp2], rax
	mov	[tl2], rdx

	cmp	dword [first], 0
	je	.nofirst
	mov	dword [first], 0
	mov	rax, [tp2]
	cmp	byte [rax], '('
	je	.openok
	mov	rdi, [p]
	jmp	.after_openmsg
  .openmsg db "expected '(' to start the parameter list"
  .after_openmsg:
	lea	rsi, [.openmsg]
	mov	rdx, .after_openmsg - .openmsg
	call	__bfa_die
  .openok:
	mov	rax, [tp2]
	inc	rax
	mov	[tp2], rax
	mov	rax, [tl2]
	dec	rax
	mov	[tl2], rax
  .nofirst:

	mov	dword [closes], 0
	mov	rax, [tl2]
	test	rax, rax
	jz	.checkempty
	mov	rax, [tp2]
	mov	rcx, [tl2]
	movzx	edx, byte [rax + rcx - 1]
	cmp	dl, ')'
	jne	.checkempty
	mov	dword [closes], 1
	mov	rax, [tl2]
	dec	rax
	mov	[tl2], rax
  .checkempty:
	mov	rax, [tl2]
	test	rax, rax
	jz	.skiptype

	mov	rdi, [p]
	mov	rsi, [tp2]
	mov	rdx, [tl2]
	call	__bfa_parse_type
	push	rax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.sig_extra]
	call	vec_push
	pop	rcx
	mov	[rax], ecx
	mov	eax, [cnt]
	inc	eax
	mov	[cnt], eax
  .skiptype:

	cmp	dword [closes], 1
	je	.finished
	jmp	.loop

  .finished:
	mov	eax, [pstart0]
	mov	edx, [cnt]
	return
endp

; __bfa_parse_attrs(p, func) -- consumes `numeri`/`nucleus`/`externus`
; attributes up to and including the opening '{', which must be the header
; line's last token.
proc __bfa_parse_attrs, p, func
	locals
		slot tp2, dq
		slot tl2, dq
	endl
  .loop:
	mov	rdi, [p]
	call	__bfa_tok_next
	mov	[tp2], rax
	mov	[tl2], rdx

	mov	rdi, [tp2]
	mov	rsi, [tl2]
	jmp	.after_brace
  .lit_brace db "{"
  .after_brace:
	lea	rdx, [.lit_brace]
	mov	rcx, .after_brace - .lit_brace
	call	__bfa_streq
	test	eax, eax
	jz	.notbrace
	mov	rdi, [p]
	call	__bfa_tok_remaining
	test	rax, rax
	jz	.done
	mov	rdi, [p]
	jmp	.after_trailmsg
  .trailmsg db "unexpected tokens after '{'"
  .after_trailmsg:
	lea	rsi, [.trailmsg]
	mov	rdx, .after_trailmsg - .trailmsg
	call	__bfa_die
  .notbrace:

	mov	rdi, [tp2]
	mov	rsi, [tl2]
	jmp	.after_numeri
  .lit_numeri db "numeri"
  .after_numeri:
	lea	rdx, [.lit_numeri]
	mov	rcx, .after_numeri - .lit_numeri
	call	__bfa_streq
	test	eax, eax
	jz	.notnumeri

	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rsi, rax
	mov	rax, rdx
	push	rax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.names]
	pop	rdx
	call	intern_id
	mov	rdi, [func]
	mov	[rdi + BfaFunc.numeri0], eax

	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rsi, rax
	mov	rax, rdx
	push	rax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.names]
	pop	rdx
	call	intern_id
	mov	rdi, [func]
	mov	[rdi + BfaFunc.numeri1], eax

	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rsi, rax
	mov	rax, rdx
	push	rax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.names]
	pop	rdx
	call	intern_id
	mov	rdi, [func]
	mov	[rdi + BfaFunc.numeri2], eax

	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rsi, rax
	mov	rax, rdx
	push	rax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.names]
	pop	rdx
	call	intern_id
	mov	rdi, [func]
	mov	[rdi + BfaFunc.numeri3], eax
	jmp	.loop
  .notnumeri:

	mov	rdi, [tp2]
	mov	rsi, [tl2]
	jmp	.after_nucleus
  .lit_nucleus db "nucleus"
  .after_nucleus:
	lea	rdx, [.lit_nucleus]
	mov	rcx, .after_nucleus - .lit_nucleus
	call	__bfa_streq
	test	eax, eax
	jz	.notnucleus
	mov	rdi, [func]
	mov	eax, [rdi + BfaFunc.attrs]
	or	eax, BFA_FUNC_ATTR_NUCLEUS
	mov	[rdi + BfaFunc.attrs], eax
	jmp	.loop
  .notnucleus:

	mov	rdi, [tp2]
	mov	rsi, [tl2]
	jmp	.after_externus
  .lit_externus db "externus"
  .after_externus:
	lea	rdx, [.lit_externus]
	mov	rcx, .after_externus - .lit_externus
	call	__bfa_streq
	test	eax, eax
	jz	.notexternus
	mov	rdi, [func]
	mov	eax, [rdi + BfaFunc.attrs]
	or	eax, BFA_FUNC_ATTR_EXTERNUS
	mov	[rdi + BfaFunc.attrs], eax
	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rsi, rax
	mov	rax, rdx
	push	rax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.names]
	pop	rdx
	call	intern_id
	mov	rdi, [func]
	mov	[rdi + BfaFunc.extern_abi], eax
	jmp	.loop
  .notexternus:

	mov	rdi, [p]
	jmp	.after_attrmsg
  .attrmsg db "unknown function attribute"
  .after_attrmsg:
	lea	rsi, [.attrmsg]
	mov	rdx, .after_attrmsg - .attrmsg
	call	__bfa_die

  .done:
	return
endp

; __bfa_parse_function(p) -- the `functio` keyword token has already been
; consumed by bfa_parse_module; everything else (header through the
; matching `}`) is parsed here.
proc __bfa_parse_function, p
	locals
		slot func, dq
		slot rty, dd
		slot pstart, dd
		slot pcount, dd
		slot sigidx, dd
		slot tp2, dq
		slot tl2, dq
		slot fixn, dd
		slot fixcnt, dd
		slot fixi, dd
		slot fixval, dd
	endl
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	call	bfa_func_new
	mov	[func], rax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.pending_off]
	mov	rsi, [p]
	mov	rsi, [rsi + BfaParser.pending_len]
	mov	rcx, [func]
	mov	dword [rcx + BfaFunc.span_file], 0
	mov	[rcx + BfaFunc.span_start], edi
	mov	[rcx + BfaFunc.span_len], esi

	mov	rdi, [p]
	call	__bfa_tok_next
	cmp	byte [rax], '@'
	je	.atok_ok
	mov	rdi, [p]
	jmp	.after_atmsg
  .atmsg db "functio: expected @name"
  .after_atmsg:
	lea	rsi, [.atmsg]
	mov	rdx, .after_atmsg - .atmsg
	call	__bfa_die
  .atok_ok:
	lea	rsi, [rax + 1]
	dec	rdx
	mov	[tp2], rsi
	mov	[tl2], rdx
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.names]
	mov	rsi, [tp2]
	mov	rdx, [tl2]
	call	intern_id
	mov	rcx, [func]
	mov	[rcx + BfaFunc.name], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	rax, [rdi + Vec.len]
	mov	[sigidx], eax
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.func_by_name]
	mov	rsi, [tp2]
	mov	rdx, [tl2]
	mov	ecx, [sigidx]
	call	map_insert

	mov	rdi, [p]
	call	__bfa_parse_params
	mov	[pstart], eax
	mov	[pcount], edx

	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rdi, rax
	mov	rsi, rdx
	jmp	.after_arrow
  .lit_arrow db "->"
  .after_arrow:
	lea	rdx, [.lit_arrow]
	mov	rcx, .after_arrow - .lit_arrow
	call	__bfa_streq
	test	eax, eax
	jnz	.arrowok
	mov	rdi, [p]
	jmp	.after_arrowmsg
  .arrowmsg db "functio: expected '->'"
  .after_arrowmsg:
	lea	rsi, [.arrowmsg]
	mov	rdx, .after_arrowmsg - .arrowmsg
	call	__bfa_die
  .arrowok:

	mov	rdi, [p]
	call	__bfa_tok_next
	mov	[tp2], rax
	mov	[tl2], rdx
	mov	rdi, [tp2]
	mov	rsi, [tl2]
	jmp	.after_void2
  .lit_void2 db "void"
  .after_void2:
	lea	rdx, [.lit_void2]
	mov	rcx, .after_void2 - .lit_void2
	call	__bfa_streq
	test	eax, eax
	jnz	.retvoid
	mov	rdi, [p]
	mov	rsi, [tp2]
	mov	rdx, [tl2]
	call	__bfa_parse_type
	mov	[rty], eax
	jmp	.retdone
  .retvoid:
	mov	dword [rty], 0
  .retdone:

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.sigs]
	call	vec_push
	mov	ecx, [rty]
	mov	[rax + BfaSig.ret_ty], ecx
	mov	ecx, [pstart]
	mov	[rax + BfaSig.p_first], ecx
	mov	ecx, [pcount]
	mov	[rax + BfaSig.p_count], ecx

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.sigs]
	mov	rax, [rdi + Vec.len]
	dec	eax
	mov	rcx, [func]
	mov	[rcx + BfaFunc.sig], eax

	mov	rdi, [p]
	mov	rsi, [func]
	call	__bfa_parse_attrs

	mov	rax, [p]
	mov	rcx, [func]
	mov	[rax + BfaParser.func], rcx
	call	__bfa_prescan_blocks

	mov	rdi, [p]
	mov	qword [rdi + BfaParser.curblock], 0

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.scratch]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	rcx, rax
	mov	rdi, [p]
	mov	[rdi + BfaParser.val_map], rcx
	mov	rdi, rcx
	mov	rsi, [p]
	mov	rsi, [rsi + BfaParser.scratch]
	mov	rdx, 4
	xor	rcx, rcx
	call	vec_init

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.scratch]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	rcx, rax
	mov	rdi, [p]
	mov	[rdi + BfaParser.fixups], rcx
	mov	rdi, rcx
	mov	rsi, [p]
	mov	rsi, [rsi + BfaParser.scratch]
	mov	rdx, sizeof.BfaFixup
	xor	rcx, rcx
	call	vec_init

	mov	rdi, [p]
	mov	qword [rdi + BfaParser.next_valn], 0

	mov	rdi, [p]
	call	__bfa_realpass_body

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.fixups]
	mov	rax, [rdi + Vec.len]
	mov	[fixcnt], eax
	mov	dword [fixi], 0
  .fixloop:
	mov	eax, [fixi]
	cmp	eax, [fixcnt]
	jge	.fixdone
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.fixups]
	mov	esi, [fixi]
	call	vec_get
	mov	ecx, [rax + BfaFixup.textn]
	mov	edx, [rax + BfaFixup.extra_idx]
	mov	[fixn], ecx
	mov	[fixval], edx		; temporarily holds extra_idx

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.val_map]
	mov	rax, [rdi + Vec.len]
	cmp	[fixn], eax
	jl	.fixok
	mov	rdi, [p]
	jmp	.after_fixmsg
  .fixmsg db "phi references a %N never defined"
  .after_fixmsg:
	lea	rsi, [.fixmsg]
	mov	rdx, .after_fixmsg - .fixmsg
	call	__bfa_die
  .fixok:
	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.val_map]
	movsxd	rsi, dword [fixn]
	call	vec_get
	mov	eax, [rax]

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.func]
	mov	esi, [fixval]		; extra_idx
	mov	edx, eax		; resolved internal id
	call	bfa_extra_set

	mov	eax, [fixi]
	inc	eax
	mov	[fixi], eax
	jmp	.fixloop
  .fixdone:
	return
endp

; ============================================================================
; module top level: `data` globals and `functio` dispatch
; ============================================================================

; __bfa_parse_global(p) -- `data $ID SIZE ALIGN HEXBYTES`, the `data` token
; already peeked (not yet consumed) by bfa_parse_module.
proc __bfa_parse_global, p
	locals
		slot id, dd
		slot sz, dd
		slot al, dd
		slot doff, dd
		slot dlen, dd
		slot hp, dq
		slot hn, dq
		slot hi, dq
		slot hib, dd
	endl
	mov	rdi, [p]
	call	__bfa_tok_next			; consume "data"
	mov	rdi, [p]
	call	__bfa_tok_next			; "$N"
	cmp	byte [rax], '$'
	je	.dollarok
	mov	rdi, [p]
	jmp	.after_dmsg
  .dmsg db "data: expected $ID"
  .after_dmsg:
	lea	rsi, [.dmsg]
	mov	rdx, .after_dmsg - .dmsg
	call	__bfa_die
  .dollarok:
	lea	rsi, [rax + 1]
	dec	rdx
	mov	rdi, [p]
	call	__bfa_parse_udec
	mov	[id], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.globals]
	mov	rax, [rdi + Vec.len]
	cmp	[id], eax
	je	.idok
	mov	rdi, [p]
	jmp	.after_gseqmsg
  .gseqmsg db "out-of-sequence global $N"
  .after_gseqmsg:
	lea	rsi, [.gseqmsg]
	mov	rdx, .after_gseqmsg - .gseqmsg
	call	__bfa_die
  .idok:

	mov	rdi, [p]
	call	__bfa_next_udec
	mov	[sz], eax
	mov	rdi, [p]
	call	__bfa_next_udec
	mov	[al], eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.gdata]
	mov	rax, [rdi + Vec.len]
	mov	[doff], eax

	mov	rdi, [p]
	call	__bfa_tok_next
	mov	[hp], rax
	mov	[hn], rdx
	test	rdx, 1
	jz	.evenlen
	mov	rdi, [p]
	jmp	.after_hexmsg
  .hexmsg db "data: HEXBYTES must have an even number of hex digits"
  .after_hexmsg:
	lea	rsi, [.hexmsg]
	mov	rdx, .after_hexmsg - .hexmsg
	call	__bfa_die
  .evenlen:
	mov	qword [hi], 0
	mov	dword [dlen], 0
  .hexloop:
	mov	rax, [hi]
	cmp	rax, [hn]
	jge	.hexdone
	mov	rcx, [hp]
	movzx	esi, byte [rcx + rax]
	mov	rdi, [p]
	call	__bfa_hexnib
	mov	[hib], eax

	mov	rax, [hi]
	inc	rax
	mov	rcx, [hp]
	movzx	esi, byte [rcx + rax]
	mov	rdi, [p]
	call	__bfa_hexnib
	mov	ecx, [hib]
	shl	ecx, 4
	or	ecx, eax

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.gdata]
	push	rcx
	call	vec_push
	pop	rcx
	mov	[rax], cl

	mov	rax, [hi]
	add	rax, 2
	mov	[hi], rax
	mov	eax, [dlen]
	inc	eax
	mov	[dlen], eax
	jmp	.hexloop
  .hexdone:

	mov	rdi, [p]
	mov	rdi, [rdi + BfaParser.module]
	mov	rdi, [rdi + BfaModule.globals]
	call	vec_push
	mov	ecx, [id]
	mov	[rax + BfaGlobal.id], ecx
	mov	ecx, [sz]
	mov	[rax + BfaGlobal.size], ecx
	mov	ecx, [al]
	mov	[rax + BfaGlobal.algn], ecx
	mov	ecx, [doff]
	mov	[rax + BfaGlobal.data_off], ecx
	mov	ecx, [dlen]
	mov	[rax + BfaGlobal.data_len], ecx
	return
endp

; bfa_parse_module(ir_arena, scratch_arena, src_ptr, src_len) -> rax = the
; parsed BfaModule*. Never returns an error through CF/eax -- malformed
; text is fatal (this file's header explains why). `ir_arena` backs the
; whole IR image for the parsed module's lifetime; `scratch_arena` is reset
; once per function by the CALLER between functions is NOT required here --
; this file resets nothing itself (each function gets fresh val_map/fixups
; Vecs from whatever `scratch_arena` currently offers), so a caller that
; wants the H1-documented per-function reset (docs/design/ssa-ir.md
; section 5) does it between calls to this function at a coarser grain, or
; simply sizes scratch generously enough for a whole module -- this pass's
; own test harness does the latter (see the report for the arena sizes
; chosen and why).
proc bfa_parse_module, ir_arena, scratch_arena, src_ptr, src_len
	locals
		slot p, dq
		slot mdl, dq
	endl
	mov	rdi, [ir_arena]
	mov	rsi, sizeof.BfaModule
	call	arena_alloc
	mov	[mdl], rax
	mov	rdi, rax
	mov	rsi, [ir_arena]
	call	bfa_module_init

	mov	rdi, [ir_arena]
	mov	rsi, sizeof.BfaParser
	call	arena_alloc
	mov	[p], rax

	mov	rcx, [mdl]
	mov	rdi, [p]
	mov	[rdi + BfaParser.module], rcx
	mov	rcx, [ir_arena]
	mov	rdi, [p]
	mov	[rdi + BfaParser.ir_arena], rcx
	mov	rcx, [scratch_arena]
	mov	rdi, [p]
	mov	[rdi + BfaParser.scratch], rcx
	mov	rcx, [src_ptr]
	mov	rdi, [p]
	mov	[rdi + BfaParser.src_ptr], rcx
	mov	rcx, [src_len]
	mov	rdi, [p]
	mov	[rdi + BfaParser.src_len], rcx
	mov	rdi, [p]
	mov	qword [rdi + BfaParser.pos], 0
	mov	rdi, [p]
	mov	qword [rdi + BfaParser.line_no], 0
	mov	rdi, [p]
	mov	qword [rdi + BfaParser.func], 0
	mov	rdi, [p]
	mov	qword [rdi + BfaParser.curblock], 0

	mov	rdi, [ir_arena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	rdi, [p]
	mov	[rdi + BfaParser.toks], rax

	mov	rdi, [p]
	call	__bfa_optab_init

  .toplevel:
	mov	rdi, [p]
	call	__bfa_p_next_nonblank
	test	eax, eax
	jz	.eof

	mov	rdi, [p]
	xor	esi, esi
	call	__bfa_tok_at

	mov	rdi, rax
	mov	rsi, rdx
	jmp	.after_data
  .lit_data db "data"
  .after_data:
	lea	rdx, [.lit_data]
	mov	rcx, .after_data - .lit_data
	call	__bfa_streq
	test	eax, eax
	jz	.notdata
	mov	rdi, [p]
	call	__bfa_parse_global
	jmp	.toplevel
  .notdata:

	mov	rdi, [p]
	xor	esi, esi
	call	__bfa_tok_at
	mov	rdi, rax
	mov	rsi, rdx
	jmp	.after_functio
  .lit_functio db "functio"
  .after_functio:
	lea	rdx, [.lit_functio]
	mov	rcx, .after_functio - .lit_functio
	call	__bfa_streq
	test	eax, eax
	jz	.badtop
	mov	rdi, [p]
	call	__bfa_tok_next
	mov	rdi, [p]
	call	__bfa_parse_function
	jmp	.toplevel

  .badtop:
	mov	rdi, [p]
	jmp	.after_topmsg
  .topmsg db "expected 'data' or 'functio'"
  .after_topmsg:
	lea	rsi, [.topmsg]
	mov	rdx, .after_topmsg - .topmsg
	call	__bfa_die

  .eof:
	mov	rax, [mdl]
	return
endp
