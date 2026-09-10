; compiler/x86_64/cst/red.inc
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
; THE RED HALF: absolute positions, computed on demand and NEVER PERSISTED.
; §9.1's other half of the red-green tree. A `CstRed` is `(green, parent,
; text_offset)` and lives on the caller's stack for exactly as long as the
; caller is looking at that node -- there is no red arena, no red vector and
; no red node stored inside a green one, because storing one would undo the
; sharing the green half exists for: an interned subtree occurring twice has
; two positions and one allocation, and only a computed-on-demand view can say
; that.
;
; WHY THE OFFSET IS A SUM AND NOT A FIELD. `cst_red_child` computes a child's
; offset as the parent's offset plus the text lengths of the children before
; it. That is O(children) per step and O(depth * width) for a full walk -- the
; cost rowan pays too, and the reason it is acceptable is that a green node's
; `tlen` is precomputed, so the sum is an add per sibling and never a descent.
;
; THE ROUND-TRIP IS THIS FILE'S TOTALITY CLAIM. `cst_text` concatenates every
; leaf's bytes in tree order. §9.1 requires the CST to be lossless, and the
; only honest test of that is that the concatenation equals the source byte
; for byte -- on malformed input as much as on well-formed input, which is why
; cst/parse.inc puts skipped tokens and even the bytes it never tokenized into
; the tree rather than dropping them. `tests/unit/cst_roundtrip.asm` is that
; test.
;
; It writes through diag/render.inc's `DiagOut` rather than a buffer of its
; own: `DiagOut` is already the bounded, all-or-nothing byte sink this tree
; uses everywhere else, and a second one would be a second truncation policy
; to get wrong.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.1, §8.3 (spans).
; -----------------------------------------------------------------------------

include 'green.inc'

; ---- the red view ----------------------------------------------------------
; `parent` is a plain pointer to the CALLER'S `CstRed`, which is a stack
; address by construction. That is safe precisely because a red node is never
; persisted: it cannot outlive the frame that made it. It is also the one
; pointer in this module, and it orders nothing -- see cst/green.inc's header
; on why child links are ids instead.
struct CstRed
	field green,  dd	; green id
	field pad0,   dd
	field parent, dq	; -> the enclosing CstRed, or 0 at the root
	field off,    dq	; absolute byte offset of this node's first byte
end struct

; cst_red_root(red, t) -- point `red` at the finished tree's root, at offset 0.
; `cst_finish` must have run; a tree with no root traps.
proc cst_red_root, red, t
	locals
	endl
	mov	rdi, [red]
	mov	rsi, [t]
	mov	eax, [rsi + CstTree.root]
	rassert	eax ge 1
	mov	[rdi + CstRed.green], eax
	mov	dword [rdi + CstRed.pad0], 0
	mov	qword [rdi + CstRed.parent], 0
	mov	qword [rdi + CstRed.off], 0
	return
endp

; cst_red_nkid(t, red) -> rax = child count, or 0 for a leaf. A leaf answers 0
; rather than trapping: a walk should be able to ask without first asking
; whether it may ask.
proc cst_red_nkid, t, red
	locals
	endl
	mov	rdi, [t]
	mov	rsi, [red]
	mov	esi, [rsi + CstRed.green]
	call	cst_green_ptr
	mov	ecx, [rax + CstGreen.nkid]
	xor	eax, eax
	cmp	ecx, CST_GREEN_LEAF
	je	.leaf
	mov	eax, ecx
  .leaf:
	return
endp

; cst_red_kind(t, red) -> eax = the node's CST_* kind.
proc cst_red_kind, t, red
	locals
	endl
	mov	rdi, [t]
	mov	rsi, [red]
	mov	esi, [rsi + CstRed.green]
	call	cst_green_ptr
	mov	eax, [rax + CstGreen.kind]
	return
endp

; cst_red_len(t, red) -> rax = the node's text length in bytes.
proc cst_red_len, t, red
	locals
	endl
	mov	rdi, [t]
	mov	rsi, [red]
	mov	esi, [rsi + CstRed.green]
	call	cst_green_ptr
	mov	rax, [rax + CstGreen.tlen]
	return
endp

; cst_red_child(t, red, i, dest) -- fill `dest` with the red view of child `i`.
; `dest` may not alias `red`. Traps if `red` is a leaf or `i` is out of range.
;
; NAMED `dest`, NOT `out`: `out` is a real x86 instruction mnemonic, and
; macros/proc.inc binds an argument name as an unmangled global, so `proc ...,
; out` fails at the binding with "missing argument" and names neither the
; instruction nor the argument (docs/asm-conventions.md 4.1, "bare instruction
; mnemonics collide case-insensitively" -- found here by assembling it).
proc cst_red_child, t, red, i, dest
	uses	rbx, r12, r13, r14
	locals
		slot	acc,  dq
		slot	kidv, dq
	endl
	mov	rbx, [t]
	mov	r12, [red]
	mov	r13, [dest]
	mov	rdi, rbx
	mov	esi, [r12 + CstRed.green]
	call	cst_green_ptr
	mov	ecx, [rax + CstGreen.nkid]
	mov	edx, CST_GREEN_LEAF
	rassert	ecx ne edx
	mov	rdx, rcx
	mov	rcx, [i]
	rassert	rcx lt rdx
	mov	rcx, [rax + CstGreen.kids]
	mov	[kidv], rcx

	; offset = parent offset + sum of the preceding siblings' lengths
	mov	rax, [r12 + CstRed.off]
	mov	[acc], rax
	xor	r14, r14
  .sum:
	cmp	r14, [i]
	jae	.summed
	mov	rdi, rbx
	mov	rcx, [kidv]
	mov	esi, [rcx + r14*4]
	call	cst_green_ptr
	mov	rdx, [rax + CstGreen.tlen]
	add	[acc], rdx
	inc	r14
	jmp	.sum
  .summed:
	mov	rcx, [kidv]
	mov	eax, [rcx + r14*4]
	mov	[r13 + CstRed.green], eax
	mov	dword [r13 + CstRed.pad0], 0
	mov	[r13 + CstRed.parent], r12
	mov	rax, [acc]
	mov	[r13 + CstRed.off], rax
	return
endp

; cst_red_span(t, red, file_id, dest) -- write this node's §8.3 source span.
; It is an rt/span.inc `Span`, built by `span_make` and not by three stores, so
; a CST node's span, a token's span and a diagnostic's span stay the same
; 12-byte record with no translation (lexer/token.inc's header makes the same
; argument for `Tok`).
proc cst_red_span, t, red, file_id, dest
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [t]
	mov	r12, [red]
	mov	r13, [dest]
	mov	rdi, rbx
	mov	rsi, r12
	call	cst_red_len
	mov	rdi, r13
	mov	esi, [file_id]
	mov	rdx, [r12 + CstRed.off]
	mov	rcx, rax
	call	span_make
	return
endp

; ---- the round trip --------------------------------------------------------

; cst_text(t, id, wr) -> eax = 1 if every byte fit in `wr`, 0 if the writer
; truncated. Appends the subtree's source text, in tree order, to a `DiagOut`.
; Recursive: depth is the tree's, which is the source's nesting depth.
proc cst_text, t, id, wr
	uses	rbx, r12, r13, r14
	locals
		slot	n, dq
	endl
	mov	rbx, [t]
	mov	r13, [wr]
	mov	rdi, rbx
	mov	rsi, [id]
	call	cst_green_ptr
	mov	r12, rax
	mov	ecx, [r12 + CstGreen.nkid]
	cmp	ecx, CST_GREEN_LEAF
	jne	.node

	; leaf: its bytes are whatever it interned, or nothing at all
	mov	ecx, [r12 + CstGreen.text]
	test	ecx, ecx
	jz	.empty
	mov	rdi, [rbx + CstTree.interner]
	mov	rsi, rcx
	call	intern_bytes		; rax = bytes, rdx = length
	mov	r14, rdx
	mov	rdi, r13
	mov	rsi, rax
	mov	rdx, r14
	call	diag_out_bytes
	return
  .empty:
	mov	eax, 1
	return
  .node:
	mov	[n], rcx
	xor	r14, r14
  .kid:
	cmp	r14, [n]
	jae	.done
	mov	rdi, rbx
	mov	rsi, [id]
	mov	rdx, r14
	call	cst_green_kid
	mov	rdi, rbx
	mov	esi, eax
	mov	rdx, r13
	call	cst_text
	test	eax, eax
	jz	.short
	inc	r14
	jmp	.kid
  .done:
	mov	eax, 1
	return
  .short:
	xor	eax, eax
	return
endp

; ---- a tree dump, for fixtures and for debugging ---------------------------
; One line per node: `<indent><KIND>@<off>+<len>`, depth-first. Not a
; diagnostic and not output -- nothing in §8.3's rendering path calls this, and
; kind names carry none of §8.3's permanence (cst/kinds.inc's header).

; __cst_dump_rec(t, red, wr, depth, file_id)
proc __cst_dump_rec, t, red, wr, depth, file_id
	uses	rbx, r12, r13, r14
	locals
		slot	kid,  dq
		slot	nkid, dq
		slot	sub2, sizeof.CstRed
	endl
	mov	rbx, [t]
	mov	r12, [red]
	mov	r13, [wr]

	mov	r14, [depth]
  .indent:
	test	r14, r14
	jz	.indented
	mov	rdi, r13
	mov	esi, ' '
	mov	rdx, 2
	call	diag_out_fill
	dec	r14
	jmp	.indent
  .indented:
	mov	rdi, rbx
	mov	rsi, r12
	call	cst_red_kind
	mov	rdi, rax
	call	cst_kind_name		; rax = name, rdx = length
	mov	r14, rdx
	mov	rdi, r13
	mov	rsi, rax
	mov	rdx, r14
	call	diag_out_bytes
	mov	rdi, r13
	mov	esi, '@'
	mov	rdx, 1
	call	diag_out_fill
	mov	rdi, r13
	mov	rsi, [r12 + CstRed.off]
	call	diag_out_u32
	mov	rdi, r13
	mov	esi, '+'
	mov	rdx, 1
	call	diag_out_fill
	mov	rdi, rbx
	mov	rsi, r12
	call	cst_red_len
	mov	rdi, r13
	mov	rsi, rax
	call	diag_out_u32
	mov	rdi, r13
	mov	esi, 0x0A
	mov	rdx, 1
	call	diag_out_fill

	mov	rdi, rbx
	mov	rsi, r12
	call	cst_red_nkid
	mov	[nkid], rax
	mov	qword [kid], 0
  .child:
	mov	rax, [kid]
	cmp	rax, [nkid]
	jae	.done
	mov	rdi, rbx
	mov	rsi, r12
	mov	rdx, [kid]
	lea	rcx, [sub2]
	call	cst_red_child
	mov	rdi, rbx
	lea	rsi, [sub2]
	mov	rdx, r13
	mov	rcx, [depth]
	inc	rcx
	mov	r8d, [file_id]
	call	__cst_dump_rec
	inc	qword [kid]
	jmp	.child
  .done:
	return
endp

; cst_dump(t, wr, file_id) -- the whole tree, from the root.
proc cst_dump, t, wr, file_id
	uses	rbx
	locals
		slot	rt2, sizeof.CstRed
	endl
	lea	rdi, [rt2]
	mov	rsi, [t]
	call	cst_red_root
	mov	rdi, [t]
	lea	rsi, [rt2]
	mov	rdx, [wr]
	xor	ecx, ecx
	mov	r8d, [file_id]
	call	__cst_dump_rec
	return
endp
