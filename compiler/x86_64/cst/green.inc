; compiler/x86_64/cst/green.inc
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
; THE GREEN TREE: immutable, interned, arena-allocated nodes -- §9.1's "red-green
; tree, Roslyn/rowan", the green half. A green node is `(kind, text_len,
; children[])` and knows NOTHING about where in the file it sits. That is the
; whole point of the split: position lives in the red half (cst/red.inc), which
; is computed on demand and never persisted, so two subtrees with identical
; shape and text are ONE allocation no matter how many places they occur.
;
; A GREEN NODE IS NAMED BY AN ID, NOT BY AN ADDRESS. Every green node is an
; index into one `Vec`, and children are stored as 1-based ids, never as
; pointers. This is not a memory optimisation, it is CLAUDE.md's "no ordering
; may depend on a pointer value" taken literally: interning has to hash
; something, and hashing a child POINTER would make the interner's probe
; sequence -- and therefore, through `map_iterate`, any order derived from it
; -- depend on where mmap happened to put the arena. Ids are assigned in
; creation order, creation order is parse order, and parse order is a function
; of the input bytes alone. `id` 0 is "no node", the same sentinel convention
; rt/intern.inc uses.
;
; INTERNING IDENTITY, exactly. Two green nodes are the same node iff:
;   - leaf:  same kind and the same text (as an rt/intern.inc id -- and §8.1
;            guarantees the source is NFC, so byte equality IS §8.2's
;            comparison, with no re-normalisation here);
;   - node:  same kind, same child count, and pairwise identical child IDS.
; Both are expressed as a byte key handed to one `rt/map.inc` map. Because
; child ids are themselves canonical, structural equality is one flat compare
; instead of a recursive walk -- the standard hash-consing argument.
;
; THE KEY IS BUILT IN THE WORK STACK, IN PLACE. `cst_open` pushes two spare
; dd slots before a node's children accumulate; `cst_close` fills them with
; (kind, count), which makes the already-contiguous run `[kind][n][ids...]`
; the map key with no copy and no scratch buffer. Only on a MISS is it copied
; into the arena (rt/map.inc borrows a key, it does not copy one), and that
; copy is then the node's own child array -- so the key and the children are
; one allocation rather than two. A hit allocates nothing at all.
;
; WHAT THIS FILE DOES NOT DO: it never looks at the source text, never reports
; a diagnostic, and never decides what a node MEANS. It is a builder over
; kinds and bytes; §8.6's grammar lives entirely in cst/parse.inc.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.1 (the red-green tree), §9.3
; (determinism), §8.2 (byte equality after NFC).
; -----------------------------------------------------------------------------

include 'kinds.inc'

; A leaf's child count. Never a real count -- `cst_close` would need 4 GiB of
; children to reach it -- so one field distinguishes a leaf from a node and
; the two key shapes can share one map.
CST_GREEN_LEAF = 0xFFFFFFFF

; Bytes of key prefix `cst_open` reserves in the work stack: the kind slot and
; the count slot, both dd.
CST_KEY_PREFIX = 8

; ---- the green record ------------------------------------------------------
; 32 bytes. `text` and `kids` are the two halves of one union that this dialect
; has no way to spell, so both are present and exactly one is meaningful:
; `nkid = CST_GREEN_LEAF` selects `text`, anything else selects `kids`.
struct CstGreen
	field kind, dd		; CST_* (cst/kinds.inc)
	field nkid, dd		; child count, or CST_GREEN_LEAF
	field tlen, dq		; total text length of this subtree, in bytes
	field text, dd		; leaf: rt/intern.inc id of its bytes; 0 if empty
	field pad0, dd
	field kids, dq		; node: -> dd[nkid], the child ids
end struct

; ---- the builder -----------------------------------------------------------
; `work` is the pending-children stack, `green` the dense array of records,
; `imap` the interner. All three are caller-owned and caller-initialised, the
; same arrangement `lex_init` uses for its vectors -- this file allocates no
; storage of its own beyond what it takes from `arena`.
struct CstTree
	field arena,    dq	; -> Arena: child arrays and key copies
	field interner, dq	; -> Interner: leaf text, shared with the lexer
	field green,    dq	; -> Vec of CstGreen, stride sizeof.CstGreen
	field work,     dq	; -> Vec of dd, the builder's child stack
	field imap,     dq	; -> Map, node identity -> green id
	field root,     dd	; green id of the finished root, 0 until then
	field tkey,     dd	; scratch -- see cst_emit_token
	field tkey2,    dd
	field tkey3,    dd
end struct

; cst_tree_init(t, arena, interner, green, work, imap) -- bind a tree to its
; storage. `green`, `work` and `imap` must already be initialised (`vec_init`
; with stride sizeof.CstGreen and 4 respectively, `map_init`); this file does
; not init them, so a caller that wants several trees over one arena can
; decide for itself what is shared. Cannot fail; leaves CF clear.
proc cst_tree_init, t, arena, interner, green, work, imap
	locals
	endl
	mov	rdi, [t]
	mov	rax, [arena]
	mov	[rdi + CstTree.arena], rax
	mov	rax, [interner]
	mov	[rdi + CstTree.interner], rax
	mov	rax, [green]
	mov	[rdi + CstTree.green], rax
	mov	rax, [work]
	mov	[rdi + CstTree.work], rax
	mov	rax, [imap]
	mov	[rdi + CstTree.imap], rax
	mov	dword [rdi + CstTree.root], 0
	return
endp

; cst_green_ptr(t, id) -> rax = the `CstGreen` record for `id`. A 0 or
; out-of-range id traps through `vec_get` -- an id this compiler did not get
; from this file is an internal contract violation, not a source condition
; (docs/asm-conventions.md, "1.3 Error protocol: CF / eax").
proc cst_green_ptr, t, id
	locals
	endl
	mov	rdi, [t]
	mov	rsi, [id]
	rassert	rsi ge 1
	dec	rsi
	mov	rdi, [rdi + CstTree.green]
	call	vec_get
	return
endp

; cst_green_kid(t, id, i) -> eax = the id of child `i` of node `id`. Traps on a
; leaf or an out-of-range index.
proc cst_green_kid, t, id, i
	uses	rbx
	locals
	endl
	mov	rdi, [t]
	mov	rsi, [id]
	call	cst_green_ptr
	mov	ecx, [rax + CstGreen.nkid]
	mov	edx, CST_GREEN_LEAF
	rassert	ecx ne edx
	mov	rbx, [i]
	mov	rdx, rcx
	rassert	rbx lt rdx
	mov	rax, [rax + CstGreen.kids]
	mov	eax, [rax + rbx*4]
	return
endp

; __cst_intern_green(t, key, keylen) -> eax = an existing green id for these
; identity bytes, or 0 if this is the first time they have been seen.
proc __cst_intern_green, t, key, keylen
	locals
	endl
	mov	rdi, [t]
	mov	rdi, [rdi + CstTree.imap]
	mov	rsi, [key]
	mov	rdx, [keylen]
	call	map_get
	return
endp

; __cst_record(t, kind, nkid, tlen) -> rax = a fresh `CstGreen`, appended to the
; dense array; the caller fills in `text` or `kids`. The new id is the array's
; new length, which the caller reads back as `Vec.len`.
proc __cst_record, t, kind, nkid, tlen
	uses	rbx
	locals
	endl
	mov	rbx, [t]
	mov	rdi, [rbx + CstTree.green]
	call	vec_push
	mov	ecx, [kind]
	mov	[rax + CstGreen.kind], ecx
	mov	ecx, [nkid]
	mov	[rax + CstGreen.nkid], ecx
	mov	rcx, [tlen]
	mov	[rax + CstGreen.tlen], rcx
	mov	dword [rax + CstGreen.text], 0
	mov	dword [rax + CstGreen.pad0], 0
	mov	qword [rax + CstGreen.kids], 0
	return
endp

; __cst_green_len(t) -> rax = how many green nodes exist, which is also the id
; of the most recently created one.
proc __cst_green_len, t
	locals
	endl
	mov	rax, [t]
	mov	rax, [rax + CstTree.green]
	mov	rax, [rax + Vec.len]
	return
endp

; __cst_work_push(t, value) -- append one dd to the builder's child stack.
proc __cst_work_push, t, value
	uses	rbx
	locals
	endl
	mov	rbx, [t]
	mov	rdi, [rbx + CstTree.work]
	call	vec_push
	mov	ecx, [value]
	mov	[rax], ecx
	return
endp

; cst_open(t) -> rax = the mark to hand back to `cst_close`. Reserves the two
; dd the identity key needs in front of the children (this file's header, "THE
; KEY IS BUILT IN THE WORK STACK").
proc cst_open, t
	uses	rbx
	locals
		slot	mrk, dq
	endl
	mov	rbx, [t]
	mov	rdi, [rbx + CstTree.work]
	mov	rax, [rdi + Vec.len]
	mov	[mrk], rax
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_work_push
	mov	rdi, rbx
	xor	esi, esi
	call	__cst_work_push
	mov	rax, [mrk]
	return
endp

; cst_emit_token(t, kind, ptr, len) -> eax = the leaf's green id, ALSO pushed
; onto the work stack as a child of whatever node is open. `len` may be 0, for
; the zero-width `CST_MISSING` leaf §8.6's recovery rules require -- an empty
; leaf carries text id 0 and is never handed to `intern_id`, which has no
; meaning for zero bytes.
proc cst_emit_token, t, kind, ptr, len
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [t]
	xor	r12d, r12d			; text id
	cmp	qword [len], 0
	je	.have_text
	mov	rdi, [rbx + CstTree.interner]
	mov	rsi, [ptr]
	mov	rdx, [len]
	call	intern_id
	mov	r12d, eax
  .have_text:
	; identity key: (kind, CST_GREEN_LEAF, text id). The text id determines
	; the bytes, so the length is not part of the key -- except for the
	; empty leaf, whose text id is 0 and whose length is 0 by definition.
	mov	eax, [kind]
	mov	[rbx + CstTree.tkey], eax
	mov	dword [rbx + CstTree.tkey2], CST_GREEN_LEAF
	mov	[rbx + CstTree.tkey3], r12d
	mov	rdi, rbx
	lea	rsi, [rbx + CstTree.tkey]
	mov	rdx, 12
	call	__cst_intern_green
	test	eax, eax
	jnz	.found

	mov	rdi, rbx
	mov	esi, [kind]
	mov	edx, CST_GREEN_LEAF
	mov	rcx, [len]
	call	__cst_record
	mov	[rax + CstGreen.text], r12d

	mov	rdi, rbx
	call	__cst_green_len
	mov	r13, rax			; the new id

	; The key must outlive this call: rt/map.inc BORROWS a key pointer, and
	; `tkey` is scratch that the next token overwrites.
	mov	rdi, [rbx + CstTree.arena]
	mov	rsi, 12
	call	arena_alloc
	mov	ecx, [rbx + CstTree.tkey]
	mov	[rax], ecx
	mov	ecx, [rbx + CstTree.tkey2]
	mov	[rax + 4], ecx
	mov	ecx, [rbx + CstTree.tkey3]
	mov	[rax + 8], ecx
	mov	rdi, [rbx + CstTree.imap]
	mov	rsi, rax
	mov	rdx, 12
	mov	rcx, r13
	call	map_insert
	mov	rax, r13
	jmp	.push
  .found:
	mov	r13, rax
  .push:
	mov	rdi, rbx
	mov	esi, r13d
	call	__cst_work_push
	mov	eax, r13d
	return
endp

; cst_close(t, kind, base) -> eax = the node's green id, ALSO left on the work
; stack in place of the children it consumed -- so the enclosing node sees one
; child where a whole subtree was built. `base` is `cst_open`'s return value.
proc cst_close, t, kind, base
	uses	rbx, r12, r13, r14
	locals
		slot	cn,   dq	; child count
		slot	tl2,  dq	; summed text length
		slot	kptr, dq	; -> the key inside the work stack
	endl
	mov	rbx, [t]
	mov	r12, [rbx + CstTree.work]
	mov	rax, [r12 + Vec.len]
	sub	rax, [base]
	sub	rax, 2
	mov	[cn], rax

	; Fill the two reserved slots, making [base..len) the identity key.
	mov	r13, [r12 + Vec.data]
	mov	rax, [base]
	lea	r13, [r13 + rax*4]
	mov	[kptr], r13
	mov	eax, [kind]
	mov	[r13], eax
	mov	eax, [cn]
	mov	[r13 + 4], eax

	; Sum the children's text lengths. A node's length is its children's,
	; always -- there is no text in a node that is not in a leaf, which is
	; what makes the round-trip in cst/red.inc total.
	xor	r14, r14
	mov	qword [tl2], 0
  .sum:
	cmp	r14, [cn]
	jae	.summed
	mov	rdi, rbx
	mov	eax, [r13 + 8 + r14*4]
	mov	rsi, rax
	call	cst_green_ptr
	mov	rcx, [rax + CstGreen.tlen]
	add	[tl2], rcx
	inc	r14
	jmp	.sum
  .summed:

	mov	rdi, rbx
	mov	rsi, [kptr]
	mov	rdx, [cn]
	lea	rdx, [rdx*4 + CST_KEY_PREFIX]
	call	__cst_intern_green
	test	eax, eax
	jnz	.found

	mov	rdi, rbx
	mov	esi, [kind]
	mov	rdx, [cn]
	mov	rcx, [tl2]
	call	__cst_record
	mov	r14, rax			; the record

	mov	rdi, rbx
	call	__cst_green_len
	mov	r13, rax			; the new id

	; One arena block is BOTH the borrowed map key and the node's own child
	; array; `kids` points past the (kind, count) prefix.
	mov	rdi, [rbx + CstTree.arena]
	mov	rsi, [cn]
	lea	rsi, [rsi*4 + CST_KEY_PREFIX]
	call	arena_alloc
	mov	r12, rax
	mov	rdi, r12
	mov	rsi, [kptr]
	mov	rcx, [cn]
	lea	rcx, [rcx*4 + CST_KEY_PREFIX]
	cld
	rep	movsb
	lea	rax, [r12 + CST_KEY_PREFIX]
	mov	[r14 + CstGreen.kids], rax

	mov	rdi, [rbx + CstTree.imap]
	mov	rsi, r12
	mov	rdx, [cn]
	lea	rdx, [rdx*4 + CST_KEY_PREFIX]
	mov	rcx, r13
	call	map_insert
	jmp	.pop
  .found:
	mov	r13, rax
  .pop:
	; Truncate the work stack back to the mark and leave the node id there.
	; rt/vec.inc has no truncate operation -- REPORTED to asm-rt, not added
	; here (CLAUDE.md, Scope) -- and `Vec.len` is a plain public field of a
	; POD element vector, so lowering it IS the whole operation.
	mov	r12, [rbx + CstTree.work]
	mov	rax, [base]
	mov	[r12 + Vec.len], rax
	mov	rdi, rbx
	mov	esi, r13d
	call	__cst_work_push
	mov	eax, r13d
	return
endp

; cst_cancel(t, base) -- undo a `cst_open` without making a node: the children
; built since the mark shift down over the two reserved key slots and stay
; where they are, as children of whatever encloses them.
;
; This is what makes cst/kinds.inc's departure 1 an operation rather than a
; special case: every precedence level opens a mark, parses its operand, and
; cancels if no operator of that level turned up -- so `redde "..." ;` is
; three leaves under a statement instead of ten nested expression nodes, and
; the parse is unchanged either way. rust-analyzer spells the same thing
; `Marker::abandon`.
proc cst_cancel, t, base
	uses	rbx, r12, r13, r14
	locals
		slot	kn2, dq
	endl
	mov	rbx, [t]
	mov	r12, [rbx + CstTree.work]
	mov	rax, [r12 + Vec.len]
	sub	rax, [base]
	sub	rax, 2
	mov	[kn2], rax
	mov	r13, [r12 + Vec.data]
	mov	rax, [base]
	lea	r13, [r13 + rax*4]	; -> the kind slot
	xor	r14, r14
  .shift:
	cmp	r14, [kn2]
	jae	.shifted
	mov	eax, [r13 + 8 + r14*4]
	mov	[r13 + r14*4], eax
	inc	r14
	jmp	.shift
  .shifted:
	mov	rax, [base]
	add	rax, [kn2]
	mov	[r12 + Vec.len], rax
	return
endp

; cst_finish(t) -> eax = the root's green id. The work stack must hold exactly
; one node -- anything else is an unbalanced open/close in the parser, which is
; a compiler bug and traps rather than returning a half tree.
proc cst_finish, t
	uses	rbx
	locals
	endl
	mov	rbx, [t]
	mov	rdi, [rbx + CstTree.work]
	mov	rax, [rdi + Vec.len]
	rassert	rax eq 1
	mov	rax, [rdi + Vec.data]
	mov	eax, [rax]
	mov	[rbx + CstTree.root], eax
	mov	rdi, [rbx + CstTree.work]
	mov	qword [rdi + Vec.len], 0
	return
endp
