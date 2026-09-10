; compiler/x86_64/exsc.asm
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
; `exsc`. The whole compiler, as one fasmg translation unit: there is no link
; step, so this file is the only entry point and everything else arrives by
; `include` (docs/asm-conventions.md, "7. How to add a module", step 3;
; spec §18.1's "build closure is exactly one tool").
;
; WHAT IT CAN DO TODAY, so that nobody has to run it to find out: it reads a
; source file, applies §8.1, tokenizes it under §8.4, PARSES it under §8.6
; into the lossless CST of §9.1, builds the typed AST of §9.1 Stage 1 from
; that CST, and reports every diagnostic either half raised, in a human format
; or JSON. That is the whole of §16 Stage 1. There is no type checker and no
; backend (Stages 2 and 3), so `exsc aedifica ... -o OUT` finds out whether
; OUT *could* be built and then says it cannot build it. Nine of the ten
; subcommands §12 names are refusals that name themselves.
;
;	exsc aedifica --hospes TRIPLE SOURCE [-o OUT]
;	              [--env KEY=VALUE]... [--epoch N]
;	              [--diagnostica textus|json]
;
; Exit: 0 clean, 1 diagnostics, 2 usage, 3 host/internal, 4 unimplemented.
; 132 is `rassert`'s SIGILL trap and means a compiler bug. driver/msg.inc
; carries the full table; `exsc` with no arguments prints it.
;
; THE ORDER OF THE SECTIONS BELOW IS LOAD-BEARING, and each one is a
; rule someone else's header already paid for:
;
;   1. `include 'format/format.inc'` then `format ELF64 executable 3` then
;      `entry start`, BEFORE any rt/ file. rt/sys.inc's header: a `proc`
;      emits real instructions the moment fasmg processes it, and those need
;      the x86 mnemonic set that `format ELF64 executable 3` loads. The
;      other order fails with "symbol 'x86.mode' is undefined or out of
;      scope" at the first `push`.
;   2. `segment readable executable` and `start:` BEFORE the includes, so
;      that the entry point is the FIRST thing in the one executable
;      segment and every routine follows it. This is not style: it is what
;      makes `make audit` strong. tools/syscall-audit.sh anchors its linear
;      sweep at the entry point ("the one address the CPU is guaranteed to
;      treat as an instruction boundary") and warns that any OTHER
;      executable segment is swept from its own start "with no such
;      guarantee". With the includes first, this file produced TWO
;      executable segments and the audit reported, verbatim,
;      `Scanning executable segment 0x400000 (size 24092) from its own
;      start [WARNING: does not contain the entry point -- linear-sweep
;      alignment from here is NOT guaranteed]` over 24 KB of compiler and
;      166 bytes of entry stub. Moving `start:` ahead of them yields one
;      segment, `anchored at entry point 0x4000e8 [alignment reliable]`.
;      Measured both ways; the messages above are copied from the two runs.
;
;   3. cst/cst.inc, then ast/ast.inc, then driver/driver.inc -- and
;      lexer/lexer.inc is NOT included here at all. cst/cst.inc includes it
;      itself (see that file's header), and fasmg has one flat namespace, so
;      including both would process every lexer `proc` twice and fail on the
;      second. The include graph is a strict CHAIN: cst/ brings lexer/, diag/
;      and rt/; ast/ and driver/ include none of those and rely on cst/ having
;      run first.
;   4. compiler/shared/unicode/tables/tables.inc EXACTLY ONCE, in a data
;      segment of this file's choosing. lexer/lexer.inc's header is explicit
;      that it does not include it: it emits ~120 KB of `file` data and
;      processing it twice is fatal, and only the consumer knows which
;      segment the blobs belong in.
;   5. The writable segment holds every mutable object in this process.
;      Nothing under compiler/x86_64/ has module-level mutable state --
;      lexer/source.inc's header states that rule and applies §4.1's own
;      `EXS-E0500` to the compiler itself -- so the storage lives here, in
;      the entry file, and every routine reaches it through `r15` or through
;      an argument.
;
; THE r15 PIN IS ESTABLISHED IN THREE INSTRUCTIONS AT `start`, AND NOWHERE
; ELSE. docs/asm-conventions.md, "1.2 The r15 pin": *"established once, at
; process entry, before control reaches any internal routine."* Nothing in
; rt/, diag/, lexer/ or compiler/shared/unicode/ reads or writes r15, and no
; `uses` list in this tree names it, so the pointer survives every call by
; stock SysV's callee-saved rule with no save/restore code anywhere.
;
; NOTHING HERE READS envp. At entry `rsp` points at argc, then argc argv
; pointers, then a NULL, then the environment. `drv_main` gets argc and
; `&argv[0]` and indexes strictly below argc. §9.3: *"Reads no environment
; variables. `--env KEY=VALUE` supplies values on the command line
; instead."* There is no `getenv` in this binary to disable -- there is no
; libc in it.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §8.1, §8.3, §9.3, §9.5, §12, §18.1.
; -----------------------------------------------------------------------------

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	; ---- the r15 pin, established once, before any internal call --------
	lea	r15, [drv_ctx]

	; ---- bind the context to this process's storage ----------------------
	; Pointers only, and only here: driver/ allocates nothing statically,
	; so this is the single place the compiler's mutable objects are named.
	; Written out one field per line, in declaration order, rather than
	; looped over a table -- there is no order here for anything to depend
	; on (CLAUDE.md, "Determinism is not optional").
	lea	rax, [drv_arena]
	mov	[r15 + DrvCtx.arena], rax
	lea	rax, [drv_iarena]
	mov	[r15 + DrvCtx.iarena], rax
	lea	rax, [drv_scratch]
	mov	[r15 + DrvCtx.scratch], rax
	lea	rax, [drv_interner]
	mov	[r15 + DrvCtx.interner], rax
	lea	rax, [drv_envmap]
	mov	[r15 + DrvCtx.envmap], rax
	lea	rax, [drv_vlx]
	mov	[r15 + DrvCtx.vlx], rax
	lea	rax, [drv_vtoks]
	mov	[r15 + DrvCtx.vtoks], rax
	lea	rax, [drv_vdiags]
	mov	[r15 + DrvCtx.vdiags], rax
	lea	rax, [drv_lx]
	mov	[r15 + DrvCtx.lx], rax
	lea	rax, [drv_toks]
	mov	[r15 + DrvCtx.toks], rax
	lea	rax, [drv_diags]
	mov	[r15 + DrvCtx.diags], rax
	lea	rax, [drv_green]
	mov	[r15 + DrvCtx.green], rax
	lea	rax, [drv_work]
	mov	[r15 + DrvCtx.work], rax
	lea	rax, [drv_cmap]
	mov	[r15 + DrvCtx.cmap], rax
	lea	rax, [drv_ctree]
	mov	[r15 + DrvCtx.ctree], rax
	lea	rax, [drv_parser]
	mov	[r15 + DrvCtx.parser], rax
	lea	rax, [drv_ast]
	mov	[r15 + DrvCtx.ast], rax

	; ---- argc, argv ------------------------------------------------------
	; rsp is 16-byte aligned at process entry and neither instruction below
	; moves it, so the `call` sees the alignment SysV requires
	; (docs/asm-conventions.md, "2. Register discipline").
	mov	rdi, [rsp]		; argc
	lea	rsi, [rsp + 8]		; &argv[0] -- envp is past argv[argc]
					; and is never reached (§9.3)
	call	drv_main

	mov	edi, eax
	call	sys_exit_group

	; Unreachable: exit_group does not return. If it ever does, that is an
	; internal contract violation and it traps rather than falling into
	; whatever bytes follow -- the same raw `ud2` encoding macros/assert.inc
	; uses, since vendor/fasmg-x86 has no symbolic mnemonic for it.
	db	0x0F, 0x0B

include 'cst/cst.inc'
include 'ast/ast.inc'
include 'driver/driver.inc'

segment readable
  ; ~120 KB of UCD-derived blobs, included exactly once -- see note 4 above
  ; and compiler/shared/unicode/README.md.
  include '../shared/unicode/tables/tables.inc'

segment readable writeable
  ; Every mutable object in the process, and all of it reached through r15.
  ; `rb sizeof.X` is BSS: fasmg reserves it without putting bytes in the
  ; file, so a struct that grows costs address space, not binary size.
  drv_ctx	rb sizeof.DrvCtx
  drv_arena	rb sizeof.Arena	; the compilation -- sized in driver/io.inc
  drv_iarena	rb sizeof.Arena	; interner + env map -- never reset
  drv_scratch	rb sizeof.Arena	; reset once per --env key
  drv_interner	rb sizeof.Interner
  drv_envmap	rb sizeof.Map
  drv_vlx	rb sizeof.Lexer	; }
  drv_vtoks	rb sizeof.Vec	; } --env key validation only
  drv_vdiags	rb sizeof.Vec	; }
  drv_lx	rb sizeof.Lexer	; }
  drv_toks	rb sizeof.Vec	; } the compilation
  drv_diags	rb sizeof.Vec	; }
  drv_green	rb sizeof.Vec		; }
  drv_work	rb sizeof.Vec		; } the §9.1 CST -- cst/green.inc
  drv_cmap	rb sizeof.Map		; } owns none of its own storage
  drv_ctree	rb sizeof.CstTree	; }
  drv_parser	rb sizeof.CstParser	; the §8.6 parse
  drv_ast	rb sizeof.Ast		; the §9.1 Stage 1 typed tree
