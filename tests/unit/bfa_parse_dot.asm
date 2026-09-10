; tests/unit/bfa_parse_dot.asm
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
; asm-rt fixture for compiler/x86_64/backend_fasmg/parse.inc: parses the
; worked `@dot` example VERBATIM from docs/design/ssa-ir.md section 2.11
; (dot product over two f32 arrays via an `ordinata` reduction) and checks
; the resulting structure: block count, and the exact instruction count per
; block INCLUDING void statements (jmp/br/contrib), since that is exactly
; what proves the parser is not silently dropping or miscounting the
; control-flow terminators and the reduction's `contrib` statement.
;
;   1. exactly 1 function
;   2. exactly 4 blocks (b0..b3)
;   3. exactly 19 instructions total (14 value-producing `%0..%13` plus 5
;      void statements: two `jmp`, one `br`, one `contrib`, one `ret` --
;      see the per-block breakdown in the source comment below)
;
; This exercises: functio header parsing (glued parens `(ptr ptr u64)`,
; `numeri` attribute), `param`, `iconst`, `redinit`, `jmp`, `phi` (the one
; forward-reference case -- `%11` used in b1's phi before its own
; definition in b2), `cmp.lt`, `br`, `index`, `load`, `addw` (with a bare
; integer immediate operand, not a `%N` -- ssa-ir.md's own worked example,
; not this file's invention), `fmul`, `contrib`, `redfin`, `ret`.
;
; Exit 0 = all checks passed; 10+N = check N failed (see tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/backend_fasmg/parse.inc'

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

	mov	rdi, [modout]
	mov	rdi, [rdi + BfaModule.funcs]
	mov	rax, [rdi + Vec.len]
	cmp	rax, 1
	jne	.fail2

	mov	rdi, [modout]
	mov	rdi, [rdi + BfaModule.funcs]
	xor	rsi, rsi
	call	vec_get
	mov	rax, [rax]
	mov	[fn], rax

	mov	rdi, [fn]
	mov	rdi, [rdi + BfaFunc.blocks]
	mov	rax, [rdi + Vec.len]
	cmp	rax, 4
	jne	.fail3

	; b0: param,param,param,iconst,redinit,jmp        = 6
	; b1: phi,cmp,br                                   = 3
	; b2: index,load,index,load,addw,fmul,contrib,jmp  = 8
	; b3: redfin,ret                                    = 2
	; total = 19
	mov	rdi, [fn]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	rax, [rdi + Vec.len]
	cmp	rax, 19
	jne	.fail4

	mov	eax, 231
	xor	edi, edi
	syscall

  .fail1: mov eax,231
	mov edi,11
	syscall
  .fail2: mov eax,231
	mov edi,12
	syscall
  .fail3: mov eax,231
	mov edi,13
	syscall
  .fail4: mov eax,231
	mov edi,14
	syscall

segment readable writeable
  ar: rb sizeof.Arena
  scr: rb sizeof.Arena
  modout: dq 0
  fn: dq 0
  srctext:
  db "functio @dot (ptr ptr u64) -> f32 numeri ad_parem vetita explicita conservata {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = param ptr 1", 10
  db "%2 = param u64 2", 10
  db "%3 = iconst u64 0", 10
  db "%4 = redinit f32 fadd ordinata 0", 10
  db "jmp b1", 10
  db "b1:", 10
  db "%5 = phi u64 b0 %3 b2 %11", 10
  db "%6 = cmp.lt u64 %5 %2", 10
  db "br %6 b2 b3", 10
  db "b2:", 10
  db "%7 = index %0 %5 4", 10
  db "%8 = load f32 %7 0 nativus", 10
  db "%9 = index %1 %5 4", 10
  db "%10 = load f32 %9 0 nativus", 10
  db "%11 = addw u64 %5 1", 10
  db "%12 = fmul f32 %8 %10", 10
  db "contrib %4 %12", 10
  db "jmp b1", 10
  db "b3:", 10
  db "%13 = redfin f32 %4", 10
  db "ret %13", 10
  db "}", 10
  .len = $ - srctext
