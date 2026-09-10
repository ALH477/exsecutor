; compiler/x86_64/backend_fasmg/program.inc
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
; `OUT`: the whole fasmg source file a compiled Exsecutor program is, built
; around `bfa_emit_module`'s function text. docs/design/runtime.md section
; 2.1 lays out every line of it and section 4 audits what each line is a
; function of; this file is that layout, emitted.
;
;   ; exsecutor: reference backend          banner -- no version, no path, no time
;   include 'format/format.inc'            the one external include (spec 18.1's
;                                          vendored package; spec 12 says exsc
;                                          never assembles OUT itself)
;   format ELF64 executable 3
;   entry exsrt_start
;   EXS_POTESTAS_MUNDUS     = 1            eleven constants, spec 4.6 order,
;   ...                                    from `potestates`, one bit each
;   EXS_MXCSR               = 0x1F80       runtime.md 2.7's image
;   segment readable executable
;   <prelude.asm, byte for byte>           exsrt_start first: see below
;   <bfa_emit_module's text>               the module's functions, then bfausr_trap
;   segment readable
;   bfausr_g0: db 0x41,...                 literals, global id order, HEX ONLY
;   segment readable writeable
;   <prelude_data.asm, byte for byte>
;
; WHY THE BLOBS ARE CARRIED, NOT PATH-REFERENCED. Spec 12 makes `OUT` text
; that `exsc` never assembles, and nothing but `vendor/fasmg-x86/` is on the
; include path -- so `OUT` cannot say `include '.../prelude.asm'` and expect
; a user's `fasmg` to find it. runtime.md constraint 1 draws exactly that
; conclusion: the prelude is "bytes that `exsc` writes INTO `OUT`". So this
; file carries both blobs with fasmg's `file` directive at COMPILER assembly
; time -- the same idiom compiler/shared/unicode/tables/tables.inc uses for
; the UCD blobs -- and copies them out verbatim. `OUT` is then
; self-contained modulo the vendored macro package, and the largest single
; component of it is a function of `exsc`'s own bytes and of nothing else
; (spec 9.3 by construction rather than by discipline).
;
; The two blobs are DATA in `exsc`'s executable segment, `jmp`-guarded like
; every other inline literal in this tree. They contain no 0x0F byte, so
; `tools/syscall-audit.sh`'s linear sweep cannot decode a `syscall` (0F 05)
; out of them; they are ASCII text and `exsc`'s own nine syscalls are
; unchanged by their presence.
;
; WHY THE PRELUDE LEADS AND THE MODULE FOLLOWS. exsc.asm's own header
; records, from two measured runs, that the audit's linear sweep is reliable
; only from the entry point and that code before it is swept "with no such
; guarantee". `entry exsrt_start` and `exsrt_start` is the blob's first
; routine, so the executable segment starts at the entry point and the sweep
; over a compiled program is anchored the same way it is over `exsc`.
;
; WHY LITERALS ARE HEX `db` AND NEVER QUOTED. A fasmg string cannot hold a
; raw newline and `examples/saluta.exsc`'s literal holds six (runtime.md
; 2.1). Hex is also the only form with no escaping question at all, which
; is the same reason spec 8.3 keeps format strings out of diagnostics.
;
; DETERMINISM (runtime.md 4). Every byte here is a function of: the module
; (function order = declaration order; global order = id order; literal
; bytes = Module.gdata), the `potestates` bitmask, the MXCSR image, and
; `exsc`'s own bytes. No path, no clock, no host name, no pointer, no hash
; bucket. `bfa_emit_program` reads nothing else and this file opens no file
; at run time.
;
; [OPEN] NON-ASCII LABEL MANGLING. runtime.md section 6 lists it as this
; file's job and section 4 leaves it `[OPEN]`; it is NOT implemented. A
; source identifier outside ASCII reaches `OUT` unmangled and whether fasmg
; accepts it is untested. The hello world's names are ASCII. Recorded here
; rather than half-done.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md 4.6, 4.7, 9.2, 9.3, 12, 18.1.
; Design: docs/design/runtime.md 2.1, 2.2, 2.7, 4, 5, 6.
; -----------------------------------------------------------------------------

include 'emit.inc'

; ---- the eleven capability atoms, spec 4.6 order, as bit positions -------
; A caller builds `potestates` from these rather than counting bits; the
; checker's computed closure of `initium` is what fills it (spec 10.3), and
; until a checker exists a caller states it directly.
BFA_POTESTAS_MUNDUS      = 1 shl 0
BFA_POTESTAS_ALLOC       = 1 shl 1
BFA_POTESTAS_SERMO       = 1 shl 2
BFA_POTESTAS_HOROLOGIUM  = 1 shl 3
BFA_POTESTAS_ARCHIVUM    = 1 shl 4
BFA_POTESTAS_RETE        = 1 shl 5
BFA_POTESTAS_FORTUNA     = 1 shl 6
BFA_POTESTAS_AMBITUS     = 1 shl 7
BFA_POTESTAS_FILUM       = 1 shl 8
BFA_POTESTAS_MACHINA     = 1 shl 9
BFA_POTESTAS_CRUDUM      = 1 shl 10
BFA_POTESTAS_COUNT       = 11

; The three MXCSR images runtime.md 2.7 defines, named so a caller does not
; retype a bit pattern. Toward-zero (RC = 11) has no spec 5.4 word
; (runtime.md finding 11) and therefore no name here.
BFA_MXCSR_AD_PAREM     = 0x1F80
BFA_MXCSR_AD_INFERIUS  = 0x3F80
BFA_MXCSR_AD_SUPERIUS  = 0x5F80

; ---- constant names, pre-padded to one fixed column ----------------------
; Twelve rows of 24 bytes: the name, space-padded to 23, then one space.
; The row width is a property of the longest name (EXS_POTESTAS_HOROLOGIUM,
; 23 characters) and is baked in rather than computed, so the emitted
; alignment cannot drift with a formatting decision made elsewhere.
BFA_PROG_NAMEW = 24
	jmp	__bfa_prog_names_end
__bfa_prog_names:
	db	"EXS_POTESTAS_MUNDUS     "
	db	"EXS_POTESTAS_ALLOC      "
	db	"EXS_POTESTAS_SERMO      "
	db	"EXS_POTESTAS_HOROLOGIUM "
	db	"EXS_POTESTAS_ARCHIVUM   "
	db	"EXS_POTESTAS_RETE       "
	db	"EXS_POTESTAS_FORTUNA    "
	db	"EXS_POTESTAS_AMBITUS    "
	db	"EXS_POTESTAS_FILUM      "
	db	"EXS_POTESTAS_MACHINA    "
	db	"EXS_POTESTAS_CRUDUM     "
	db	"EXS_MXCSR               "
__bfa_prog_names_end:

; ---- the two blobs, carried verbatim -------------------------------------
	jmp	__bfa_prelude_text_end
__bfa_prelude_text:
	file	'../prelude/prelude.asm'
__bfa_prelude_text_end:

	jmp	__bfa_prelude_data_end
__bfa_prelude_data:
	file	'../prelude/prelude_data.asm'
__bfa_prelude_data_end:

; __bfa_prog_hexnib(ctx, nib) -- one UPPERCASE hex digit. print.inc's own
; nibble printer emits lowercase for the IR's `data $N HEXBYTES` text; the
; emitted `db 0x41` lines use uppercase, matching every hand-written
; `tests/unit/prelude_*.asm` harness this output has to stay comparable
; with. Two spellings of one number, each fixed where it is read.
proc __bfa_prog_hexnib, ctx, nib
	locals
	endl
	mov	eax, [nib]
	cmp	eax, 10
	jl	.digit
	add	eax, 'A' - 10
	jmp	.emit
  .digit:
	add	eax, '0'
  .emit:
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_byte
	return
endp

; __bfa_prog_hexbyte(ctx, val) -- "0xAB".
proc __bfa_prog_hexbyte, ctx, val
	locals
	endl
	mov	rdi, [ctx]
	jmp	.after_pfx
  .lit_pfx db "0x"
  .after_pfx:
	lea	rsi, [.lit_pfx]
	mov	rdx, .after_pfx - .lit_pfx
	call	__bfa_out_lit
	mov	eax, [val]
	shr	eax, 4
	and	eax, 0xF
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_prog_hexnib
	mov	eax, [val]
	and	eax, 0xF
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_prog_hexnib
	return
endp

; __bfa_prog_constants(ctx, potestates, mxcsr) -- the twelve lines.
; A bit that is set prints `1` and a bit that is clear prints `0`; all
; eleven are always printed, in spec 4.6 order, because prelude.asm
; requires every one to be DEFINED and a missing one is a fasmg error at
; the top of the blob rather than a silently absent routine
; (prelude/README.md).
proc __bfa_prog_constants, ctx, potestates, mxcsr
	uses	rbx
	locals
		slot i, dd
	endl
	mov	dword [i], 0
  .loop:
	cmp	dword [i], BFA_POTESTAS_COUNT
	jge	.mxcsr
	mov	rdi, [ctx]
	mov	eax, [i]
	imul	eax, BFA_PROG_NAMEW
	lea	rsi, [__bfa_prog_names]
	add	rsi, rax
	mov	rdx, BFA_PROG_NAMEW
	call	__bfa_out_lit
	mov	rdi, [ctx]
	jmp	.after_eq
  .lit_eq db "= "
  .after_eq:
	lea	rsi, [.lit_eq]
	mov	rdx, .after_eq - .lit_eq
	call	__bfa_out_lit

	mov	ecx, [i]
	mov	rax, [potestates]
	shr	rax, cl
	and	eax, 1
	add	eax, '0'
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_out_byte
	mov	rdi, [ctx]
	mov	esi, 10
	call	__bfa_out_byte

	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.loop

  .mxcsr:
	mov	rdi, [ctx]
	mov	eax, BFA_POTESTAS_COUNT * BFA_PROG_NAMEW
	lea	rsi, [__bfa_prog_names]
	add	rsi, rax
	mov	rdx, BFA_PROG_NAMEW
	call	__bfa_out_lit
	mov	rdi, [ctx]
	jmp	.after_eq2
  .lit_eq2 db "= "
  .after_eq2:
	lea	rsi, [.lit_eq2]
	mov	rdx, .after_eq2 - .lit_eq2
	call	__bfa_out_lit

	; 0x then exactly four uppercase nibbles. An image above 0xFFFF is not
	; one runtime.md 2.7's table can produce and is refused rather than
	; truncated.
	mov	rax, [mxcsr]
	cmp	rax, 0xFFFF
	jbe	.imgok
	jmp	.after_msg
  .msg db "EXS_MXCSR image does not fit in 16 bits"
  .after_msg:
	lea	rdi, [.msg]
	mov	rsi, .after_msg - .msg
	call	__bfa_emit_die
  .imgok:
	mov	rdi, [ctx]
	jmp	.after_x
  .lit_x db "0x"
  .after_x:
	lea	rsi, [.lit_x]
	mov	rdx, .after_x - .lit_x
	call	__bfa_out_lit
	mov	dword [i], 3
  .nibloop:
	cmp	dword [i], 0
	jl	.nibdone
	mov	ecx, [i]
	shl	ecx, 2
	mov	rax, [mxcsr]
	shr	rax, cl
	and	eax, 0xF
	mov	rdi, [ctx]
	mov	esi, eax
	call	__bfa_prog_hexnib
	mov	eax, [i]
	dec	eax
	mov	[i], eax
	jmp	.nibloop
  .nibdone:
	mov	rdi, [ctx]
	mov	esi, 10
	call	__bfa_out_byte
	return
endp

; __bfa_prog_global(ctx, module, gptr2) -- one literal: its label, then its
; bytes as hex `db` lines, twelve to a line. `gptr2` rather than `gptr`
; because `gptr` is burned (docs/asm-conventions.md section 4.1).
BFA_PROG_HEXPERLINE = 12
proc __bfa_prog_global, ctx, module, gptr2
	uses	rbx
	locals
		slot i, dd
		slot n, dd
		slot off, dd
	endl
	mov	rbx, [gptr2]
	mov	rdi, [ctx]
	jmp	.after_lbl
  .lit_lbl db "bfausr_g"
  .after_lbl:
	lea	rsi, [.lit_lbl]
	mov	rdx, .after_lbl - .lit_lbl
	call	__bfa_out_lit
	mov	rdi, [ctx]
	mov	esi, [rbx + BfaGlobal.id]
	call	__bfa_out_u64
	mov	rdi, [ctx]
	jmp	.after_colon
  .lit_colon db ":", 10
  .after_colon:
	lea	rsi, [.lit_colon]
	mov	rdx, .after_colon - .lit_colon
	call	__bfa_out_lit

	mov	ecx, [rbx + BfaGlobal.data_len]
	mov	[n], ecx
	mov	ecx, [rbx + BfaGlobal.data_off]
	mov	[off], ecx
	mov	dword [i], 0
  .loop:
	mov	eax, [i]
	cmp	eax, [n]
	jge	.done
	; start of a line?
	mov	eax, [i]
	xor	edx, edx
	mov	ecx, BFA_PROG_HEXPERLINE
	div	ecx
	test	edx, edx
	jnz	.nonewline
	cmp	dword [i], 0
	je	.firstline
	mov	rdi, [ctx]
	mov	esi, 10
	call	__bfa_out_byte
  .firstline:
	mov	rdi, [ctx]
	jmp	.after_db
  .lit_db db "	db	"
  .after_db:
	lea	rsi, [.lit_db]
	mov	rdx, .after_db - .lit_db
	call	__bfa_out_lit
	jmp	.emitbyte
  .nonewline:
	mov	rdi, [ctx]
	mov	esi, ','
	call	__bfa_out_byte
  .emitbyte:
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.gdata]
	mov	eax, [off]
	add	eax, [i]
	mov	esi, eax
	call	vec_get
	movzx	ecx, byte [rax]
	mov	rdi, [ctx]
	mov	esi, ecx
	call	__bfa_prog_hexbyte

	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.loop
  .done:
	cmp	dword [n], 0
	je	.nobytes
	mov	rdi, [ctx]
	mov	esi, 10
	call	__bfa_out_byte
  .nobytes:
	return
endp

; bfa_emit_program(module, arena, potestates, mxcsr) -> rax = ptr,
; rdx = len -- the whole of `OUT`.
;
; `potestates` is an eleven-bit mask in spec 4.6 order (BFA_POTESTAS_*
; above), from the checker's computed capability closure of `initium`;
; `mxcsr` is runtime.md 2.7's image, from `initium`'s `Func.numeri`.
; Neither is derived here: this file has no ego reader and must not invent
; one (spec 9.3 -- nothing in `OUT` may come from anywhere but the input).
;
; The returned buffer lives in `arena` and is valid until that arena is
; reset. The driver's job is to `openat`/`write`/`close` it; this file
; issues no syscall at all, which is what lets `tests/unit/*.asm` compare
; its output byte for byte without a file system.
proc bfa_emit_program, module, arena, potestates, mxcsr
	uses	rbx, r12
	locals
		slot ctx, dq
		slot i, dd
		slot n, dd
		slot mptr, dq
		slot mlen, dq
	endl
	mov	rdi, [arena]
	mov	rsi, sizeof.BfaPrintCtx
	call	arena_alloc
	mov	[ctx], rax
	mov	rcx, [module]
	mov	[rax + BfaPrintCtx.module], rcx

	mov	rdi, [arena]
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	rcx, rax
	mov	rdi, [ctx]
	mov	[rdi + BfaPrintCtx.obuf], rcx
	mov	rdi, rcx
	mov	rsi, [arena]
	mov	rdx, 1
	mov	rcx, 65536
	call	vec_init

	; ---- banner, include, format, entry ------------------------------
	mov	rdi, [ctx]
	jmp	.after_head
  .lit_head db "; exsecutor: reference backend", 10
	db "include 'format/format.inc'", 10
	db "format ELF64 executable 3", 10
	db "entry exsrt_start", 10
  .after_head:
	lea	rsi, [.lit_head]
	mov	rdx, .after_head - .lit_head
	call	__bfa_out_lit

	; ---- the twelve constants ----------------------------------------
	mov	rdi, [ctx]
	mov	rsi, [potestates]
	mov	rdx, [mxcsr]
	call	__bfa_prog_constants

	; ---- executable segment: the blob, then the module ---------------
	mov	rdi, [ctx]
	jmp	.after_seg1
  .lit_seg1 db "segment readable executable", 10
  .after_seg1:
	lea	rsi, [.lit_seg1]
	mov	rdx, .after_seg1 - .lit_seg1
	call	__bfa_out_lit

	mov	rdi, [ctx]
	lea	rsi, [__bfa_prelude_text]
	mov	rdx, __bfa_prelude_text_end - __bfa_prelude_text
	call	__bfa_out_bytes

	mov	rdi, [module]
	mov	rsi, [arena]
	call	bfa_emit_module
	mov	[mptr], rax
	mov	[mlen], rdx
	mov	rdi, [ctx]
	mov	rsi, [mptr]
	mov	rdx, [mlen]
	call	__bfa_out_bytes

	; ---- data segment: the literals, in global id order --------------
	mov	rdi, [ctx]
	jmp	.after_seg2
  .lit_seg2 db "segment readable", 10
  .after_seg2:
	lea	rsi, [.lit_seg2]
	mov	rdx, .after_seg2 - .lit_seg2
	call	__bfa_out_lit

	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.globals]
	mov	rax, [rdi + Vec.len]
	mov	[n], eax
	mov	dword [i], 0
  .globloop:
	mov	eax, [i]
	cmp	eax, [n]
	jge	.globdone
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.globals]
	mov	esi, [i]
	call	vec_get
	push	rax
	mov	rdi, [ctx]
	mov	rsi, [module]
	pop	rdx
	call	__bfa_prog_global
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.globloop
  .globdone:

	; ---- writeable segment: the prelude's own mutable state ----------
	mov	rdi, [ctx]
	jmp	.after_seg3
  .lit_seg3 db "segment readable writeable", 10
  .after_seg3:
	lea	rsi, [.lit_seg3]
	mov	rdx, .after_seg3 - .lit_seg3
	call	__bfa_out_lit

	mov	rdi, [ctx]
	lea	rsi, [__bfa_prelude_data]
	mov	rdx, __bfa_prelude_data_end - __bfa_prelude_data
	call	__bfa_out_bytes

	mov	rdi, [ctx]
	mov	rdi, [rdi + BfaPrintCtx.obuf]
	mov	rax, [rdi + Vec.data]
	mov	rdx, [rdi + Vec.len]
	return
endp
