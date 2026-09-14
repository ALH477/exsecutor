; compiler/x86_64/backend_fasmg/ir.inc
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
; SSA IR in-memory representation for the fasmg reference backend: the vecs,
; structs and opcode/type enumerations docs/design/ssa-ir.md section 2.1
; describes ("the contract both backends are written against"). No node
; pointers, no per-instruction operand vectors -- indices into `Vec`s, one
; append-only `extra` pool per function for anything variable-length, exactly
; as that section specifies.
;
; NAMING: every global here is prefixed `bfa_` (this module's prefix) or, for
; internals not meant to be called from outside this tree, `__bfa_` -- checked
; against docs/asm-conventions.md section 4.1's burned-name list before writing
; a single one of them. `Bfa`-prefixed struct names for the same reason.
;
; WHY A SEPARATE STRUCT PER VEC ELEMENT, NEVER A NESTED `struct`-IN-`struct`
; FIELD: macros/struct.inc's `field name, decl` sugar expands to the bare
; native form `name decl ?`, and only dq/dd/dw/db accept a trailing `?` as an
; "uninitialized" marker -- a struct name (e.g. `Vec`) does not, and whether
; `field insts, Vec` would even parse is untested by anything this tree has
; run. Rather than add a second untested hypothesis to a file that is
; supposed to be evidence, every "vec of records" field below is a plain `dq`
; POINTER to an independently arena-allocated `Vec`, exactly the pattern
; rt/map.inc already uses and already tests (`Map.entries -> Vec of
; MapEntry`). Growing that Vec moves its own backing block; it never moves
; the struct holding the pointer to it.
;
; VALUE / BLOCK IDENTITY. A value *is* a 1-based index into a function's
; `insts` Vec (0 = none) -- section 2.1's own words, unmodified here. A block
; id is a 1-based index into `blocks` (0 = none) for the same "0 is a free
; sentinel" reason. These are INTERNAL ids. The TEXT `%N`/`bN` the parser
; reads and the printer writes are a separate, denser numbering that skips
; void (non-value-producing) instructions -- section 2.11's own grammar
; (`Line ::= ['%' INT '='] OP Tok*`) makes the `%N =` prefix optional per
; line, not per opcode, so text order and internal creation order coincide
; only for the value-producing subsequence. `parse.inc` carries the
; translation table; nothing in this file needs it.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md section 9.2 (the fasmg backend is
; normative), section 5.4 (numeric semantics this IR exists to carry
; faithfully). Design contract: docs/design/ssa-ir.md, sections 2.1-2.3.
; -----------------------------------------------------------------------------

; NO rt/ INCLUDE. This file needs rt/intern.inc's chain (intern -> map -> vec
; -> arena -> struct, assert, sys, proc) and does not include it: the CONSUMER
; brings rt/, which is what ast/, checker/, cst/, diag/, driver/ and lexer/ all
; already do, and what exsc.asm already arranges.
;
; It used to include it, on a diamond-discipline argument that was right about
; diamonds and wrong about who owns the root. cst/ reaches rt/intern.inc by its
; own path (diag/ -> rt/span.inc); fasmg has no include guards; so the second
; path through that file died with `symbol 'sizeof?.Arena' is undefined` and
; NO fasmg program could contain both checker/ and backend_fasmg/ at once --
; that is, no `exsc` could both type-check a program and emit one. Found by
; the lowering agent, which had to restate `struct Span` to work around it
; (compiler/x86_64/lower/ssa.inc).
;
; Every other backend_fasmg/*.inc still includes exactly one other file in THIS
; directory, never rt/ directly: a single strict chain, never a diamond.

; ---- type kinds (BfaType.kind) -----------------------------------------
BFA_TK_U    = 1		; uN
BFA_TK_I    = 2		; iN
BFA_TK_F32  = 3
BFA_TK_F64  = 4
BFA_TK_PTR  = 5
BFA_TK_DPTR = 6		; reserved, Stage 3 [OPEN] per ssa-ir.md section 2.2 -- parsed, never emitted
BFA_TK_REF  = 7
BFA_TK_REFC = 8
BFA_TK_RED  = 9		; red.F -- `ref` = the inner type id
BFA_TK_FN   = 10	; fn.S  -- `ref` = the signature index (not interned/deduped -- see bfa_sig_new)
BFA_TK_VF32 = 11	; vf32.N -- `ref` = the lane count N (2/4/8); width 0
BFA_TK_VF64 = 12	; vf64.N -- `ref` = the lane count N (2/4/8); width 0.
				; Like F32/F64, the kind carries the element size; the
				; `fn.N`/`red.` precedent puts N in `ref`. Width is 0
				; CANONICALLY -- parse/lower's float-width discrepancy
				; (parse.inc's f32 arm at 32 vs lower/ty.inc's at 0) is
				; not repeated here: one interpretation, kind+ref carry
				; everything, interning is on all 8 bytes regardless.

; ---- comparison / float-comparison predicates (Inst.aux for OP_CMP/OP_FCMP)
BFA_PRED_EQ = 0
BFA_PRED_NE = 1
BFA_PRED_LT = 2
BFA_PRED_LE = 3
BFA_PRED_GT = 4
BFA_PRED_GE = 5

; ---- memory access byte order (Inst.aux for load/store/loadbits/storebits)
; Closed 3-word vocabulary (docs/design/ssa-ir.md section 2.7) -- a fixed
; enum, not interned, because the grammar admits no fourth spelling.
BFA_ORDER_NATIVUS = 0
BFA_ORDER_MAIOR   = 1
BFA_ORDER_MINOR   = 2

; ---- reduction shape (Inst.a for OP_REDINIT) -- closed 2-word vocabulary
BFA_RSHAPE_ORDINATA = 0
BFA_RSHAPE_ARBOREA  = 1

; ---- opcodes (Inst.op). One code per distinct MNEMONIC FAMILY: the six
; `cmp.*`/`fcmp.*` spellings each collapse to OP_CMP/OP_FCMP with the
; predicate carried in `aux` (docs/design/ssa-ir.md section 2.3's own table
; groups them this way; the six *texts* are still six distinct opcode-table
; rows in parse.inc's dispatch map, see there).
BFA_OP_ADD      = 1
BFA_OP_SUB      = 2
BFA_OP_MUL      = 3
BFA_OP_DIV      = 4
BFA_OP_REM      = 5
BFA_OP_ADDW     = 6
BFA_OP_SUBW     = 7
BFA_OP_MULW     = 8
BFA_OP_ADDS     = 9
BFA_OP_SUBS     = 10
BFA_OP_MULS     = 11
BFA_OP_ADDOV    = 12
BFA_OP_SUBOV    = 13
BFA_OP_MULOV    = 14
BFA_OP_AND      = 15
BFA_OP_OR       = 16
BFA_OP_XOR      = 17
BFA_OP_SHL      = 18
BFA_OP_SHR      = 19
BFA_OP_CMP      = 20
BFA_OP_ZEXT     = 21
BFA_OP_SEXT     = 22
BFA_OP_TRUNC    = 23
BFA_OP_FADD     = 24
BFA_OP_FSUB     = 25
BFA_OP_FMUL     = 26
BFA_OP_FDIV     = 27
BFA_OP_FNEG     = 28
BFA_OP_FMA      = 29
BFA_OP_FCMP     = 30
BFA_OP_FEXT     = 31
BFA_OP_FTRUNC   = 32
BFA_OP_ITOF     = 33
BFA_OP_FTOI     = 34
BFA_OP_BITCAST  = 35
BFA_OP_REDINIT  = 36
BFA_OP_CONTRIB  = 37
BFA_OP_REDFIN   = 38
BFA_OP_SLOT     = 39
BFA_OP_LOAD     = 40
BFA_OP_STORE    = 41
BFA_OP_LOADBITS = 42
BFA_OP_STOREBITS= 43
BFA_OP_COPY     = 44
BFA_OP_ADDR     = 45
BFA_OP_INDEX    = 46
BFA_OP_GADDR    = 47
BFA_OP_CHK      = 48
BFA_OP_RETAIN   = 49
BFA_OP_RELEASE  = 50
BFA_OP_CALL     = 51
BFA_OP_CALLIND  = 52
BFA_OP_RET      = 53
BFA_OP_JMP      = 54
BFA_OP_BR       = 55
BFA_OP_TRAP     = 56
BFA_OP_PHI      = 57
BFA_OP_PARAM    = 58
BFA_OP_ICONST   = 59
BFA_OP_FCONST   = 60
BFA_OP_VADD     = 61
BFA_OP_VSUB     = 62
BFA_OP_VMUL     = 63
BFA_OP_VDIV     = 64

; ---- operand shapes (drives parse.inc's tokenizer AND print.inc's
; re-emission -- one interpreter for both directions, never two).
BFA_SH_BIN       = 1	; T %a %b               (inline a,b)
BFA_SH_BINU1     = 2	; T %a %b  -> u1         (inline a,b; addov/subov/mulov)
BFA_SH_CMP       = 3	; T %a %b  -> u1, aux=pred
BFA_SH_FCMP      = 4	; F %a %b  -> u1, aux=pred
BFA_SH_UN        = 5	; T %a                   (fneg)
BFA_SH_CONV      = 6	; T %a                   (zext/sext/trunc/fext/ftrunc/itof/ftoi/bitcast)
BFA_SH_FMA       = 7	; F %a %b %c             (extra: 3)
BFA_SH_REDINIT   = 8	; F op shape w           (inline a=shape,b=w; aux=op word, ty=red.F)
BFA_SH_CONTRIB   = 9	; %h %v                  (inline a,b; no type token)
BFA_SH_REDFIN    = 10	; F %h                   (inline a)
BFA_SH_SLOT      = 11	; size align             (inline a,b; ty=ptr, no type token)
BFA_SH_LOAD      = 12	; T %p off order         (extra: 3)
BFA_SH_STORE     = 13	; T %p off order %v      (extra: 4)
BFA_SH_LOADBITS  = 14	; T %p byte bit          (extra: 3)
BFA_SH_STOREBITS = 15	; T %p byte bit %v       (extra: 4)
BFA_SH_COPY      = 16	; n %d %s                (extra: 3; no type token)
BFA_SH_ADDR      = 17	; %p off                 (inline a,b; ty=ptr)
BFA_SH_INDEX     = 18	; %p %i stride           (extra: 3; ty=ptr)
BFA_SH_GADDR     = 19	; N                      (inline a; ty=ptr)
BFA_SH_CHK       = 20	; %i %n                  (inline a,b; no type token)
BFA_SH_RETAIN    = 21	; %r                     (inline a)
BFA_SH_RELEASE   = 22	; %r                     (inline a)
BFA_SH_CALL      = 23	; T @f args...           (extra: variadic; T may be void)
BFA_SH_CALLIND   = 24	; T %fp args...          (extra: variadic; T may be void)
BFA_SH_RET       = 25	; [%v]                   (inline a = value or 0)
BFA_SH_JMP       = 26	; bN                     (inline a = block id)
BFA_SH_BR        = 27	; %c bT bF               (extra: 3)
BFA_SH_TRAP      = 28	; kind                   (aux = interned kind word)
BFA_SH_PHI       = 29	; T (bP %v)+             (extra: variadic pairs)
BFA_SH_PARAM     = 30	; T i                    (inline a)
BFA_SH_ICONST    = 31	; T imm                  (extra: 2 words, lo/hi -- section 2.1's own
				; call-out: "more than two operands (phi, call,
				; 64-bit immediates) sets a to an extra index")
BFA_SH_FCONST    = 32	; F bits                 (extra: 2 words, lo/hi)

; ---- BfaType -- 8 bytes, exactly docs/design/ssa-ir.md section 2.2's
; `TypeNode { kind u8, sign u8, width u16, ref u32 }`.
struct BfaType
	field kind,  db
	field sign,  db
	field width, dw
	field ref,   dd
end struct

; ---- BfaSig -- one per function (never de-duplicated: nothing in this pass
; builds first-class function values or `callind` through a shared `fn.S`,
; so interning signatures would add bookkeeping with no reader yet --
; [UNIMPLEMENTED], recorded here rather than silently assumed away).
struct BfaSig
	field ret_ty,   dd	; 0 = void
	field p_first,  dd	; index into Module.sig_extra of the first param type
	field p_count,  dd
end struct

; ---- BfaGlobal -- `data $ID SIZE ALIGN HEXBYTES` (docs/design/ssa-ir.md
; section 2.11). `data_off`/`data_len` index Module.gdata, the module-wide
; byte pool -- parsed and printed in full; `gaddr` emission is
; [UNIMPLEMENTED] (this pass's emitter has no test that needs global data;
; see the report).
struct BfaGlobal
	field id,       dd
	field size,     dd
	field algn,     dd	; NOT `align` -- collides with fasmg's native
				; `align?` directive (found by running this file)
	field data_off, dd
	field data_len, dd
end struct

; ---- BfaEdge -- 8 bytes, `{ pred u32, next u32 }`. `pred` is a 1-based
; BLOCK id (the predecessor); `next` chains to the next edge in the same
; block's predecessor list, 0 = end.
struct BfaEdge
	field pred, dd
	field nextp, dd
end struct

; ---- BfaBlock -- phi list head/tail, body list head/tail (both singly
; linked through BfaInst.nextp, section 2.1), predecessor edge list
; head/tail, flags, and the span of its own `bN:` line (section 2.11: "a
; hand-written function gets the span of its line").
BFA_BLOCK_FLAG_SEALED  = 1
BFA_BLOCK_FLAG_QUISQUE = 2
struct BfaBlock
	field first,       dd
	field last,        dd
	field first_phi,   dd
	field last_phi,    dd
	field first_pred,  dd
	field last_pred,   dd
	field flags,       dd
	field span_file,   dd
	field span_start,  dd
	field span_len,    dd
end struct

; ---- BfaInst -- 32 bytes, exactly docs/design/ssa-ir.md section 2.1's
; `Inst { op u16, aux u16, ty u32, a u32, b u32, next u32, span Span }`
; (2+2+4+4+4+4 = 20, +12-byte span = 32). `a`/`b` mean different things per
; opcode SHAPE (BFA_SH_*, above) -- sometimes value ids, sometimes raw
; integers, sometimes (BFA_SH_FMA and every "extra"-noted shape) an
; (extra-pool index, count) pair, per section 2.1: "more than two operands
; ... sets a to an extra index and b to a count." `ty` is always the type
; TOKEN as written in the text (or the implicit `ptr` for the address-family
; ops that carry none) -- NOT always the produced value's own type; see
; bfa_value_type, below, for the handful of opcodes (cmp, fcmp, the three
; overflow predicates) where those differ, exactly as section 2.3's table
; shows with a "-> u1" annotation.
struct BfaInst
	field op,         dw
	field aux,        dw
	field ty,         dd
	field a,          dd
	field b,          dd
	field nextp,      dd
	field span_file,  dd
	field span_start, dd
	field span_len,   dd
end struct

; ---- BfaFunc. `insts`/`blocks`/`edges`/`extra` are pointers to
; independently arena-allocated Vecs (see this file's header for why never
; nested-by-value). `numeri0..3` are interned ids for the four
; `rotundatio`/`reassociatio`/`contractio`/`subnormales` words (spec section
; 5.4) -- 0 if the function carried no `numeri` attribute at all (accepted
; here so a fixture that does not care about numerics need not spell it out;
; the emitter treats an absent `numeri` as the spec's stated defaults:
; `ad_parem vetita explicita conservata`).
BFA_FUNC_ATTR_NUCLEUS  = 1
BFA_FUNC_ATTR_EXTERNUS = 2
struct BfaFunc
	field insts,       dq
	field blocks,      dq
	field edges,       dq
	field extra,       dq
	field name,        dd	; interned id (Module.names)
	field sig,         dd	; index into Module.sigs (0-based)
	field numeri0,     dd	; rotundatio
	field numeri1,     dd	; reassociatio
	field numeri2,     dd	; contractio
	field numeri3,     dd	; subnormales
	field attrs,       dd
	field extern_abi,  dd	; interned id of the externus ABI identifier, 0 if none
	field span_file,   dd
	field span_start,  dd
	field span_len,    dd
end struct

; ---- BfaModule
struct BfaModule
	field funcs,     dq	; -> Vec<dq>  (pointers to BfaFunc)
	field types,     dq	; -> Vec<BfaType>
	field type_map,  dq	; -> Map, 8-byte BfaType key -> 1-based type id
	field sigs,      dq	; -> Vec<BfaSig>
	field sig_extra, dq	; -> Vec<dd>  (parameter type ids, append-only pool)
	field globals,   dq	; -> Vec<BfaGlobal>
	field gdata,     dq	; -> Vec<db>  (global initial-data byte pool)
	field names,     dq	; -> Interner (function names + every bareword
				;    that must round-trip verbatim: numeri
				;    words, redinit's op word, externus abi)
	field func_by_name, dq	; -> Map, name bytes -> 1-based func index
	field arena,     dq	; the IR arena itself (module lifetime)
end struct

; bfa_module_init(module, arena) -- `module` is caller-owned `sizeof.BfaModule`
; bytes (mirrors arena_init's own convention: this routine does not allocate
; the header, only what it points at). `arena` backs the WHOLE IR image for
; this module's lifetime -- never reset while any id or pointer from it is
; still in use (same rule rt/intern.inc states for its own arena, restated
; here because Module.names is an Interner riding on the same arena).
proc bfa_module_init, module, arena
	uses	rbx, r12
	locals
	endl
	mov	rbx, [module]
	mov	r12, [arena]
	mov	[rbx + BfaModule.arena], r12

	mov	rdi, r12
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[rbx + BfaModule.funcs], rax
	mov	rdi, rax
	mov	rsi, r12
	mov	rdx, 8			; stride: one pointer per function
	xor	rcx, rcx
	call	vec_init

	mov	rdi, r12
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[rbx + BfaModule.types], rax
	mov	rdi, rax
	mov	rsi, r12
	mov	rdx, sizeof.BfaType
	xor	rcx, rcx
	call	vec_init

	mov	rdi, r12
	mov	rsi, sizeof.Map
	call	arena_alloc
	mov	[rbx + BfaModule.type_map], rax
	mov	rdi, rax
	mov	rsi, r12
	mov	rdx, 64			; nbuckets -- see this file's report note
	call	map_init

	mov	rdi, r12
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[rbx + BfaModule.sigs], rax
	mov	rdi, rax
	mov	rsi, r12
	mov	rdx, sizeof.BfaSig
	xor	rcx, rcx
	call	vec_init

	mov	rdi, r12
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[rbx + BfaModule.sig_extra], rax
	mov	rdi, rax
	mov	rsi, r12
	mov	rdx, 4
	xor	rcx, rcx
	call	vec_init

	mov	rdi, r12
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[rbx + BfaModule.globals], rax
	mov	rdi, rax
	mov	rsi, r12
	mov	rdx, sizeof.BfaGlobal
	xor	rcx, rcx
	call	vec_init

	mov	rdi, r12
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[rbx + BfaModule.gdata], rax
	mov	rdi, rax
	mov	rsi, r12
	mov	rdx, 1
	xor	rcx, rcx
	call	vec_init

	mov	rdi, r12
	mov	rsi, sizeof.Interner
	call	arena_alloc
	mov	[rbx + BfaModule.names], rax
	mov	rdi, rax
	mov	rsi, r12
	mov	rdx, 64
	call	intern_init

	mov	rdi, r12
	mov	rsi, sizeof.Map
	call	arena_alloc
	mov	[rbx + BfaModule.func_by_name], rax
	mov	rdi, rax
	mov	rsi, r12
	mov	rdx, 16
	call	map_init
	return
endp

; bfa_type_intern(module, kind, sign, width, ref) -> eax = 1-based type id.
; Builds the 8-byte candidate key on the stack, looks it up, and on a miss
; copies it into a FRESH arena_alloc'd 8 bytes before map_insert -- never
; points the map's key at Module.types' own backing array, which vec_push can
; move (rt/map.inc's header: "a key must remain valid for as long as the map
; is queried"). Two 8-byte copies of the same fact (one map-owned, one in the
; Vec for bfa_type_ptr's indexed lookup) is deliberate, not an oversight --
; the alternative is depending on Vec-growth-abandons-not-frees to make a
; dangling key pointer harmless, which is exactly the fragility rt/intern.inc
; was written to avoid for its own keys.
proc bfa_type_intern, module, kind, sign, width, ref
	uses	rbx, r12, r13
	locals
		slot key, 8		; 8-byte candidate BfaType, built in place
	endl
	mov	rbx, [module]
	mov	al, byte [kind]
	mov	[key + BfaType.kind], al
	mov	al, byte [sign]
	mov	[key + BfaType.sign], al
	mov	ax, word [width]
	mov	[key + BfaType.width], ax
	mov	eax, dword [ref]
	mov	[key + BfaType.ref], eax

	mov	r12, [rbx + BfaModule.type_map]
	mov	rdi, r12
	lea	rsi, [key]
	mov	rdx, 8
	call	map_get
	test	rax, rax
	jnz	.found

	mov	r13, [rbx + BfaModule.arena]
	mov	rdi, r13
	mov	rsi, 8
	call	arena_alloc
	mov	rcx, [key]
	mov	[rax], rcx		; whole 8-byte key, one qword move

	mov	rdi, [rbx + BfaModule.types]
	push	rax			; save the permanent key copy across vec_push
	call	vec_push		; rax = pointer to the new BfaType slot
	mov	rcx, [key]
	mov	[rax], rcx		; store the same 8 bytes into the Vec entry
	pop	rax

	mov	rdi, [rbx + BfaModule.types]
	mov	rdi, [rdi + Vec.len]	; new type id = post-push length (1-based)
	push	rdi
	mov	rsi, rax		; key ptr (the permanent arena copy)
	mov	rdx, 8
	mov	rcx, rdi
	mov	rdi, r12
	call	map_insert
	pop	rax
	return
  .found:
	return
endp

; bfa_type_ptr(module, type_id) -> rax = pointer to the BfaType record.
; `rassert`s (via vec_get) on an out-of-range id -- an id this file did not
; itself hand out is an internal contract violation, never a source-level
; condition (docs/asm-conventions.md, "1.3 Error protocol: CF / eax").
proc bfa_type_ptr, module, type_id
	locals
	endl
	mov	rdi, [module]
	mov	rdi, [rdi + BfaModule.types]
	mov	rsi, [type_id]
	dec	rsi			; 1-based -> 0-based
	call	vec_get
	return
endp

; bfa_ty_u1(module) -> eax = the type id for `u1`, interning it if this is
; the first request. Called often enough (every cmp/fcmp/overflow-predicate
; result) to deserve its own name instead of five inline arguments at each
; call site.
proc bfa_ty_u1, module
	locals
	endl
	mov	rdi, [module]
	mov	esi, BFA_TK_U
	xor	rdx, rdx
	mov	ecx, 1
	xor	r8, r8
	call	bfa_type_intern
	return
endp

; bfa_ty_ptr(module) -> eax = the type id for `ptr` (width fixed at 64 --
; this backend is x86-64 only, spec section 9.5 has already fixed
; `--hospes` by the time any IR reaches a backend).
proc bfa_ty_ptr, module
	locals
	endl
	mov	rdi, [module]
	mov	esi, BFA_TK_PTR
	xor	rdx, rdx
	mov	ecx, 64
	xor	r8, r8
	call	bfa_type_intern
	return
endp

; bfa_value_type(func, valueid) -> eax = the type id this value, once
; computed, actually HOLDS -- which is `ty` for almost every opcode, except
; the three families docs/design/ssa-ir.md section 2.3's table marks
; "-> u1" (cmp, fcmp, the overflow predicates), where `ty` instead records
; the OPERAND type (needed to print/emit the line) and the produced value's
; own type is always u1. `module` is read off the func's own... no, this
; proc is not handed a module pointer; callers needing u1's type id already
; have one cached (see emit.inc) -- this proc returns BFA_TK_U1_MARKER (a
; value no real type id can equal, since real ids are 1-based Vec indices
; and this is 0) and lets the caller substitute its cached u1 id, rather
; than threading a module pointer through a hot path for one constant.
BFA_VALUE_TYPE_IS_U1 = 0
proc bfa_value_type, func, valueid
	locals
	endl
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	rsi, [valueid]
	dec	rsi
	call	vec_get			; rax = BfaInst*
	movzx	ecx, word [rax + BfaInst.op]
	cmp	ecx, BFA_OP_CMP
	je	.is_u1
	cmp	ecx, BFA_OP_FCMP
	je	.is_u1
	cmp	ecx, BFA_OP_ADDOV
	je	.is_u1
	cmp	ecx, BFA_OP_SUBOV
	je	.is_u1
	cmp	ecx, BFA_OP_MULOV
	je	.is_u1
	mov	eax, [rax + BfaInst.ty]
	return
  .is_u1:
	mov	eax, BFA_VALUE_TYPE_IS_U1
	return
endp

; bfa_type_canon(module, ty, value) -> rax = `value` in the CANONICAL FORM
; of type `ty` (docs/design/ssa-ir.md section 2.2): for `uN` with N < 64
; bits N..63 cleared, for `iN` with N < 64 bits N..63 copies of bit N-1,
; and for anything else -- a 64-bit integer, `ptr`, a float's bits --
; `value` unchanged. A compile-time mirror of the `shl 64-N` / `shr`|`sar`
; pair emit.inc writes after a narrow operation, so a caller holding a
; constant asks `bfa_type_canon(...) == value` to learn whether it is a
; value of `ty` at all: the verifier (rule 1, for `iconst` and inline
; immediates) and the emitter (which the driver reaches WITHOUT the
; verifier, driver/run.inc) both do.
proc bfa_type_canon, module, ty, value
	locals
	endl
	mov	rdi, [module]
	mov	esi, [ty]
	call	bfa_type_ptr
	movzx	edx, byte [rax + BfaType.kind]
	movzx	ecx, word [rax + BfaType.width]
	mov	rax, [value]
	cmp	edx, BFA_TK_U
	je	.isint
	cmp	edx, BFA_TK_I
	jne	.done
  .isint:
	neg	ecx
	add	ecx, 64			; cl = 64 - N
	jle	.done			; N = 64: every bit pattern is a value
	shl	rax, cl
	cmp	edx, BFA_TK_I
	je	.signed
	shr	rax, cl
	return
  .signed:
	sar	rax, cl
  .done:
	return
endp

; bfa_func_new(module) -> rax = pointer to a freshly arena-allocated, zeroed
; BfaFunc, already appended to module.funcs. Caller fills in name/sig/numeri/
; attrs/span and then builds blocks/insts through bfa_block_new/bfa_inst_push.
proc bfa_func_new, module
	uses	rbx, r12, r13
	locals
	endl
	mov	rbx, [module]
	mov	r12, [rbx + BfaModule.arena]
	mov	rdi, r12
	mov	rsi, sizeof.BfaFunc
	call	arena_alloc
	mov	r13, rax
	; zero it (arena memory is not guaranteed pre-zeroed by anything in
	; this file's contract -- rt/arena.inc's header makes no such promise)
	mov	rdi, r13
	mov	rcx, sizeof.BfaFunc
	xor	rax, rax
	cld
	rep	stosb

	mov	rdi, r12
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[r13 + BfaFunc.insts], rax
	mov	rdi, rax
	mov	rsi, r12
	mov	rdx, sizeof.BfaInst
	xor	rcx, rcx
	call	vec_init

	mov	rdi, r12
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[r13 + BfaFunc.blocks], rax
	mov	rdi, rax
	mov	rsi, r12
	mov	rdx, sizeof.BfaBlock
	xor	rcx, rcx
	call	vec_init

	mov	rdi, r12
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[r13 + BfaFunc.edges], rax
	mov	rdi, rax
	mov	rsi, r12
	mov	rdx, sizeof.BfaEdge
	xor	rcx, rcx
	call	vec_init

	mov	rdi, r12
	mov	rsi, sizeof.Vec
	call	arena_alloc
	mov	[r13 + BfaFunc.extra], rax
	mov	rdi, rax
	mov	rsi, r12
	mov	rdx, 4
	xor	rcx, rcx
	call	vec_init

	mov	rdi, [rbx + BfaModule.funcs]
	call	vec_push		; rax = new slot (dq) in module.funcs
	mov	[rax], r13
	mov	rax, r13
	return
endp

; bfa_block_new(func) -> eax = 1-based internal block id, zeroed, unsealed.
proc bfa_block_new, func
	locals
	endl
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.blocks]
	call	vec_push		; rax = pointer to the new BfaBlock
	mov	rcx, sizeof.BfaBlock
	push	rax
	mov	rdi, rax
	xor	rax, rax
	cld
	rep	stosb
	pop	rax			; (rcx already ran out; rax still valid --
					; rep stosb only touches rdi/rcx/al)
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.blocks]
	mov	rax, [rdi + Vec.len]	; 1-based id = length after push
	return
endp

; bfa_block_ptr(func, blockid) -> rax = pointer to the BfaBlock.
proc bfa_block_ptr, func, blockid
	locals
	endl
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.blocks]
	mov	rsi, [blockid]
	dec	rsi
	call	vec_get
	return
endp

; bfa_inst_ptr(func, valueid) -> rax = pointer to the BfaInst.
proc bfa_inst_ptr, func, valueid
	locals
	endl
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	rsi, [valueid]
	dec	rsi
	call	vec_get
	return
endp

; bfa_inst_push(func, op, aux, ty, a, b) -> eax = new 1-based value id.
; Pure append -- does NOT link the instruction into any block's phi/body
; list (bfa_block_link_body/bfa_block_link_phi do that, separately, so a
; caller can create an instruction before deciding which list it belongs to
; -- never needed today, kept symmetric with section 2.1's own two-list
; design anyway). Span fields are set to 0 here; callers that have a real
; span (the parser always does) set it via bfa_inst_set_span right after.
proc bfa_inst_push, func, op, aux, ty, a, b
	locals
	endl
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.insts]
	call	vec_push		; rax = new BfaInst slot
	mov	cx, word [op]
	mov	[rax + BfaInst.op], cx
	mov	cx, word [aux]
	mov	[rax + BfaInst.aux], cx
	mov	ecx, [ty]
	mov	[rax + BfaInst.ty], ecx
	mov	ecx, [a]
	mov	[rax + BfaInst.a], ecx
	mov	ecx, [b]
	mov	[rax + BfaInst.b], ecx
	mov	dword [rax + BfaInst.nextp], 0
	mov	dword [rax + BfaInst.span_file], 0
	mov	dword [rax + BfaInst.span_start], 0
	mov	dword [rax + BfaInst.span_len], 0

	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.insts]
	mov	rax, [rdi + Vec.len]
	return
endp

; bfa_inst_set_span(func, valueid, file_id, soff, slen). Arguments named
; `soff` (not `start`) and `file_id` (not `file`): macros/proc.inc's
; reserved-name guard rejects `start` outright (docs/asm-conventions.md
; section 4.1); `file` alone collides with a native fasmg directive, found
; the same way -- caught here at assemble time, not at some unrelated later
; failure.
proc bfa_inst_set_span, func, valueid, file_id, soff, slen
	locals
	endl
	mov	rdi, [func]
	mov	rsi, [valueid]
	call	bfa_inst_ptr
	mov	ecx, [file_id]
	mov	[rax + BfaInst.span_file], ecx
	mov	ecx, [soff]
	mov	[rax + BfaInst.span_start], ecx
	mov	ecx, [slen]
	mov	[rax + BfaInst.span_len], ecx
	return
endp

; bfa_block_link_body(func, blockid, valueid) -- appends `valueid` to
; blockid's BODY list (O(1), tail-pointer append -- section 2.1 describes
; O(1) PREPEND for late phis; a body instruction is always appended in text
; order instead, since nothing here ever inserts one out of order).
proc bfa_block_link_body, func, blockid, valueid
	locals
	endl
	mov	rdi, [func]
	mov	rsi, [blockid]
	call	bfa_block_ptr		; rax = BfaBlock*
	mov	ecx, [rax + BfaBlock.last]
	test	ecx, ecx
	jnz	.have_tail
	mov	ecx, [valueid]
	mov	[rax + BfaBlock.first], ecx
	mov	[rax + BfaBlock.last], ecx
	return
  .have_tail:
	push	rax
	mov	rdi, [func]
	mov	esi, ecx
	call	bfa_inst_ptr		; rax = old tail BfaInst*
	mov	ecx, [valueid]
	mov	[rax + BfaInst.nextp], ecx
	pop	rax
	mov	[rax + BfaBlock.last], ecx
	return
endp

; bfa_block_link_phi(func, blockid, valueid) -- same as bfa_block_link_body
; but for the PHI list. Appends in text order; section 2.1's O(1)-prepend
; case is a builder concern (Braun's `addPhiOperands`, on the fly) this
; file's text-only parser never exercises -- hand-written IR already states
; its phis in final form, so there is nothing to prepend during a pass this
; tool makes only once.
proc bfa_block_link_phi, func, blockid, valueid
	locals
	endl
	mov	rdi, [func]
	mov	rsi, [blockid]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.last_phi]
	test	ecx, ecx
	jnz	.have_tail
	mov	ecx, [valueid]
	mov	[rax + BfaBlock.first_phi], ecx
	mov	[rax + BfaBlock.last_phi], ecx
	return
  .have_tail:
	push	rax
	mov	rdi, [func]
	mov	esi, ecx
	call	bfa_inst_ptr
	mov	ecx, [valueid]
	mov	[rax + BfaInst.nextp], ecx
	pop	rax
	mov	[rax + BfaBlock.last_phi], ecx
	return
endp

; bfa_extra_push(func, val) -> eax = the index `val` was stored at. Argument
; named `val`, not `word`: `word` is a native fasmg/x86 size-override keyword
; (`word [x]`) -- not risked here, per the same reserved-name discipline that
; caught `align`/`start` above in this file.
proc bfa_extra_push, func, val
	locals
	endl
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.extra]
	call	vec_push
	mov	ecx, [val]
	mov	[rax], ecx
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.extra]
	mov	rax, [rdi + Vec.len]
	dec	eax			; index, not length
	return
endp

; bfa_extra_get(func, index) -> eax = the word.
proc bfa_extra_get, func, index
	locals
	endl
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.extra]
	mov	rsi, [index]
	call	vec_get
	mov	eax, [rax]
	return
endp

; bfa_extra_set(func, index, val) -- used only by the phi forward-reference
; fixup pass (parse.inc): every other writer of `extra` appends once and
; never revisits.
proc bfa_extra_set, func, index, val
	locals
	endl
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.extra]
	mov	rsi, [index]
	call	vec_get
	mov	ecx, [val]
	mov	[rax], ecx
	return
endp

; ============================================================================
; BUILDERS ADDED FOR THE LOWERING AND THE VERIFIER
; (docs/design/lowering.md section 5's request list, and verify.inc's
; FINDING 1 and FINDING 2). Everything above this line predates them and is
; unchanged; everything below exists because a named consumer asked for it,
; not speculatively.
; ============================================================================

; ---- the `nop` opcode, named at last -----------------------------------
; docs/design/ssa-ir.md section 2.1: "A removed instruction becomes `nop`
; with `a` = its replacement; reads follow the chain", and section 3 makes
; "no `nop`" the verifier's rule 9. Until now no BFA_OP_* value stood for
; it: verify.inc INFERRED raw `Inst.op == 0` (its own FINDING 2) and
; lowering.md's finding 10 seconded that the constant was missing. 0 is the
; right value and not a new one -- print.inc's op-shape table already
; documents index 0 as "never a real opcode", and every BfaInst this file
; hands out is zeroed or op-assigned, so `op == 0` already means "no real
; opcode here". Naming it changes no bytes; it removes the inference.
;
; There is deliberately NO `nop` mnemonic in parse.inc's optab: section 2.11
; gives no text for one, a `nop` may only be produced by an
; instruction-removal pass, and a text form would let hand-written IR spell
; something the verifier exists to reject.
BFA_OP_NOP = 0

; ============================================================================
; predecessor edges -- docs/design/ssa-ir.md section 2.4
; ============================================================================

; bfa_edge_push(func, pfrom, pto) -> eax = the new 1-based edge index.
; Appends `pfrom` to `pto`'s predecessor list: allocates a BfaEdge in
; func.edges and links it onto BfaBlock.first_pred/last_pred, so the list
; is in THE ORDER TERMINATORS WERE EMITTED -- which is section 2.4's
; definition of predecessor order and therefore of phi operand order, and
; which is NOT ascending block index in general (verify.inc's header
; argues that at length; lowering.md finding 11 is the same point from the
; other side).
;
; ARGUMENT ORDER. This is lowering.md section 5's `bfa_edge_push(func,
; block, pred)` with the two block ids in the opposite order: (from, to),
; edge direction, so a call site reads the way the CFG edge does --
; `bfa_edge_push(f, cur, target)` at the site that emits `jmp target`.
; Recorded here rather than silently chosen, because a caller written
; against lowering.md's spelling and this proc's order would build a
; REVERSED CFG that still verifies on a symmetric fixture. Named
; `pfrom`/`pto` because a bare `to` is one keystroke from a fasmg
; size/operator token and `from` is not obviously safe either
; (docs/asm-conventions.md section 4.1).
;
; Edge indices are 1-BASED (0 = end of list) for the same reason value and
; block ids are: 0 has to stay a free sentinel in `first_pred`/`nextp`.
proc bfa_edge_push, func, pfrom, pto
	uses	rbx, r12
	locals
		slot eidx, dd
	endl
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.edges]
	call	vec_push		; rax = the new BfaEdge slot
	mov	ecx, [pfrom]
	mov	[rax + BfaEdge.pred], ecx
	mov	dword [rax + BfaEdge.nextp], 0

	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.edges]
	mov	rax, [rdi + Vec.len]	; 1-based index = length after push
	mov	[eidx], eax

	mov	rdi, [func]
	mov	rsi, [pto]
	call	bfa_block_ptr
	mov	rbx, rax
	mov	r12d, [rbx + BfaBlock.last_pred]
	test	r12d, r12d
	jnz	.have_tail
	mov	eax, [eidx]
	mov	[rbx + BfaBlock.first_pred], eax
	mov	[rbx + BfaBlock.last_pred], eax
	mov	eax, [eidx]
	return
  .have_tail:
	; the old tail edge, re-fetched through vec_get because the push above
	; may have moved func.edges' backing block (rt/vec.inc: growth moves
	; the data, never the Vec header -- ir.inc's own header states the
	; rule and this is the one place in this file that could violate it).
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.edges]
	mov	esi, r12d
	dec	esi
	call	vec_get
	mov	ecx, [eidx]
	mov	[rax + BfaEdge.nextp], ecx
	mov	eax, [eidx]
	mov	[rbx + BfaBlock.last_pred], eax
	return
endp

; bfa_edge_count(func, blockid) -> eax = how many predecessor edges the
; block has. A walk of the same list bfa_edge_push builds; used by rule 3
; (one phi operand per predecessor) and by anything sizing a phi's `extra`
; range before filling it.
proc bfa_edge_count, func, blockid
	uses	rbx
	locals
		slot cnt, dd
		slot e, dd
	endl
	mov	rdi, [func]
	mov	rsi, [blockid]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first_pred]
	mov	[e], ecx
	mov	dword [cnt], 0
  .loop:
	cmp	dword [e], 0
	je	.done
	mov	eax, [cnt]
	inc	eax
	mov	[cnt], eax
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.edges]
	mov	esi, [e]
	dec	esi
	call	vec_get
	mov	ecx, [rax + BfaEdge.nextp]
	mov	[e], ecx
	jmp	.loop
  .done:
	mov	eax, [cnt]
	return
endp

; bfa_edge_nth(func, blockid, index) -> eax = the 0-based `index`-th
; predecessor BLOCK id, or 0 if the list is shorter than that. Walks; the
; list is a handful of entries in every function this backend has seen and
; a reader that wants all of them should walk it once itself.
proc bfa_edge_nth, func, blockid, index
	locals
		slot i, dd
		slot e, dd
	endl
	mov	rdi, [func]
	mov	rsi, [blockid]
	call	bfa_block_ptr
	mov	ecx, [rax + BfaBlock.first_pred]
	mov	[e], ecx
	mov	dword [i], 0
  .loop:
	cmp	dword [e], 0
	je	.none
	mov	rdi, [func]
	mov	rdi, [rdi + BfaFunc.edges]
	mov	esi, [e]
	dec	esi
	call	vec_get
	mov	ecx, [rax + BfaEdge.pred]
	mov	edx, [rax + BfaEdge.nextp]
	mov	eax, [i]
	cmp	eax, [index]
	je	.hit
	mov	[e], edx
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.loop
  .hit:
	mov	eax, ecx
	return
  .none:
	xor	eax, eax
	return
endp

; ============================================================================
; phi prepend -- docs/design/ssa-ir.md section 2.1's O(1) case
; ============================================================================

; bfa_block_prepend_phi(func, blockid, valueid) -- puts `valueid` at the
; FRONT of the block's phi list, O(1), nothing moves. This is the operation
; section 2.1 describes ("a late phi is prepended in O(1)") and the one
; Braun's unsealed-block read performs; `bfa_block_link_phi`, just above,
; APPENDS and is what the text parser wants, since hand-written IR already
; states its phis in final order. Both exist because they are different
; operations, not two spellings of one.
;
; The prepended instruction must not already be on any list: its `nextp` is
; overwritten here.
proc bfa_block_prepend_phi, func, blockid, valueid
	uses	rbx
	locals
	endl
	mov	rdi, [func]
	mov	rsi, [blockid]
	call	bfa_block_ptr
	mov	rbx, rax
	mov	rdi, [func]
	mov	esi, [valueid]
	call	bfa_inst_ptr
	mov	ecx, [rbx + BfaBlock.first_phi]
	mov	[rax + BfaInst.nextp], ecx
	mov	ecx, [valueid]
	mov	[rbx + BfaBlock.first_phi], ecx
	cmp	dword [rbx + BfaBlock.last_phi], 0
	jne	.have_tail
	mov	[rbx + BfaBlock.last_phi], ecx
  .have_tail:
	return
endp

; ============================================================================
; signatures and globals -- the two module-level tables parse.inc built
; inline (lowering.md section 5: "a second writer should not repeat" that)
; ============================================================================

; bfa_sig_new(module, retty, ptypes, pn) -> eax = the 0-based signature
; index, which is what BfaFunc.sig holds. `retty` is a type id or 0 for
; void; `ptypes` points at `pn` consecutive 32-bit type ids, copied into
; Module.sig_extra in order. Named in ir.inc's BFA_TK_FN comment long
; before it existed; signatures are still never de-duplicated (that
; comment's [UNIMPLEMENTED] stands -- nothing builds `fn.S` values yet).
proc bfa_sig_new, module, retty, ptypes, pn
	uses	rbx, r12, r13
	locals
		slot base, dd
		slot i, dd
	endl
	mov	rbx, [module]
	mov	rdi, [rbx + BfaModule.sig_extra]
	mov	rax, [rdi + Vec.len]
	mov	[base], eax

	mov	dword [i], 0
  .ploop:
	mov	eax, [i]
	cmp	eax, [pn]
	jge	.pdone
	mov	rdi, [rbx + BfaModule.sig_extra]
	call	vec_push
	mov	rcx, [ptypes]
	mov	edx, [i]
	mov	ecx, [rcx + rdx*4]
	mov	[rax], ecx
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.ploop
  .pdone:

	mov	rdi, [rbx + BfaModule.sigs]
	call	vec_push
	mov	ecx, [retty]
	mov	[rax + BfaSig.ret_ty], ecx
	mov	ecx, [base]
	mov	[rax + BfaSig.p_first], ecx
	mov	ecx, [pn]
	mov	[rax + BfaSig.p_count], ecx

	mov	rdi, [rbx + BfaModule.sigs]
	mov	rax, [rdi + Vec.len]
	dec	eax			; index, not length
	return
endp

; bfa_global_new(module, gsize, galgn, gbytes, gn) -> eax = the new
; global's id. Ids are assigned DENSELY FROM 0 in creation order, which is
; exactly the rule parse.inc's `data $N` line already enforces ("out-of-
; sequence global $N" if `N` is not the current globals length) -- so a
; module built through this proc and a module parsed from text agree on
; ids without either knowing about the other. `gbytes`/`gn` are the initial
; bytes, copied into Module.gdata; `gn` may be less than `gsize` (a global
; whose tail is zero-filled), and no check couples the two -- section 2.11
; prints size and bytes as independent fields.
proc bfa_global_new, module, gsize, galgn, gbytes, gn
	uses	rbx
	locals
		slot doff, dd
		slot gid, dd
		slot i, dd
	endl
	mov	rbx, [module]
	mov	rdi, [rbx + BfaModule.gdata]
	mov	rax, [rdi + Vec.len]
	mov	[doff], eax
	mov	rdi, [rbx + BfaModule.globals]
	mov	rax, [rdi + Vec.len]
	mov	[gid], eax

	mov	dword [i], 0
  .bloop:
	mov	eax, [i]
	cmp	eax, [gn]
	jge	.bdone
	mov	rdi, [rbx + BfaModule.gdata]
	call	vec_push
	mov	rcx, [gbytes]
	mov	edx, [i]
	movzx	ecx, byte [rcx + rdx]
	mov	[rax], cl
	mov	eax, [i]
	inc	eax
	mov	[i], eax
	jmp	.bloop
  .bdone:

	mov	rdi, [rbx + BfaModule.globals]
	call	vec_push
	mov	ecx, [gid]
	mov	[rax + BfaGlobal.id], ecx
	mov	ecx, [gsize]
	mov	[rax + BfaGlobal.size], ecx
	mov	ecx, [galgn]
	mov	[rax + BfaGlobal.algn], ecx
	mov	ecx, [doff]
	mov	[rax + BfaGlobal.data_off], ecx
	mov	ecx, [gn]
	mov	[rax + BfaGlobal.data_len], ecx
	mov	eax, [gid]
	return
endp

; ============================================================================
; the operand-slot iterator -- lowering.md section 5's last request
; ============================================================================
;
; WHY A SECOND TABLE RATHER THAN print.inc's SHAPE TABLE. parse.inc,
; print.inc and verify.inc each carry their own switch over BFA_SH_*
; deliberately (each file's header says so), and print.inc sits ABOVE this
; file in the include chain, so reading its table from here would invert
; the chain ir.inc's header describes. The table below is keyed on the
; OPCODE directly and answers exactly one question -- which of an
; instruction's slots hold a VALUE ID -- which is less than a shape and
; strictly easier to keep true.
;
; ONE BYTE PER OPCODE:
;   bits 0-3  fixed extra mask: bit i set iff extra[base + i] is a value id
;   bits 4-5  variadic kind: 0 fixed, 1 phi, 2 call, 3 callind
;   bit 6     Inst.a is a value id
;   bit 7     Inst.b is a value id
; An op with all four fields zero has no value operands at all.
BFA_OPD_A          = 0x40
BFA_OPD_B          = 0x80
BFA_OPD_VAR_PHI    = 0x10	; extra = (bP %v)*: ODD indices are values
BFA_OPD_VAR_CALL   = 0x20	; extra = callee, args...: indices >= 1
BFA_OPD_VAR_CALIND = 0x30	; extra = %fp, args...: every index
BFA_OPD_VAR_MASK   = 0x30

; Operand SLOT CODES, the second return of bfa_inst_operand_next and the
; address bfa_inst_operand_set takes:
;   0                      Inst.a
;   1                      Inst.b
;   2 + <extra pool index> that word of func.extra
BFA_OSLOT_A       = 0
BFA_OSLOT_B       = 1
BFA_OSLOT_EXTRA0  = 2

	jmp	__bfa_opd_tab_end
__bfa_opd_tab:
	db	0				; 0  (nop -- no operands to walk;
						;     its `a` is a REPLACEMENT id,
						;     not a use, per section 2.1)
	db	BFA_OPD_A or BFA_OPD_B		; 1  ADD
	db	BFA_OPD_A or BFA_OPD_B		; 2  SUB
	db	BFA_OPD_A or BFA_OPD_B		; 3  MUL
	db	BFA_OPD_A or BFA_OPD_B		; 4  DIV
	db	BFA_OPD_A or BFA_OPD_B		; 5  REM
	db	BFA_OPD_A or BFA_OPD_B		; 6  ADDW
	db	BFA_OPD_A or BFA_OPD_B		; 7  SUBW
	db	BFA_OPD_A or BFA_OPD_B		; 8  MULW
	db	BFA_OPD_A or BFA_OPD_B		; 9  ADDS
	db	BFA_OPD_A or BFA_OPD_B		; 10 SUBS
	db	BFA_OPD_A or BFA_OPD_B		; 11 MULS
	db	BFA_OPD_A or BFA_OPD_B		; 12 ADDOV
	db	BFA_OPD_A or BFA_OPD_B		; 13 SUBOV
	db	BFA_OPD_A or BFA_OPD_B		; 14 MULOV
	db	BFA_OPD_A or BFA_OPD_B		; 15 AND
	db	BFA_OPD_A or BFA_OPD_B		; 16 OR
	db	BFA_OPD_A or BFA_OPD_B		; 17 XOR
	db	BFA_OPD_A or BFA_OPD_B		; 18 SHL
	db	BFA_OPD_A or BFA_OPD_B		; 19 SHR
	db	BFA_OPD_A or BFA_OPD_B		; 20 CMP
	db	BFA_OPD_A			; 21 ZEXT
	db	BFA_OPD_A			; 22 SEXT
	db	BFA_OPD_A			; 23 TRUNC
	db	BFA_OPD_A or BFA_OPD_B		; 24 FADD
	db	BFA_OPD_A or BFA_OPD_B		; 25 FSUB
	db	BFA_OPD_A or BFA_OPD_B		; 26 FMUL
	db	BFA_OPD_A or BFA_OPD_B		; 27 FDIV
	db	BFA_OPD_A			; 28 FNEG
	db	0x07				; 29 FMA      extra %a %b %c
	db	BFA_OPD_A or BFA_OPD_B		; 30 FCMP
	db	BFA_OPD_A			; 31 FEXT
	db	BFA_OPD_A			; 32 FTRUNC
	db	BFA_OPD_A			; 33 ITOF
	db	BFA_OPD_A			; 34 FTOI
	db	BFA_OPD_A			; 35 BITCAST
	db	0				; 36 REDINIT  a=shape b=w
	db	BFA_OPD_A or BFA_OPD_B		; 37 CONTRIB  %h %v
	db	BFA_OPD_A			; 38 REDFIN   %h
	db	0				; 39 SLOT     a=size b=align
	db	0x01				; 40 LOAD     extra %p off order
	db	0x09				; 41 STORE    extra %p off order %v
	db	0x01				; 42 LOADBITS extra %p byte bit
	db	0x09				; 43 STOREBITS
	db	0x06				; 44 COPY     extra n %d %s
	db	BFA_OPD_A			; 45 ADDR     a=%p b=off
	db	0x03				; 46 INDEX    extra %p %i stride
	db	0				; 47 GADDR    a=N
	db	BFA_OPD_A or BFA_OPD_B		; 48 CHK      %i %n
	db	BFA_OPD_A			; 49 RETAIN
	db	BFA_OPD_A			; 50 RELEASE
	db	BFA_OPD_VAR_CALL		; 51 CALL
	db	BFA_OPD_VAR_CALIND		; 52 CALLIND
	db	BFA_OPD_A			; 53 RET      a=0 when valueless
	db	0				; 54 JMP      a=block id
	db	0x01				; 55 BR       extra %c bT bF
	db	0				; 56 TRAP
	db	BFA_OPD_VAR_PHI			; 57 PHI
	db	0				; 58 PARAM
	db	0				; 59 ICONST   extra lo hi
	db	0				; 60 FCONST   extra lo hi
	db	BFA_OPD_A or BFA_OPD_B		; 61 VADD
	db	BFA_OPD_A or BFA_OPD_B		; 62 VSUB
	db	BFA_OPD_A or BFA_OPD_B		; 63 VMUL
	db	BFA_OPD_A or BFA_OPD_B		; 64 VDIV
__bfa_opd_tab_end:

; __bfa_opd_of(func, valueid) -> eax = the descriptor byte for this
; instruction, with the two INLINE bits cleared for any operand the
; instruction's own `aux` marks as a bare immediate rather than a value.
; parse.inc's `__bfa_val_or_imm` sets aux bit 0x100 for `a` and 0x200 for
; `b` on the bin/cmp shapes -- the only shapes that can carry an immediate
; where a value id would otherwise be -- and emit.inc reads exactly those
; two bits for the same purpose. No op whose descriptor sets an inline bit
; uses `aux` for anything else (`trap` and `redinit` do use `aux` as an
; intern id, which can exceed 0x100, and both have descriptor 0), so
; masking unconditionally is safe; that argument is the reason it is safe,
; and it must be rechecked if a future op both takes a value in `a`/`b`
; and stores something large in `aux`.
proc __bfa_opd_of, func, valueid
	locals
		slot d, dd
	endl
	mov	rdi, [func]
	mov	rsi, [valueid]
	call	bfa_inst_ptr
	movzx	ecx, word [rax + BfaInst.op]
	movzx	edx, word [rax + BfaInst.aux]
	push	rdx
	lea	rdx, [__bfa_opd_tab]
	movzx	eax, byte [rdx + rcx]
	pop	rdx
	test	edx, 0x100
	jz	.nota
	and	eax, not BFA_OPD_A
  .nota:
	test	edx, 0x200
	jz	.notb
	and	eax, not BFA_OPD_B
  .notb:
	return
endp

; bfa_inst_operand_slots(func, valueid) -> eax = the descriptor byte
; (BFA_OPD_*, immediates already masked out), ecx = the extra-pool base
; index, edx = the extra-pool word count. `ecx`/`edx` are 0 for an
; instruction whose operands are entirely inline. This is
; lowering.md section 5's `(inline mask, extra start, extra count)`, with
; the inline mask in the top two bits of `eax` exactly as the table above
; lays it out; callers that just want to WALK the value operands should use
; bfa_inst_operand_next instead of decoding this themselves.
proc bfa_inst_operand_slots, func, valueid
	uses	rbx
	locals
		slot d, dd
	endl
	mov	rdi, [func]
	mov	rsi, [valueid]
	call	__bfa_opd_of
	mov	[d], eax
	test	eax, 0x3F		; any extra-pool slots at all?
	jnz	.hasextra
	mov	eax, [d]
	xor	ecx, ecx
	xor	edx, edx
	return
  .hasextra:
	mov	rdi, [func]
	mov	rsi, [valueid]
	call	bfa_inst_ptr
	mov	ecx, [rax + BfaInst.a]
	mov	edx, [rax + BfaInst.b]
	mov	eax, [d]
	return
endp

; __bfa_opd_extra_is_val(d, i) -> eax = 1 iff extra slot `i` (relative to
; the base) of an instruction with descriptor `d` holds a value id.
proc __bfa_opd_extra_is_val, d, i
	locals
	endl
	mov	eax, [d]
	and	eax, BFA_OPD_VAR_MASK
	cmp	eax, BFA_OPD_VAR_PHI
	je	.phi
	cmp	eax, BFA_OPD_VAR_CALL
	je	.call
	cmp	eax, BFA_OPD_VAR_CALIND
	je	.callind
	; fixed: the low nibble is a direct mask over slots 0..3
	mov	ecx, [i]
	cmp	ecx, 4
	jge	.no
	mov	eax, [d]
	and	eax, 0x0F
	shr	eax, cl
	and	eax, 1
	return
  .phi:					; (bP %v) pairs: the odd words
	mov	eax, [i]
	and	eax, 1
	return
  .call:				; extra[0] is the callee index
	mov	eax, [i]
	test	eax, eax
	jz	.no
	mov	eax, 1
	return
  .callind:				; extra[0] is the function POINTER value
	mov	eax, 1
	return
  .no:
	xor	eax, eax
	return
endp

; bfa_inst_operand_next(func, valueid, cursor) -> eax = the cursor to pass
; next (0 = no more operands), ecx = the slot code of the operand just
; found (BFA_OSLOT_*), edx = the value id currently in that slot.
;
; Start with cursor = 1. The walk order is: Inst.a, Inst.b, then the extra
; range in ascending index -- which is the order every operand of every
; shape is WRITTEN in (parse.inc pushes in text order), so a canonicalising
; pass that rewrites in walk order rewrites in text order.
;
; A `ret` with no value has `a` = 0 and yields no operands at all; that is
; the one shape whose operand count is not a function of its opcode alone,
; and it is handled here rather than by every caller.
proc bfa_inst_operand_next, func, valueid, cursor
	uses	rbx, r12, r13
	locals
		slot d, dd
		slot cur, dd
		slot base, dd
		slot cnt, dd
		slot idx, dd
	endl
	mov	rdi, [func]
	mov	rsi, [valueid]
	call	bfa_inst_operand_slots
	mov	[d], eax
	mov	[base], ecx
	mov	[cnt], edx
	mov	eax, [cursor]
	mov	[cur], eax

  .again:
	cmp	dword [cur], 1
	jne	.tryb
	mov	eax, [d]
	test	eax, BFA_OPD_A
	jz	.skipa
	mov	rdi, [func]
	mov	rsi, [valueid]
	call	bfa_inst_ptr
	mov	edx, [rax + BfaInst.a]
	test	edx, edx		; `ret` with no value
	jz	.skipa
	mov	eax, 2
	mov	ecx, BFA_OSLOT_A
	return
  .skipa:
	mov	dword [cur], 2
  .tryb:
	cmp	dword [cur], 2
	jne	.tryextra
	mov	eax, [d]
	test	eax, BFA_OPD_B
	jz	.skipb
	mov	rdi, [func]
	mov	rsi, [valueid]
	call	bfa_inst_ptr
	mov	edx, [rax + BfaInst.b]
	mov	eax, 3
	mov	ecx, BFA_OSLOT_B
	return
  .skipb:
	mov	dword [cur], 3
  .tryextra:
	mov	eax, [cur]
	sub	eax, 3
	mov	[idx], eax
	cmp	eax, [cnt]
	jge	.done
	mov	edi, [d]
	mov	esi, [idx]
	call	__bfa_opd_extra_is_val
	test	eax, eax
	jz	.skipextra
	mov	rdi, [func]
	mov	eax, [base]
	add	eax, [idx]
	mov	esi, eax
	push	rax
	call	bfa_extra_get
	mov	edx, eax
	pop	rax
	mov	ecx, eax
	add	ecx, BFA_OSLOT_EXTRA0	; slot code = 2 + absolute extra index
	mov	eax, [cur]
	inc	eax
	return
  .skipextra:
	mov	eax, [cur]
	inc	eax
	mov	[cur], eax
	jmp	.tryextra
  .done:
	xor	eax, eax
	xor	ecx, ecx
	xor	edx, edx
	return
endp

; bfa_inst_operand_set(func, valueid, oslot, val) -- writes `val` into the
; slot bfa_inst_operand_next reported. The write-back half of the iterator:
; section 2.5's canonicalisation ("a final canonicalisation resolves every
; operand's chain, so no backend sees a `nop`") is exactly a walk plus this.
proc bfa_inst_operand_set, func, valueid, oslot, val
	locals
	endl
	cmp	dword [oslot], BFA_OSLOT_A
	jne	.nota
	mov	rdi, [func]
	mov	rsi, [valueid]
	call	bfa_inst_ptr
	mov	ecx, [val]
	mov	[rax + BfaInst.a], ecx
	return
  .nota:
	cmp	dword [oslot], BFA_OSLOT_B
	jne	.notb
	mov	rdi, [func]
	mov	rsi, [valueid]
	call	bfa_inst_ptr
	mov	ecx, [val]
	mov	[rax + BfaInst.b], ecx
	return
  .notb:
	mov	rdi, [func]
	mov	esi, [oslot]
	sub	esi, BFA_OSLOT_EXTRA0
	mov	edx, [val]
	call	bfa_extra_set
	return
endp
