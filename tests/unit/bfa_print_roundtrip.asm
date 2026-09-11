; tests/unit/bfa_print_roundtrip.asm
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
; The round-trip test docs/design/ssa-ir.md section 2.11 asks for: "Round-
; trip is checked by re-parsing the printer's output and comparing
; structures, spans excluded." This fixture does that as a FIXPOINT check:
;
;   text1 (the @dot worked example, verbatim) --parse--> M1 --print--> text2
;   text2                                     --parse--> M2 --print--> text3
;   assert text2 == text3 (byte for byte)
;
; Spans are never in the text at all (section 2.11: "Spans are not in the
; text"), so the printer is a pure function of STRUCTURE alone; if M1 and M2
; have the same structure, printing them must produce identical bytes.
; text2 == text3 is therefore exactly "the structure survived the round
; trip", checked without needing a separate in-memory structural comparator.
;
; A second, independent check: text2 must equal the original text1 verbatim
; -- legitimate here (not an accident of a lenient comparison) because the
; hand-written source below is already in the printer's own canonical form
; (same spacing, same renumbering-in-print-order that a fresh parse
; already produces for well-formed input): any real difference would be a
; printer bug, caught the same way the segfault this fixture's own
; development hit was caught (docs/design/ssa-ir.md section 2.11's example
; is the actual regression input, not a simplified stand-in for it).
;
; Exit 0 = both checks passed. 12 = fixpoint failed (text2 != text3, a
; printer non-determinism or a structural loss across the round trip). 13 =
; text2 != the canonical original (a printer formatting/content bug).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

; backend_fasmg/ir.inc no longer includes rt/ (the consumer brings it, as
; every other module's does), so this fixture brings the chain itself (span.inc includes intern.inc).
include '../../compiler/x86_64/rt/span.inc'
include '../../compiler/x86_64/backend_fasmg/print.inc'

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
	mov	[mod1], rax

	mov	rdi, [mod1]
	lea	rsi, [ar]
	call	bfa_print_module
	mov	[text2ptr], rax
	mov	[text2len], rdx

	lea	rdi, [scr2]
	mov	rsi, 1048576
	call	arena_init
	jc	.fail1

	lea	rdi, [ar]
	lea	rsi, [scr2]
	mov	rdx, [text2ptr]
	mov	rcx, [text2len]
	call	bfa_parse_module
	mov	[mod2], rax

	mov	rdi, [mod2]
	lea	rsi, [ar]
	call	bfa_print_module
	mov	[text3ptr], rax
	mov	[text3len], rdx

	mov	rdi, [text2ptr]
	mov	rsi, [text2len]
	mov	rdx, [text3ptr]
	mov	rcx, [text3len]
	call	__bfa_streq
	test	eax, eax
	jz	.fail2

	mov	rdi, [text2ptr]
	mov	rsi, [text2len]
	lea	rdx, [srctext]
	mov	rcx, srctext.len
	call	__bfa_streq
	test	eax, eax
	jz	.fail3

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

segment readable writeable
  ar: rb sizeof.Arena
  scr: rb sizeof.Arena
  scr2: rb sizeof.Arena
  mod1: dq 0
  mod2: dq 0
  text2ptr: dq 0
  text2len: dq 0
  text3ptr: dq 0
  text3len: dq 0
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
