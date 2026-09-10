; compiler/x86_64/driver/cli.inc
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
; The command line: `DrvCtx`, the subcommand table, and the flag parser.
;
; §12 pins the invocation: `exsc aedifica --hospes TRIPLE SOURCE -o OUT`, and
; *"a subcommand is required; there is no bare form."* §9.5 pins the one flag
; that has no default: *"`exsc` with no `--hospes` is an error. No
; default-to-build-platform."* Both are enforced here, not further down.
;
; NOTHING HERE READS THE ENVIRONMENT, AND THE CODE SHOWS IT. At process entry
; the stack holds argc, then argc argv pointers, then a NULL, then envp. This
; parser is handed `argc` and `&argv[0]` and indexes strictly below `argc`, so
; the NULL terminator is never even loaded, let alone stepped past. There is
; no `getenv` in this binary because there is no libc in this binary, and
; §9.3's *"`--env KEY=VALUE` supplies a value; it does not permit a read"* is
; therefore structural rather than promised: a value that never arrives on the
; command line does not exist as far as `exsc` is concerned.
;
; A REPEATED OPTION IS AN ERROR. §9.3 states this for `--env` and gives the
; reason: *"A repeated `KEY` is an error, not a last-wins. Last-wins makes the
; result depend on argument order, which is exactly the class this section
; removes."* That reason is not specific to `--env`, so it is applied to
; `--hospes`, `-o`, `--epoch` and `--diagnostica` as well, and to a second
; positional SOURCE. RECORDED AS AN INFERENCE, not as spec text: §9.3 says it
; about `--env` only, and if the spec ever wants last-wins for the others,
; this file is the one place that changes.
;
; `--env KEY` IS VALIDATED BY THE LEXER, NOT BY A SECOND IDENTIFIER RULE.
; §9.3 says *"`KEY` matches an identifier (§8.2)"*, and §8.2 is 30 lines of
; UAX #31 and UTS #39 that lexer/ already implements exactly. `drv_ident_ok`
; therefore runs `lex_run` over the key bytes and accepts exactly one token
; spanning all of them, with no diagnostic. That buys §8.1's dangerous-
; character class and §8.2's mixed-script rule for free and, more to the
; point, makes it impossible for the driver's notion of an identifier to
; drift from the compiler's. Cost, stated: a `--env` key that spells a §8.4
; reserved word lexes as `TOK_KEYWORD` rather than `TOK_IDENT`, and is
; ACCEPTED here -- the question §9.3 asks is whether the key is spelled like
; an identifier (§8.2), and §8.4's "never usable as an identifier" is a
; grammar rule about source, not about a flag.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.2 (identifiers), §9.3 (`--env`,
; `--epoch`, purity), §9.5 (`--hospes`), §12 (the invocation).
; -----------------------------------------------------------------------------

include 'msg.inc'

; ---- the compilation context ----------------------------------------------
; ONE object, allocated by compiler/x86_64/exsc.asm, pointed at by `r15` from
; the first instruction of the process onward -- docs/asm-conventions.md,
; "1.2 The r15 pin": *"established once, at process entry, before control
; reaches any internal routine, and from that point on any internal routine
; may dereference it without receiving it as an argument."* No routine below
; assigns to r15 and none lists it in a `uses`, because it is callee-saved
; under stock SysV already and nothing in rt/, diag/, lexer/ or
; compiler/shared/unicode/ touches it (checked by grep across the tree: the
; only other mention is a comment in nfc.inc explaining that it must not be
; used as scratch).
;
; The three arenas are distinct on purpose and the distinction is load-
; bearing, not tidiness. rt/intern.inc: *"NEVER `arena_reset` the arena
; backing a live interner."* `scratch` is reset once per `--env` key;
; `iarena` never is; `arena` is not even created until the source file's size
; is known, so it can be sized from it (see driver/io.inc).
struct DrvCtx
	field cmd,       dd	; DRV_CMD_*
	field diagmode,  dd	; DIAG_MODE_TEXT | DIAG_MODE_JSON
	field dmset,     dd	; 1 once --diagnostica has been given
	field epochset,  dd	; 1 once --epoch has been given
	field nenv,      dd	; --env bindings accepted so far
	field pad0,      dd
	field emitte,    dd	; DRV_EMIT_* -- spec 18.2's stage-dump mitigation
	field emitteset, dd	; 1 once --emitte has been given
	field epoch,     dq	; §9.3: decimal seconds, default 0
	field hospes,    dq	; borrowed argv bytes, never copied
	field hospesn,   dq
	field srcpath,   dq
	field srcpathn,  dq
	field outpath,   dq
	field outpathn,  dq
	field arena,     dq	; -> Arena, the compilation. Sized in io.inc.
	field iarena,    dq	; -> Arena, interner + env map. Never reset.
	field scratch,   dq	; -> Arena, reset once per --env key.
	field interner,  dq	; -> Interner
	field envmap,    dq	; -> Map, insertion-ordered (CLAUDE.md)
	field vlx,       dq	; -> Lexer  }
	field vtoks,     dq	; -> Vec    } --env key validation only
	field vdiags,    dq	; -> Vec    }
	field lx,        dq	; -> Lexer  }
	field toks,      dq	; -> Vec    } the compilation
	field diags,     dq	; -> Vec    }
	field green,     dq	; -> Vec of CstGreen }
	field work,      dq	; -> Vec of dd        } the §9.1 CST; cst/
	field cmap,      dq	; -> Map              } allocates nothing of
	field ctree,     dq	; -> CstTree          } its own
	field parser,    dq	; -> CstParser, the §8.6 parse
	field ast,       dq	; -> Ast, the §9.1 Stage 1 typed tree
	field chk,       dq	; -> ChkCtx, sizeof.ChkCtx = 328 (checker/checker.inc,
				; checker/resolve/resolve.inc). Stage 2: resolve
				; (passes 0/1) and, once CHK_S_TYPES is set by a
				; types pass that does not exist yet, rows/layout.
				; Bound the same way `vlx`/`vdiags` are: a fixed
				; block in exsc.asm's writable segment, pointed at
				; once from `start`, never (re)allocated from an
				; arena. `chk_init` is called fresh each run and
				; re-zeroes what it owns; nothing here needs the
				; block pre-zeroed the way BSS already leaves it.
end struct

; ---- the subcommand set, exactly §12's list --------------------------------
DRV_CMD_NONE      = -1
DRV_CMD_AEDIFICA  = 0
DRV_CMD_PROBA     = 1
DRV_CMD_FORMA     = 2
DRV_CMD_LSP       = 3
DRV_CMD_EGO       = 4
DRV_CMD_EMENDA    = 5
DRV_CMD_DOCUMENTA = 6
DRV_CMD_NOVUM     = 7
DRV_CMD_LEXICON   = 8
DRV_CMD_CURRE     = 9

drv_c0 db 'aedifica',0
drv_c1 db 'proba',0
drv_c2 db 'forma',0
drv_c3 db 'lsp',0
drv_c4 db 'ego',0
drv_c5 db 'emenda',0
drv_c6 db 'documenta',0
drv_c7 db 'novum',0
drv_c8 db 'lexicon',0
drv_c9 db 'curre',0

; Order is §12's own sentence order, written out one entry per line -- the
; same reason diag/codes.inc keeps §13's table order. Nothing sorts this.
drv_cmd_tab:
	dq drv_c0, drv_c1, drv_c2, drv_c3, drv_c4
	dq drv_c5, drv_c6, drv_c7, drv_c8, drv_c9
drv_cmd_tab_end:
DRV_CMD_COUNT = (drv_cmd_tab_end - drv_cmd_tab) / 8
assert DRV_CMD_COUNT = 10

; ---- pipeline dump stages, spec §18.2's mitigation --------------------------
; "Debugging degrades ... Mitigated by stage dumps at every pipeline
; boundary rather than by a debugger." Three boundaries exist to dump today
; (lex, parse, Stage 1 typed AST); §16 Stages 2/3 (type checker, SSA, a
; backend) do not exist yet, so there is nothing past `ast` to add a value
; for. DRV_EMIT_NONE is the "nothing requested" default and is deliberately
; NOT 0 -- 0 is `DRV_EMIT_TOKENS`, and a zeroed `DrvCtx` (real BSS, or a
; fixture that forgets to call `drv_ctx_reset`) must not silently behave as
; if `--emitte tokens` had been given.
DRV_EMIT_NONE   = -1
DRV_EMIT_TOKENS = 0
DRV_EMIT_CST    = 1
DRV_EMIT_AST    = 2

; ---- option spellings ------------------------------------------------------
drv_o_hospes db '--hospes',0
drv_o_out    db '-o',0
drv_o_env    db '--env',0
drv_o_epoch  db '--epoch',0
drv_o_diagn  db '--diagnostica',0
drv_o_emitte db '--emitte',0
drv_v_textus db 'textus',0
drv_v_json   db 'json',0
drv_v_tokens db 'tokens',0
drv_v_cst    db 'cst',0
drv_v_ast    db 'ast',0

; The pseudo-path a `--env` key is lexed under. It reaches nothing but the
; interner and a discarded diagnostic vector; it exists because
; `lex_set_source` interns a path unconditionally and a compilation is
; entitled to know which file a span came from.
drv_s_envpath db '--env'
DRV_S_ENVPATH_LEN = $ - drv_s_envpath

; ---- messages this file can emit -------------------------------------------
drv_m_nosub    db 'a subcommand is required (spec 12); there is no bare form',0
drv_m_unkcmd   db 'unknown subcommand: ',0
drv_m_unkopt   db 'unknown option: ',0
drv_m_needval  db 'option requires a value: ',0
drv_m_repeat   db 'given more than once, which would make argument order observable: ',0
drv_m_twosrc   db 'more than one SOURCE; the second is: ',0
drv_m_envform  db '--env: expected KEY=VALUE, got: ',0
drv_m_envkey   db '--env: KEY does not spell an identifier (spec 8.2): ',0
drv_m_envdup   db '--env: a repeated KEY is an error, not a last-wins (spec 9.3): ',0
drv_m_epoch    db '--epoch: expected decimal seconds since the Unix epoch, got: ',0
drv_m_diagn    db '--diagnostica: expected textus or json, got: ',0
drv_m_emitte   db '--emitte: expected tokens, cst or ast, got: ',0

; ---- small predicates ------------------------------------------------------

; drv_arg_is(ptr, len, lit) -> eax = 1 if the `len` bytes at `ptr` are
; byte-equal to the NUL-terminated literal at `lit`, else 0. Cannot fail;
; leaves CF clear.
;
; WHY THIS IS NOT `str_eq`. rt/str.inc has exactly this routine and it is
; UNREACHABLE FROM THE BUILD: nothing in this tree `include`s it. The chain
; that exists is lexer/lexer.inc -> ... -> diag/fix.inc -> rt/span.inc ->
; intern -> map -> vec -> arena -> sys, and rt/str.inc (and rt/sort.inc) sit
; outside it. Pulling rt/str.inc in from here does not fix that: its own
; first line is `include 'arena.inc'`, which the chain above has already
; processed, and a second pass over a file that emits a `struct` and four
; `proc`s fails outright (rt/vec.inc's header records the same diamond).
; Wiring those two modules into the chain is rt/'s change to make, not
; driver/'s to force -- REPORTED, not done. Twelve lines of byte comparison
; are the cheaper of the two wrongs, and this one is visible.
proc drv_arg_is, ptr, len, lit
	locals
	endl
	mov	rsi, [ptr]
	mov	rdi, [lit]
	mov	rcx, [len]
	xor	rdx, rdx
  .loop:
	cmp	rdx, rcx
	jae	.arg_end
	movzx	eax, byte [rdi + rdx]
	test	eax, eax
	jz	.no			; the literal ran out first
	movzx	r8d, byte [rsi + rdx]
	cmp	eax, r8d
	jne	.no
	inc	rdx
	jmp	.loop
  .arg_end:
	cmp	byte [rdi + rdx], 0	; ... and they ran out together
	jne	.no
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; drv_cmd_lookup(ptr, len) -> eax = the DRV_CMD_* index, or DRV_CMD_NONE.
; Linear over ten entries in §12's order; nothing here hashes or sorts.
; Cannot fail; leaves CF clear.
proc drv_cmd_lookup, ptr, len
	uses	rbx, r12
	locals
	endl
	xor	rbx, rbx
  .loop:
	cmp	rbx, DRV_CMD_COUNT
	jge	.miss
	mov	r12, [drv_cmd_tab + rbx*8]
	mov	rdi, [ptr]
	mov	rsi, [len]
	mov	rdx, r12
	call	drv_arg_is
	test	eax, eax
	jnz	.hit
	inc	rbx
	jmp	.loop
  .hit:
	mov	eax, ebx
	return
  .miss:
	mov	eax, DRV_CMD_NONE
	return
endp

; drv_u64_parse(ptr, len, n) -> eax = 1 and [n] = the value if the `len`
; bytes at `ptr` are a non-empty run of ASCII digits that fits in 64 bits;
; eax = 0 and [n] untouched otherwise. A predicate, deliberately -- not the
; CF/eax channel, which docs/asm-conventions.md reserves for an EXS-E code or
; a kernel errno, and a malformed flag is neither (§9.3).
;
; No sign, no underscores, no whitespace, no 0x. §9.3 says "decimal seconds"
; and this reads exactly that. Leading zeros are accepted ("007" is 7);
; rejecting them would be a rule the spec does not state.
proc drv_u64_parse, ptr, len, n
	uses	rbx, r12, r13
	locals
	endl
	mov	r12, [ptr]
	mov	r13, [len]
	test	r13, r13
	jz	.no
	xor	rax, rax		; accumulator
	xor	rbx, rbx		; index
  .loop:
	cmp	rbx, r13
	jae	.ok
	movzx	ecx, byte [r12 + rbx]
	sub	ecx, '0'
	cmp	ecx, 9
	ja	.no			; unsigned, so a byte below '0' wraps high
	; rax = rax*10 + ecx, refusing to wrap. `mul` gives the full 128-bit
	; product, so a nonzero rdx IS the overflow -- there is nothing to
	; reason about.
	;
	; THE OBVIOUS SHIFT VERSION IS WRONG AND THIS IS WHY IT IS NOT USED.
	; `mov rdx,rax / shl rax,1 / jc / shl rdx,3 / jc / add rax,rdx / jc`
	; looks equivalent and is not: `shl r, 3` leaves CF holding only the
	; LAST bit shifted out -- bit 61 -- so an accumulator whose bit 62 is
	; set and whose bits 63 and 61 are clear shifts a 1 out and reports no
	; carry. Concretely, `--epoch 46116860184273879040` (2^62 * 10, well
	; past 2^64-1) was ACCEPTED by that version and wrapped to
	; 9223372036854775808, while `--epoch 99999999999999999999` was
	; correctly refused -- so the bug was invisible to the obvious test.
	; Found by running the built compiler, not by reading this loop.
	mov	r8, 10
	mul	r8			; rdx:rax = rax * 10
	test	rdx, rdx
	jnz	.no
	add	rax, rcx
	jc	.no
	inc	rbx
	jmp	.loop
  .ok:
	mov	rdx, [n]
	mov	[rdx], rax
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; drv_ident_ok(ptr, len) -> eax = 1 if the `len` bytes at `ptr` spell exactly
; one §8.2 identifier and nothing else, else 0. See this file's header for
; why this runs the real lexer instead of re-deriving UAX #31.
;
; Uses `r15`'s scratch arena and its own throwaway token and diagnostic
; vectors, reset on every call, so a rejected key leaves no diagnostic behind
; to be printed later as if it were about the source. Cannot fail; leaves CF
; clear.
proc drv_ident_ok, ptr, len
	uses	rbx, r12, r13
	locals
	endl
	mov	r12, [ptr]
	mov	r13, [len]
	test	r13, r13
	jz	.no

	mov	rdi, [r15 + DrvCtx.scratch]
	call	arena_reset

	mov	rdi, [r15 + DrvCtx.vtoks]
	mov	rsi, [r15 + DrvCtx.scratch]
	mov	rdx, sizeof.Tok
	mov	rcx, 8
	call	vec_init
	mov	rdi, [r15 + DrvCtx.vdiags]
	mov	rsi, [r15 + DrvCtx.scratch]
	mov	rdx, sizeof.Diag
	mov	rcx, 4
	call	vec_init

	mov	rdi, [r15 + DrvCtx.vlx]
	mov	rsi, [r15 + DrvCtx.scratch]
	mov	rdx, [r15 + DrvCtx.interner]
	mov	rcx, [r15 + DrvCtx.vtoks]
	mov	r8,  [r15 + DrvCtx.vdiags]
	call	lex_init

	mov	rdi, [r15 + DrvCtx.vlx]
	mov	rsi, r12
	mov	rdx, r13
	lea	rcx, [drv_s_envpath]
	mov	r8,  DRV_S_ENVPATH_LEN
	call	lex_set_source

	mov	rdi, [r15 + DrvCtx.vlx]
	call	lex_run
	jc	.no			; §8.1 gate failure: no token stream at all
	; lex_run's CF is the §8.1 gate only (lex.inc, "§8.1 IS A GATE, NOT A
	; PHASE"); a §8.2/§8.4 diagnostic -- mixed script, a reserved word --
	; leaves CF clear and a complete token stream. This routine's promise
	; is "one identifier and NO diagnostic", so it reads the count as well.
	; It relied on an over-broad CF until the lexer fixed it, and
	; tests/unit/driver_env_epoch.asm is what noticed.
	mov	rdi, [r15 + DrvCtx.vlx]
	cmp	dword [rdi + Lexer.ndiag], 0
	jne	.no

	; Exactly two tokens: the identifier, then TOK_EOF. Anything else --
	; two words, a trailing punctuation, an empty run -- is not "an
	; identifier and nothing else".
	mov	rbx, [r15 + DrvCtx.vtoks]
	cmp	qword [rbx + Vec.len], 2
	jne	.no

	mov	rdi, rbx
	mov	rsi, 0
	call	vec_get
	mov	ecx, [rax + Tok.kind]
	cmp	ecx, TOK_IDENT
	je	.kindok
	cmp	ecx, TOK_KEYWORD	; see this file's header
	jne	.no
  .kindok:
	cmp	dword [rax + Tok.span.start], 0
	jne	.no
	mov	ecx, [rax + Tok.span.len]
	cmp	rcx, r13
	jne	.no

	mov	rdi, rbx
	mov	rsi, 1
	call	vec_get
	cmp	dword [rax + Tok.kind], TOK_EOF
	jne	.no

	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; drv_env_add(ptr, len) -> eax = 0 on success, else the DRV_EXIT_* status to
; leave with (having already said why). `ptr`/`len` is one whole `--env`
; argument value, i.e. `KEY=VALUE`.
;
; KEY is the bytes before the FIRST `=`; VALUE is *"the rest of the argument
; verbatim, including `=` and spaces"* (§9.3), which is why the split is on
; the first one and not the last, and why VALUE is never re-scanned.
;
; The binding is stored in an insertion-ordered `Map` (CLAUDE.md: *"Maps
; iterate in insertion order"*) keyed on the KEY bytes, which are borrowed
; straight out of argv -- argv outlives the process, so nothing is copied.
; The stored value is a two-word record so a future consumer gets VALUE's
; length as well as its address; a pointer is data here, never an ordering
; key, so §9.3's determinism rule is untouched.
proc drv_env_add, ptr, len
	uses	rbx, r12, r13, r14
	locals
		slot rec, dq
	endl
	mov	r12, [ptr]
	mov	r13, [len]

	xor	rbx, rbx
  .find:
	cmp	rbx, r13
	jae	.noeq
	cmp	byte [r12 + rbx], '='
	je	.split
	inc	rbx
	jmp	.find
  .noeq:
	lea	rdi, [drv_m_envform]
	mov	rsi, r12
	mov	rdx, r13
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_USAGE
	return

  .split:
	; rbx = KEY length, r12 = KEY pointer.
	mov	rdi, r12
	mov	rsi, rbx
	call	drv_ident_ok
	test	eax, eax
	jnz	.keyok
	lea	rdi, [drv_m_envkey]
	mov	rsi, r12
	mov	rdx, rbx
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_USAGE
	return

  .keyok:
	mov	rdi, [r15 + DrvCtx.envmap]
	mov	rsi, r12
	mov	rdx, rbx
	call	map_get
	test	rax, rax
	jz	.fresh
	lea	rdi, [drv_m_envdup]
	mov	rsi, r12
	mov	rdx, rbx
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_USAGE
	return

  .fresh:
	mov	rdi, [r15 + DrvCtx.iarena]
	mov	rsi, 16
	call	arena_alloc
	mov	[rec], rax
	lea	r14, [r12 + rbx + 1]	; VALUE begins one byte past the '='
	mov	[rax], r14
	mov	rcx, r13
	sub	rcx, rbx
	dec	rcx			; VALUE length; 0 is legal
	mov	[rax + 8], rcx

	mov	rdi, [r15 + DrvCtx.envmap]
	mov	rsi, r12
	mov	rdx, rbx
	mov	rcx, [rec]
	call	map_insert
	inc	dword [r15 + DrvCtx.nenv]
	xor	eax, eax
	return
endp

; ---- the parser ------------------------------------------------------------

; drv_ctx_reset -- zero exactly the `DrvCtx` fields `drv_parse` writes, and
; set the defaults §9.3 and this driver promise: `--epoch` 0, diagnostics in
; text form, nothing else given.
;
; `drv_parse` needs those fields clear before it can detect a REPEATED
; option, and in the real process they are clear because they live in BSS.
; Relying on that would make the precondition invisible and would make
; `drv_parse` untestable more than once per process -- a fixture calling it
; twice would see the first command line's `--hospes` as the second's
; repeat. Stated as a routine instead. Cannot fail; leaves CF clear.
proc drv_ctx_reset
	locals
	endl
	mov	dword [r15 + DrvCtx.diagmode], DIAG_MODE_TEXT
	mov	dword [r15 + DrvCtx.dmset],    0
	mov	dword [r15 + DrvCtx.epochset], 0
	mov	dword [r15 + DrvCtx.nenv],     0
	mov	dword [r15 + DrvCtx.emitte],    DRV_EMIT_NONE
	mov	dword [r15 + DrvCtx.emitteset], 0
	mov	qword [r15 + DrvCtx.epoch],    0
	mov	qword [r15 + DrvCtx.hospes],   0
	mov	qword [r15 + DrvCtx.hospesn],  0
	mov	qword [r15 + DrvCtx.srcpath],  0
	mov	qword [r15 + DrvCtx.srcpathn], 0
	mov	qword [r15 + DrvCtx.outpath],  0
	mov	qword [r15 + DrvCtx.outpathn], 0
	return
endp

; drv_parse(argc, argv) -> eax = 0 if the command line was accepted, else the
; DRV_EXIT_* status to leave with -- a message has already been written in
; that case. Fills the `DrvCtx` at `r15`; `DrvCtx.cmd` is already set by
; drv_main, so this routine only ever sees `aedifica`'s options.
;
; One left-to-right pass. Anything beginning with `-` and longer than one
; byte is an option; anything else is the positional SOURCE, of which there
; is exactly one. There is no `--` end-of-options marker and no `--opt=value`
; form: §12 spells `--hospes TRIPLE` and `-o OUT` with a space, and one
; spelling per option is one thing to test. `[OPEN]`: a SOURCE whose name
; begins with `-` is therefore unreachable.
proc drv_parse, argc, argv
	uses	rbx, r12, r13, r14
	locals
		slot an,   dq		; current argument's length
		slot vp,   dq		; the value argument's pointer
		slot vn,   dq		; ... and its length
		slot num,  dq
	endl
	mov	r12, [argv]
	mov	r13, [argc]
	mov	rbx, 2			; argv[0] is the program, argv[1] the subcommand

  .loop:
	cmp	rbx, r13
	jge	.finished
	mov	r14, [r12 + rbx*8]
	mov	rdi, r14
	call	drv_cstrlen
	mov	[an], rax

	cmp	qword [an], 1
	jle	.positional		; "" and "-" are not options
	cmp	byte [r14], '-'
	jne	.positional

	; ---- --hospes TRIPLE -----------------------------------------------
	mov	rdi, r14
	mov	rsi, [an]
	lea	rdx, [drv_o_hospes]
	call	drv_arg_is
	test	eax, eax
	jz	.not_hospes
	call	.take_value
	test	eax, eax
	jnz	.leave
	cmp	qword [r15 + DrvCtx.hospes], 0
	jne	.repeated
	mov	rax, [vp]
	mov	[r15 + DrvCtx.hospes], rax
	mov	rax, [vn]
	mov	[r15 + DrvCtx.hospesn], rax
	jmp	.next
  .not_hospes:

	; ---- -o OUT --------------------------------------------------------
	mov	rdi, r14
	mov	rsi, [an]
	lea	rdx, [drv_o_out]
	call	drv_arg_is
	test	eax, eax
	jz	.not_out
	call	.take_value
	test	eax, eax
	jnz	.leave
	cmp	qword [r15 + DrvCtx.outpath], 0
	jne	.repeated
	mov	rax, [vp]
	mov	[r15 + DrvCtx.outpath], rax
	mov	rax, [vn]
	mov	[r15 + DrvCtx.outpathn], rax
	jmp	.next
  .not_out:

	; ---- --env KEY=VALUE -----------------------------------------------
	mov	rdi, r14
	mov	rsi, [an]
	lea	rdx, [drv_o_env]
	call	drv_arg_is
	test	eax, eax
	jz	.not_env
	call	.take_value
	test	eax, eax
	jnz	.leave
	mov	rdi, [vp]
	mov	rsi, [vn]
	call	drv_env_add
	test	eax, eax
	jnz	.leave
	jmp	.next
  .not_env:

	; ---- --epoch N -----------------------------------------------------
	mov	rdi, r14
	mov	rsi, [an]
	lea	rdx, [drv_o_epoch]
	call	drv_arg_is
	test	eax, eax
	jz	.not_epoch
	call	.take_value
	test	eax, eax
	jnz	.leave
	cmp	dword [r15 + DrvCtx.epochset], 0
	jne	.repeated
	mov	rdi, [vp]
	mov	rsi, [vn]
	lea	rdx, [num]
	call	drv_u64_parse
	test	eax, eax
	jz	.bad_epoch
	mov	rax, [num]
	mov	[r15 + DrvCtx.epoch], rax
	mov	dword [r15 + DrvCtx.epochset], 1
	jmp	.next
  .not_epoch:

	; ---- --diagnostica textus|json -------------------------------------
	mov	rdi, r14
	mov	rsi, [an]
	lea	rdx, [drv_o_diagn]
	call	drv_arg_is
	test	eax, eax
	jz	.not_diagn
	call	.take_value
	test	eax, eax
	jnz	.leave
	cmp	dword [r15 + DrvCtx.dmset], 0
	jne	.repeated
	mov	rdi, [vp]
	mov	rsi, [vn]
	lea	rdx, [drv_v_textus]
	call	drv_arg_is
	test	eax, eax
	jz	.try_json
	mov	dword [r15 + DrvCtx.diagmode], DIAG_MODE_TEXT
	mov	dword [r15 + DrvCtx.dmset], 1
	jmp	.next
  .try_json:
	mov	rdi, [vp]
	mov	rsi, [vn]
	lea	rdx, [drv_v_json]
	call	drv_arg_is
	test	eax, eax
	jz	.bad_diagn
	mov	dword [r15 + DrvCtx.diagmode], DIAG_MODE_JSON
	mov	dword [r15 + DrvCtx.dmset], 1
	jmp	.next
  .not_diagn:

	; ---- --emitte tokens|cst|ast -- spec 18.2's stage-dump mitigation ---
	mov	rdi, r14
	mov	rsi, [an]
	lea	rdx, [drv_o_emitte]
	call	drv_arg_is
	test	eax, eax
	jz	.unknown
	call	.take_value
	test	eax, eax
	jnz	.leave
	cmp	dword [r15 + DrvCtx.emitteset], 0
	jne	.repeated
	mov	rdi, [vp]
	mov	rsi, [vn]
	lea	rdx, [drv_v_tokens]
	call	drv_arg_is
	test	eax, eax
	jz	.try_cst
	mov	dword [r15 + DrvCtx.emitte], DRV_EMIT_TOKENS
	mov	dword [r15 + DrvCtx.emitteset], 1
	jmp	.next
  .try_cst:
	mov	rdi, [vp]
	mov	rsi, [vn]
	lea	rdx, [drv_v_cst]
	call	drv_arg_is
	test	eax, eax
	jz	.try_ast
	mov	dword [r15 + DrvCtx.emitte], DRV_EMIT_CST
	mov	dword [r15 + DrvCtx.emitteset], 1
	jmp	.next
  .try_ast:
	mov	rdi, [vp]
	mov	rsi, [vn]
	lea	rdx, [drv_v_ast]
	call	drv_arg_is
	test	eax, eax
	jz	.bad_emitte
	mov	dword [r15 + DrvCtx.emitte], DRV_EMIT_AST
	mov	dword [r15 + DrvCtx.emitteset], 1
	jmp	.next


  .positional:
	cmp	qword [r15 + DrvCtx.srcpath], 0
	jne	.two_sources
	mov	[r15 + DrvCtx.srcpath], r14
	mov	rax, [an]
	mov	[r15 + DrvCtx.srcpathn], rax
  .next:
	inc	rbx
	jmp	.loop

  .finished:
	xor	eax, eax
	return

  ; ---- the refusals --------------------------------------------------------
  .unknown:
	lea	rdi, [drv_m_unkopt]
	mov	rsi, r14
	mov	rdx, [an]
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_USAGE
	return
  .repeated:
	lea	rdi, [drv_m_repeat]
	mov	rsi, r14
	mov	rdx, [an]
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_USAGE
	return
  .two_sources:
	lea	rdi, [drv_m_twosrc]
	mov	rsi, r14
	mov	rdx, [an]
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_USAGE
	return
  .bad_epoch:
	lea	rdi, [drv_m_epoch]
	mov	rsi, [vp]
	mov	rdx, [vn]
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_USAGE
	return
  .bad_diagn:
	lea	rdi, [drv_m_diagn]
	mov	rsi, [vp]
	mov	rdx, [vn]
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_USAGE
	return
  .bad_emitte:
	lea	rdi, [drv_m_emitte]
	mov	rsi, [vp]
	mov	rdx, [vn]
	xor	rcx, rcx
	call	drv_msg
	mov	eax, DRV_EXIT_USAGE
	return
  .leave:
	return			; eax already carries the status

  ; ---- the value fetch -----------------------------------------------------
  ; A LOCAL SUBROUTINE OF THIS PROC, not a `proc` of its own, and reached
  ; only by `call .take_value` from inside this body: it reads rbx
  ; (the option's index), r12/r13 (argv/argc) and r14 (the option itself)
  ; and writes the `vp`/`vn` slots of THIS frame, all of which are this
  ; routine's own registers and stack. Making it a `proc` would mean
  ; passing five things and getting two back. It advances rbx past the
  ; value, so the caller's `.next` lands on the argument after it.
  ;
  ; ITS NAME MUST STAY LOCAL (`.take_value`, with the dot). A non-local
  ; label here resets fasmg's local-label scope, and `endp` would then emit
  ; the epilogue as `<thatname>.__proc_epilogue` while every `return` above
  ; it still refers to `drv_parse.__proc_epilogue` -- an undefined symbol
  ; far from the cause. Read straight off `endp` in macros/proc.inc, which
  ; emits that label bare, and confirmed by assembling both spellings:
  ; tests/unit/driver_cli.asm's header records the exact error the non-local
  ; one produces.
  ;
  ; The `push rbx`/`pop rbx` pairs below are for STACK ALIGNMENT, not for
  ; rbx: `call .take_value` leaves rsp 8 off the 16-byte boundary SysV
  ; requires immediately before a call (docs/asm-conventions.md, "2.
  ; Register discipline"), and one push puts it back.
  ;
  ; -> eax = 0 with vp/vn filled, or a DRV_EXIT_* status having already
  ; reported the missing value -- the same 0-means-accepted convention
  ; `drv_env_add` and `drv_parse` itself use.
  .take_value:
	lea	rax, [rbx + 1]
	cmp	rax, r13
	jge	.no_value
	mov	rbx, rax
	mov	rax, [r12 + rbx*8]
	mov	[vp], rax
	push	rbx
	mov	rdi, rax
	call	drv_cstrlen
	pop	rbx
	mov	[vn], rax
	xor	eax, eax
	ret
  .no_value:
	push	rbx
	lea	rdi, [drv_m_needval]
	mov	rsi, r14
	mov	rdx, [an]
	xor	rcx, rcx
	call	drv_msg
	pop	rbx
	mov	eax, DRV_EXIT_USAGE
	ret
endp
