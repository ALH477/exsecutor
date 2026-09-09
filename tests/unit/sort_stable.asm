; tests/unit/sort_stable.asm
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
; REQUIRED fixture (per this wave's brief, explicitly: "not optional") for
; compiler/x86_64/rt/sort.inc's stability claim: a comparator that ties on
; a real key does not tell you anything about whether the sort is stable
; -- only checking that TIES preserve original relative order does. A sort
; that is merely "a correct sort" (general-correctness fixture:
; tests/unit/sort_ops.asm) can still shuffle equal-key elements freely and
; pass every ordinary check.
;
; Seven `{key: dd, tag: dd}` records, inserted as:
;   (3,A) (1,B) (3,C) (2,D) (1,E) (3,F) (1,G)
; sorted by `key` ONLY (the comparator never looks at `tag`). A correct
; but UNSTABLE sort could legally produce any permutation of each key's
; tags; a STABLE sort must produce exactly:
;   (1,B) (1,E) (1,G)  (2,D)  (3,A) (3,C) (3,F)
; -- each key group's tags in their original input order. This fixture
; checks every field of every one of the 7 output records against that
; exact sequence, not just that the keys ended up sorted.
;
; Exit 0 = all checks passed; 10+N = check N failed (see tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/sort.inc'

; Comparator looks at `key` ONLY -- `tag` is invisible to it, so any
; observed tag ordering among equal keys comes from the sort's own
; stability, not from the comparator breaking ties itself.
proc cmp_key_only, a, b
	locals
	endl
	mov	rcx, [a]
	mov	eax, [rcx]
	mov	rdx, [b]
	mov	ecx, [rdx]
	cmp	eax, ecx
	jl	.lt
	jg	.gt
	xor	eax, eax
	return
  .lt:
	mov	eax, -1
	return
  .gt:
	mov	eax, 1
	return
endp

segment readable executable
  start:
	lea	rdi, [ar]
	mov	rsi, 1048576
	call	arena_init
	jc	.fail0

	lea	rdi, [ar]
	lea	rsi, [recs]
	mov	rdx, 7
	mov	rcx, 8
	lea	r8, [cmp_key_only]
	call	sort_stable

	; expected: (1,B)(1,E)(1,G)(2,D)(3,A)(3,C)(3,F)
	; tags as ASCII: A=65 B=66 C=67 D=68 E=69 F=70 G=71
	mov	eax, [recs+0]
	cmp	eax, 1
	jne	.fail1
	mov	eax, [recs+4]
	cmp	eax, 66			; B
	jne	.fail1

	mov	eax, [recs+8]
	cmp	eax, 1
	jne	.fail1
	mov	eax, [recs+12]
	cmp	eax, 69			; E
	jne	.fail1

	mov	eax, [recs+16]
	cmp	eax, 1
	jne	.fail1
	mov	eax, [recs+20]
	cmp	eax, 71			; G
	jne	.fail1

	mov	eax, [recs+24]
	cmp	eax, 2
	jne	.fail1
	mov	eax, [recs+28]
	cmp	eax, 68			; D
	jne	.fail1

	mov	eax, [recs+32]
	cmp	eax, 3
	jne	.fail1
	mov	eax, [recs+36]
	cmp	eax, 65			; A
	jne	.fail1

	mov	eax, [recs+40]
	cmp	eax, 3
	jne	.fail1
	mov	eax, [recs+44]
	cmp	eax, 67			; C
	jne	.fail1

	mov	eax, [recs+48]
	cmp	eax, 3
	jne	.fail1
	mov	eax, [recs+52]
	cmp	eax, 70			; F
	jne	.fail1

	mov	eax, 231
	xor	edi, edi
	syscall

  .fail0: mov eax,231
	mov edi,10
	syscall
  .fail1: mov eax,231
	mov edi,11
	syscall

segment readable writeable
  ar: rb sizeof.Arena
  ; (key,tag) pairs, in this exact input order: 3/A 1/B 3/C 2/D 1/E 3/F 1/G
  recs dd 3,65, 1,66, 3,67, 2,68, 1,69, 3,70, 1,71
