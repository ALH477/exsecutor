; compiler/x86_64/prelude/interface.inc
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
; The prelude interface: what the checker resolves the prelude's names
; against. `docs/design/runtime.md` 2.2's table, assembled.
;
; WHY A TABLE AND NOT SOURCE. There is no source form for it. Spec 8.6 has no
; receiver syntax, no `Self`, no member declaration, and no way to declare
; `Mundus` (runtime.md finding 1, CHK finding 6), and spec 12 as amended says
; the prelude's names are "pre-seeded declarations in a scope OUTSIDE the
; module's", which a module's own declaration shadows rather than colliding
; with. This file is that seed. It is included by `checker/resolve/`, which
; does not exist yet, so PART B below is [UNTESTED] and provisional: the
; FACTS are part A's constants, which are machine-checked against the blob
; every time both are assembled together, and the serialization in part B is
; the checker agent's to accept or replace.
;
; THIS FILE IS NOT EMITTED. Only prelude.asm and prelude_data.asm are `file`'d
; into `exsc` and copied into `OUT` (runtime.md 2.1). This one is `include`d
; into `exsc` itself.
;
; RUNTIME.MD H4 -- TWO COPIES OF THE SAME LAYOUT. `ExsAmbitus`, `Scriptor`,
; `Lector` and `ExsObj` offsets appear in the blob and here, and the emitter never
; sees them (runtime.md 2.2), so this pair is the only place they can
; disagree. Each offset is stated ONCE below, with the blob symbol it
; mirrors named in its comment -- and, whenever prelude.asm has been
; assembled alongside this file (which tests/unit/prelude_scribe.asm does),
; the `assert` block at the end of part A checks every one of them against
; the blob's own constant. The duplication is therefore self-checking rather
; than merely documented.
;
; SAFE TO INCLUDE ANYWHERE. Part A defines assemble-time constants only and
; emits no bytes, so a fixture may include it inside or outside a segment.
; Part B emits the table and is silent unless the includer defines
; EXS_IFACE_EMIT_DECLS first.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md 4.3, 4.6, 4.7, 5.1, 5.2, 8.6, 12;
; docs/design/runtime.md 2.2, 2.3, 2.4, 2.5, H4, H6; docs/design/checker.md 2.8.
; -----------------------------------------------------------------------------

; =============================================================================
; PART A -- the facts
; =============================================================================

; ---- declaration kinds ------------------------------------------------------
; Mirrors compiler/x86_64/ast/kinds.inc's AST_D_* (another agent's tree; the
; numbers are restated rather than `include`d so this file stays standalone,
; and cross-asserted below when both are present).
EXS_IFACE_K_FN		= 1		; AST_D_FN
EXS_IFACE_K_PARAM	= 2		; AST_D_PARAM
EXS_IFACE_K_STRUCT	= 4		; AST_D_STRUCT
EXS_IFACE_K_FIELD	= 5		; AST_D_FIELD

; ---- capability atoms -------------------------------------------------------
; Spec 4.6 order; mirrors ast/kinds.inc's AST_CAP_*, 1-based.
EXS_IFACE_CAP_MUNDUS	= 1
EXS_IFACE_CAP_ALLOC	= 2
EXS_IFACE_CAP_AMBITUS	= 8
EXS_IFACE_CAP_NONE	= 0

; ---- IR-level type codes ----------------------------------------------------
; The signature encoding below is the IR's, not the surface language's: the
; checker seeds a surface `Decl`, and the LOWERING needs to know what the call
; actually looks like (IR 2.9's hidden pointers included).
EXS_IFACE_T_VOID	= 0
EXS_IFACE_T_PTR		= 1
EXS_IFACE_T_U8		= 2
EXS_IFACE_T_U64		= 3		; `mensura` on x86_64-linux (runtime.md 2.3)
EXS_IFACE_T_I32		= 4
EXS_IFACE_T_U16		= 5		; `lege_octeto`'s result: a byte, or 256

; ---- declaration flags ------------------------------------------------------
EXS_IFACE_F_PUBLICA	= 0x01		; every prelude name is public
EXS_IFACE_F_ASSOCIATA	= 0x02		; called as `Type.name(...)`, no receiver
EXS_IFACE_F_CAPBEARING	= 0x04		; spec 4.3: the type carries a capability
EXS_IFACE_F_APERTUM	= 0x08		; [OPEN]: this row's surface spelling is
					; not settled by any spec section yet

; ---- Scriptor -- runtime.md 2.4 ---------------------------------------------
; `structura Scriptor { a: ambitus, descriptor: i32 }`, capability-bearing
; with mark {ambitus} (spec 4.3). Concrete, not `interfacies`:
; examples/initium.exsc writes `s: Scriptor` as a parameter type, and spec 8.6
; decision 4 makes trait objects explicit `dyn Scriptor`, so under the grammar
; as it stands `s: Scriptor` IS a concrete type. It becomes an interface when
; `norma` exists (runtime.md 2.4, finding 9).
;
; Fields are packed, alignment 1 (spec 5.2 as amended) -- but this is a
; PRELUDE type whose layout the prelude fixes, and the blob reads
; `descriptor` at +8. Stated once here; mirrors prelude.asm's
; EXS_SCRIPTOR_DESCRIPTOR.
EXS_IFACE_SCRIPTOR_A		= 0	; mirrors prelude.asm EXS_SCRIPTOR_A
EXS_IFACE_SCRIPTOR_DESCRIPTOR	= 8	; mirrors prelude.asm EXS_SCRIPTOR_DESCRIPTOR
EXS_IFACE_SCRIPTOR_SIZE		= 16	; mirrors prelude.asm EXS_SCRIPTOR_SIZE
EXS_IFACE_SCRIPTOR_ALIGN	= 8

; ---- Lector -- the reader, `Scriptor`'s mirror ------------------------------
; `structura Lector { a: ambitus, descriptor: i32 }`, capability-bearing with
; mark {ambitus} (spec 4.3), concrete for `Scriptor`'s reason. Spec 4.6 puts
; standard INPUT in `ambitus` with the other two streams, and spec 3.3 gives
; `leg-` the supine `lect-`, so 3.4's `-or` agent noun of "read" is `Lector`
; -- the name 3.7's own derivation table prints. Same representation as
; `Scriptor`, byte for byte; mirrors prelude.asm's EXS_LECTOR_*.
;
; ITS FIELDS HAVE NO ROWS, and that is a decision. `Scriptor.a` and
; `Scriptor.descriptor` are rows 1 and 2 because a member is resolved by
; NAME through one pool (checker/types/prim.inc's `chk_ty_pre_names`), and a
; second `a` in that pool would be unreachable behind the first. Nothing in
; the language needs `l.descriptor`, the prelude reads these two fields
; through the constants below, and the layout facts a consumer needs -- size
; and alignment -- are stated here. If `l.a` is ever wanted, the pool needs a
; (row, name) lookup rather than a name lookup; that is the edit, and it is
; not made speculatively.
EXS_IFACE_LECTOR_A		= 0	; mirrors prelude.asm EXS_LECTOR_A
EXS_IFACE_LECTOR_DESCRIPTOR	= 8	; mirrors prelude.asm EXS_LECTOR_DESCRIPTOR
EXS_IFACE_LECTOR_SIZE		= 16	; mirrors prelude.asm EXS_LECTOR_SIZE
EXS_IFACE_LECTOR_ALIGN		= 8

; ---- ambitus -- the ExsAmbitus record, runtime.md 2.2 -----------------------
; NOT a surface type: `ambitus` is a capability atom and its carrier is one
; `ptr` (CHK 2.8). These offsets are here because the checker needs them for
; nothing and the LOWERING needs them for nothing either -- only the prelude
; reads this record. They are stated so that runtime.md H4's pair is complete
; and checkable, and so a future `a.introitus()` has one place to read.
EXS_IFACE_AMBITUS_IN	= 0		; mirrors prelude.asm EXS_AMBITUS_IN
EXS_IFACE_AMBITUS_OUT	= 4		; mirrors prelude.asm EXS_AMBITUS_OUT
EXS_IFACE_AMBITUS_ERR	= 8		; mirrors prelude.asm EXS_AMBITUS_ERR
EXS_IFACE_AMBITUS_ARGC	= 16		; mirrors prelude.asm EXS_AMBITUS_ARGC
EXS_IFACE_AMBITUS_ARGV	= 24		; mirrors prelude.asm EXS_AMBITUS_ARGV
EXS_IFACE_AMBITUS_ENVP	= 32		; mirrors prelude.asm EXS_AMBITUS_ENVP
EXS_IFACE_AMBITUS_SIZE	= 40		; mirrors prelude.asm EXS_AMBITUS_SIZE

; ---- textus -- runtime.md 2.3 ----------------------------------------------
; A two-word VALUE view: pointer and byte length, no header, no refcount, no
; destructor. Spec 5.1 as amended states this. An aggregate in the IR (IR 2.2
; has no 16-byte scalar), so a function returning one takes a hidden `ptr` to
; caller-owned storage.
EXS_IFACE_TEXTUS_PTR	= 0		; mirrors prelude.asm EXS_TEXTUS_PTR
EXS_IFACE_TEXTUS_LEN	= 8		; mirrors prelude.asm EXS_TEXTUS_LEN
EXS_IFACE_TEXTUS_SIZE	= 16		; mirrors prelude.asm EXS_TEXTUS_SIZE
EXS_IFACE_TEXTUS_ALIGN	= 8

; ---- ExsObj -- the ARC header, runtime.md 2.5 -------------------------------
; At the start of every `refero<T>` / `refero_communis<T>` allocation; payload
; at +16, so a 16-byte-aligned payload (an `acies<f64>`, say) is aligned for
; free. rc == 0 means UNDER DESTRUCTION (runtime.md H5: do not reuse 0).
EXS_IFACE_OBJ_RC	= 0		; mirrors prelude.asm EXS_OBJ_RC
EXS_IFACE_OBJ_DTOR	= 8		; mirrors prelude.asm EXS_OBJ_DTOR
EXS_IFACE_OBJ_HEADER	= 16		; mirrors prelude.asm EXS_OBJ_HEADER

; ---- ExsArena -- runtime.md 2.6 ---------------------------------------------
EXS_IFACE_ARENA_BASE	= 0		; mirrors prelude.asm EXS_ARENA_BASE
EXS_IFACE_ARENA_CUR	= 8		; mirrors prelude.asm EXS_ARENA_CUR
EXS_IFACE_ARENA_LIMIT	= 16		; mirrors prelude.asm EXS_ARENA_LIMIT
EXS_IFACE_ARENA_SIZE	= 24		; mirrors prelude.asm EXS_ARENA_SIZE

; ---- abort kinds ------------------------------------------------------------
; Not EXS-E codes. Spec 6.6 (as amended) owns the shape, runtime.md 2.5 the
; enum. Restated here so a checker or a conformance runner matching
; `abortus N` has one place to read the number from.
EXS_IFACE_ABORTUS_NUMERICUS	= 1
EXS_IFACE_ABORTUS_SATURATIO	= 2
EXS_IFACE_ABORTUS_RESURRECTIO	= 3
EXS_IFACE_ABORTUS_ARENA		= 4
EXS_IFACE_ABORTUS_TERMINUS	= 5	; spec 8.5's `dum ... terminus N` overrun

; ---- runtime.md H4, made checkable -----------------------------------------
; When prelude.asm has been assembled alongside this file, every offset above
; is checked against the blob's own constant. Two copies that cannot silently
; disagree.
if defined EXS_SCRIPTOR_A
	assert EXS_IFACE_SCRIPTOR_A = EXS_SCRIPTOR_A
	assert EXS_IFACE_SCRIPTOR_DESCRIPTOR = EXS_SCRIPTOR_DESCRIPTOR
	assert EXS_IFACE_SCRIPTOR_SIZE = EXS_SCRIPTOR_SIZE
	assert EXS_IFACE_LECTOR_A = EXS_LECTOR_A
	assert EXS_IFACE_LECTOR_DESCRIPTOR = EXS_LECTOR_DESCRIPTOR
	assert EXS_IFACE_LECTOR_SIZE = EXS_LECTOR_SIZE
	assert EXS_IFACE_AMBITUS_IN = EXS_AMBITUS_IN
	assert EXS_IFACE_AMBITUS_OUT = EXS_AMBITUS_OUT
	assert EXS_IFACE_AMBITUS_ERR = EXS_AMBITUS_ERR
	assert EXS_IFACE_AMBITUS_ARGC = EXS_AMBITUS_ARGC
	assert EXS_IFACE_AMBITUS_ARGV = EXS_AMBITUS_ARGV
	assert EXS_IFACE_AMBITUS_ENVP = EXS_AMBITUS_ENVP
	assert EXS_IFACE_AMBITUS_SIZE = EXS_AMBITUS_SIZE
	assert EXS_IFACE_TEXTUS_PTR = EXS_TEXTUS_PTR
	assert EXS_IFACE_TEXTUS_LEN = EXS_TEXTUS_LEN
	assert EXS_IFACE_TEXTUS_SIZE = EXS_TEXTUS_SIZE
	assert EXS_IFACE_OBJ_RC = EXS_OBJ_RC
	assert EXS_IFACE_OBJ_DTOR = EXS_OBJ_DTOR
	assert EXS_IFACE_OBJ_HEADER = EXS_OBJ_HEADER
	assert EXS_IFACE_ARENA_BASE = EXS_ARENA_BASE
	assert EXS_IFACE_ARENA_CUR = EXS_ARENA_CUR
	assert EXS_IFACE_ARENA_LIMIT = EXS_ARENA_LIMIT
	assert EXS_IFACE_ARENA_SIZE = EXS_ARENA_SIZE
	assert EXS_IFACE_ABORTUS_NUMERICUS = EXS_ABORTUS_NUMERICUS
	assert EXS_IFACE_ABORTUS_SATURATIO = EXS_ABORTUS_SATURATIO
	assert EXS_IFACE_ABORTUS_RESURRECTIO = EXS_ABORTUS_RESURRECTIO
	assert EXS_IFACE_ABORTUS_ARENA = EXS_ABORTUS_ARENA
	assert EXS_IFACE_ABORTUS_TERMINUS = EXS_ABORTUS_TERMINUS
end if

; The same guard against ast/kinds.inc, whenever the checker has included it.
if defined AST_D_FN
	assert EXS_IFACE_K_FN = AST_D_FN
	assert EXS_IFACE_K_PARAM = AST_D_PARAM
	assert EXS_IFACE_K_STRUCT = AST_D_STRUCT
	assert EXS_IFACE_K_FIELD = AST_D_FIELD
	assert EXS_IFACE_CAP_MUNDUS = AST_CAP_MUNDUS
	assert EXS_IFACE_CAP_ALLOC = AST_CAP_ALLOC
	assert EXS_IFACE_CAP_AMBITUS = AST_CAP_AMBITUS
end if

; =============================================================================
; PART B -- the pre-seeded declarations
; =============================================================================
; Emitted only when the includer defines EXS_IFACE_EMIT_DECLS, so that a
; fixture can take part A's constants without taking bytes it has no segment
; for.
;
; [UNTESTED] and provisional. `checker/resolve/` does not exist; when it does,
; its own `Decl` record wins and this serialization changes to match. What
; must NOT change is part A: those are the layout facts the blob enforces.
;
; RECORD (36 bytes, fixed width -- an index into the table is a decl id):
;   +0  u8   kind          EXS_IFACE_K_*
;   +1  u8   flags         EXS_IFACE_F_*
;   +2  u8   atom          EXS_IFACE_CAP_* for a capability-typed thing, else 0
;   +3  u8   reserved      0
;   +4  u32  name_off      byte offset into exsrt_iface_nomina
;   +8  u32  name_len
;   +12 u32  sym_off       byte offset of the PRELUDE SYMBOL, 0 = none
;   +16 u32  sym_len
;   +20 u32  parent        index of the owning decl, or EXS_IFACE_NULLUS
;   +24 u32  sig_off       byte offset into exsrt_iface_signa, or EXS_IFACE_NULLUS
;   +28 u32  extent        STRUCT: byte size. FIELD: byte offset. else 0
;   +32 u32  align         STRUCT/FIELD: alignment. else 0
;
; ORDER IS INSERTION ORDER and is part of the contract: a decl id is an index
; into this table, so permuting the rows renumbers every reference to them
; (CLAUDE.md, "Determinism is not optional").

EXS_IFACE_NULLUS	= 0xFFFFFFFF
EXS_IFACE_DECL_SIZE	= 36

; Row indices, so that a `parent` can be written by name.
EXS_IFACE_D_SCRIPTOR		= 0
EXS_IFACE_D_SCRIPTOR_A		= 1
EXS_IFACE_D_SCRIPTOR_DESCRIPTOR	= 2
EXS_IFACE_D_MUNDUS_AMBITUS	= 3
EXS_IFACE_D_AD_EXITUM		= 4
EXS_IFACE_D_SCRIBE		= 5
EXS_IFACE_D_ALLOC_NOVUM		= 6
EXS_IFACE_D_SCRIBE_OCTETUM	= 7	; APPENDED, not inserted next to row 5:
					; an insertion renumbers alloc (above)
EXS_IFACE_D_LECTOR		= 8	; APPENDED for the same reason: a decl id
EXS_IFACE_D_AB_INTROITU		= 9	; is an INDEX into the table below, so a
EXS_IFACE_D_LEGE_OCTETO		= 10	; row next to `Scriptor`'s would renumber
					; every reference to rows 1-7
EXS_IFACE_DECL_COUNT		= 11

if defined EXS_IFACE_EMIT_DECLS

; ---- the name pool ----------------------------------------------------------
; UTF-8, NFC, no separators; every reference is (offset, length). All ASCII
; today; spec 8.1's NFC rule makes byte comparison the whole of identity.
exsrt_iface_nomina:
  .Scriptor		= $ - exsrt_iface_nomina
	db 'Scriptor'
  .Scriptor_len		= $ - exsrt_iface_nomina - .Scriptor
  .a			= $ - exsrt_iface_nomina
	db 'a'
  .a_len		= $ - exsrt_iface_nomina - .a
  .descriptor		= $ - exsrt_iface_nomina
	db 'descriptor'
  .descriptor_len	= $ - exsrt_iface_nomina - .descriptor
  .ambitus		= $ - exsrt_iface_nomina
	db 'ambitus'
  .ambitus_len		= $ - exsrt_iface_nomina - .ambitus
  .ad_exitum		= $ - exsrt_iface_nomina
	db 'ad_exitum'
  .ad_exitum_len	= $ - exsrt_iface_nomina - .ad_exitum
  .scribe		= $ - exsrt_iface_nomina
	db 'scribe'
  .scribe_len		= $ - exsrt_iface_nomina - .scribe
  .alloc		= $ - exsrt_iface_nomina
	db 'alloc'
  .alloc_len		= $ - exsrt_iface_nomina - .alloc
  .scribe_octeto	= $ - exsrt_iface_nomina
	db 'scribe_octeto'
  .scribe_octeto_len	= $ - exsrt_iface_nomina - .scribe_octeto
  .Lector		= $ - exsrt_iface_nomina
	db 'Lector'
  .Lector_len		= $ - exsrt_iface_nomina - .Lector
  .ab_introitu		= $ - exsrt_iface_nomina
	db 'ab_introitu'
  .ab_introitu_len	= $ - exsrt_iface_nomina - .ab_introitu
  .lege_octeto		= $ - exsrt_iface_nomina
	db 'lege_octeto'
  .lege_octeto_len	= $ - exsrt_iface_nomina - .lege_octeto
  ; ---- prelude symbols. The label in the blob carries emit.inc's `bfausr_`
  ; prefix (runtime.md 2.2); the IR names the short form, and the emitter
  ; prints the prefix. These are the SHORT forms.
  .sym_mundus_ambitus	= $ - exsrt_iface_nomina
	db 'exsrt_mundus_ambitus'
  .sym_mundus_ambitus_len = $ - exsrt_iface_nomina - .sym_mundus_ambitus
  .sym_ad_exitum	= $ - exsrt_iface_nomina
	db 'exsrt_scriptor_ad_exitum'
  .sym_ad_exitum_len	= $ - exsrt_iface_nomina - .sym_ad_exitum
  .sym_scribe		= $ - exsrt_iface_nomina
	db 'exsrt_scriptor_scribe'
  .sym_scribe_len	= $ - exsrt_iface_nomina - .sym_scribe
  .sym_alloc_novum	= $ - exsrt_iface_nomina
	db 'exsrt_alloc_novum'
  .sym_alloc_novum_len	= $ - exsrt_iface_nomina - .sym_alloc_novum
  .sym_scribe_octeto	= $ - exsrt_iface_nomina
	db 'exsrt_scriptor_scribe_octeto'
  .sym_scribe_octeto_len = $ - exsrt_iface_nomina - .sym_scribe_octeto
  .sym_ab_introitu	= $ - exsrt_iface_nomina
	db 'exsrt_lector_ab_introitu'
  .sym_ab_introitu_len	= $ - exsrt_iface_nomina - .sym_ab_introitu
  .sym_lege_octeto	= $ - exsrt_iface_nomina
	db 'exsrt_lector_lege_octeto'
  .sym_lege_octeto_len	= $ - exsrt_iface_nomina - .sym_lege_octeto
EXS_IFACE_NOMINA_LEN = $ - exsrt_iface_nomina

; ---- signature pool ---------------------------------------------------------
; One record per signature: `db nparams, ret, param0, ...` in IR terms, so an
; aggregate return is already spelled as the hidden leading `ptr` IR 2.9
; passes and a `-> void`.
exsrt_iface_signa:
  .mundus_ambitus	= $ - exsrt_iface_signa
	db 1, EXS_IFACE_T_PTR, EXS_IFACE_T_PTR			; (ptr) -> ptr
  .ad_exitum		= $ - exsrt_iface_signa
	db 2, EXS_IFACE_T_VOID, EXS_IFACE_T_PTR, EXS_IFACE_T_PTR
								; (ptr ptr) -> void:
								; hidden return ptr first,
								; then `a`
  .scribe		= $ - exsrt_iface_signa
	db 2, EXS_IFACE_T_U64, EXS_IFACE_T_PTR, EXS_IFACE_T_PTR
								; (ptr ptr) -> u64:
								; both aggregates by ptr
  .alloc_novum		= $ - exsrt_iface_signa
	db 2, EXS_IFACE_T_PTR, EXS_IFACE_T_PTR, EXS_IFACE_T_U64	; (ptr u64) -> ptr
  .scribe_octeto	= $ - exsrt_iface_signa
	db 2, EXS_IFACE_T_U64, EXS_IFACE_T_PTR, EXS_IFACE_T_U8
								; (ptr u8) -> u64:
								; the Scriptor by ptr,
								; the byte by value
  .ab_introitu		= $ - exsrt_iface_signa
	db 2, EXS_IFACE_T_VOID, EXS_IFACE_T_PTR, EXS_IFACE_T_PTR
								; (ptr ptr) -> void:
								; `ad_exitum`'s shape --
								; hidden return ptr first,
								; then `a`
  .lege_octeto		= $ - exsrt_iface_signa
	db 1, EXS_IFACE_T_U16, EXS_IFACE_T_PTR	; (ptr) -> u16: the Lector by
						; ptr, the byte-or-256 by value
EXS_IFACE_SIGNA_LEN = $ - exsrt_iface_signa

; ---- the declarations -------------------------------------------------------
exsrt_iface_decls:

; 0: structura Scriptor { a: ambitus, descriptor: i32 }
;    Capability-bearing, mark {ambitus} (spec 4.3). Spec 4.3 says such a type
;    "must be declared so" and gives no syntax; CHK finding 7 has the checker
;    COMPUTE the mark, and for a prelude type this row simply states it.
	db EXS_IFACE_K_STRUCT, EXS_IFACE_F_PUBLICA or EXS_IFACE_F_CAPBEARING
	db EXS_IFACE_CAP_NONE, 0
	dd exsrt_iface_nomina.Scriptor, exsrt_iface_nomina.Scriptor_len
	dd 0, 0
	dd EXS_IFACE_NULLUS
	dd EXS_IFACE_NULLUS
	dd EXS_IFACE_SCRIPTOR_SIZE, EXS_IFACE_SCRIPTOR_ALIGN

; 1: Scriptor.a : ambitus
	db EXS_IFACE_K_FIELD, EXS_IFACE_F_PUBLICA
	db EXS_IFACE_CAP_AMBITUS, 0
	dd exsrt_iface_nomina.a, exsrt_iface_nomina.a_len
	dd 0, 0
	dd EXS_IFACE_D_SCRIPTOR
	dd EXS_IFACE_NULLUS
	dd EXS_IFACE_SCRIPTOR_A, 8

; 2: Scriptor.descriptor : i32
	db EXS_IFACE_K_FIELD, EXS_IFACE_F_PUBLICA
	db EXS_IFACE_CAP_NONE, 0
	dd exsrt_iface_nomina.descriptor, exsrt_iface_nomina.descriptor_len
	dd 0, 0
	dd EXS_IFACE_D_SCRIPTOR
	dd EXS_IFACE_NULLUS
	dd EXS_IFACE_SCRIPTOR_DESCRIPTOR, 4

; 3: functio ambitus(m: Mundus) -> ambitus
;    The receiver is the first parameter (spec 8.6 decision 8), so
;    `m.ambitus()` is `ambitus(m)`. A CALL, not an intrinsic, under
;    runtime.md 2.2's rule: a name is an intrinsic iff its lowering has no
;    call form.
	db EXS_IFACE_K_FN, EXS_IFACE_F_PUBLICA
	db EXS_IFACE_CAP_AMBITUS, 0
	dd exsrt_iface_nomina.ambitus, exsrt_iface_nomina.ambitus_len
	dd exsrt_iface_nomina.sym_mundus_ambitus, exsrt_iface_nomina.sym_mundus_ambitus_len
	dd EXS_IFACE_NULLUS
	dd exsrt_iface_signa.mundus_ambitus
	dd 0, 0

; 4: functio ad_exitum(a: ambitus) -> Scriptor, on Scriptor
;    Associated, not a method: `examples/initium.exsc` writes
;    `Scriptor.ad_exitum(a)`, with no receiver value.
	db EXS_IFACE_K_FN, EXS_IFACE_F_PUBLICA or EXS_IFACE_F_ASSOCIATA
	db EXS_IFACE_CAP_NONE, 0
	dd exsrt_iface_nomina.ad_exitum, exsrt_iface_nomina.ad_exitum_len
	dd exsrt_iface_nomina.sym_ad_exitum, exsrt_iface_nomina.sym_ad_exitum_len
	dd EXS_IFACE_D_SCRIPTOR
	dd exsrt_iface_signa.ad_exitum
	dd 0, 0

; 5: functio scribe(s: Scriptor, t: textus) -> mensura
;    [OPEN] spec 11 as amended says this returns `eventus<mensura>`; the
;    prelude returns the COUNT until `eventus` has syntax, and that is what
;    this row declares. Marked APERTUM so the row cannot be mistaken for a
;    settled signature.
	db EXS_IFACE_K_FN, EXS_IFACE_F_PUBLICA or EXS_IFACE_F_APERTUM
	db EXS_IFACE_CAP_NONE, 0
	dd exsrt_iface_nomina.scribe, exsrt_iface_nomina.scribe_len
	dd exsrt_iface_nomina.sym_scribe, exsrt_iface_nomina.sym_scribe_len
	dd EXS_IFACE_D_SCRIPTOR
	dd exsrt_iface_signa.scribe
	dd 0, 0

; 6: functio alloc(m: Mundus, n: mensura) -> alloc
;    [OPEN] the surface spelling and the unit of `n` (runtime.md 8); spec 4.5
;    says only that `sub alloc = a` binds an arena.
	db EXS_IFACE_K_FN, EXS_IFACE_F_PUBLICA or EXS_IFACE_F_APERTUM
	db EXS_IFACE_CAP_ALLOC, 0
	dd exsrt_iface_nomina.alloc, exsrt_iface_nomina.alloc_len
	dd exsrt_iface_nomina.sym_alloc_novum, exsrt_iface_nomina.sym_alloc_novum_len
	dd EXS_IFACE_NULLUS
	dd exsrt_iface_signa.alloc_novum
	dd 0, 0

; 7: functio scribe_octeto(s: Scriptor, b: u8) -> mensura
;    docs/design/wire-codec.md D7. One byte, the low 8 bits of `b`, to `s`'s
;    descriptor -- the raw-byte writer a `textus` cannot be (spec 5.1: UTF-8).
;    Provisional for exactly `scribe`'s reason, and marked the same way:
;    APERTUM, returning the COUNT (1, or 0 on failure) until `eventus` has
;    syntax. A method on `Scriptor` like row 5, not associated: `s` is the
;    receiver, so the checker gives it `scribe`'s shape and its row.
	db EXS_IFACE_K_FN, EXS_IFACE_F_PUBLICA or EXS_IFACE_F_APERTUM
	db EXS_IFACE_CAP_NONE, 0
	dd exsrt_iface_nomina.scribe_octeto, exsrt_iface_nomina.scribe_octeto_len
	dd exsrt_iface_nomina.sym_scribe_octeto, exsrt_iface_nomina.sym_scribe_octeto_len
	dd EXS_IFACE_D_SCRIPTOR
	dd exsrt_iface_signa.scribe_octeto
	dd 0, 0

; 8: structura Lector { a: ambitus, descriptor: i32 }
;    `Scriptor`'s row, for the other direction. Capability-bearing, mark
;    {ambitus} (spec 4.3), stated here as row 0 states `Scriptor`'s -- and
;    with no FIELD rows after it, for the reason part A's Lector block gives.
;    The mark is therefore this row's `flags` and
;    checker/rows/compute.inc's `chk_row_pre_marks` byte, with no
;    `Lector.a : ambitus` row between them.
	db EXS_IFACE_K_STRUCT, EXS_IFACE_F_PUBLICA or EXS_IFACE_F_CAPBEARING
	db EXS_IFACE_CAP_NONE, 0
	dd exsrt_iface_nomina.Lector, exsrt_iface_nomina.Lector_len
	dd 0, 0
	dd EXS_IFACE_NULLUS
	dd EXS_IFACE_NULLUS
	dd EXS_IFACE_LECTOR_SIZE, EXS_IFACE_LECTOR_ALIGN

; 9: functio ab_introitu(a: ambitus) -> Lector, on Lector
;    Associated, not a method, exactly as `Scriptor.ad_exitum(a)` is: there is
;    no receiver value to have. Spec 3.1's qualifier rule wants an ABLATIVE
;    after the `_`, and `introitu` is the ablative of `introitus`, the
;    entrance -- `ab introitu`, "from the entrance", the preposition's own
;    case. (`ad_exitum`, row 4, writes the accusative; that mismatch predates
;    this row and is reported, not copied.)
	db EXS_IFACE_K_FN, EXS_IFACE_F_PUBLICA or EXS_IFACE_F_ASSOCIATA
	db EXS_IFACE_CAP_NONE, 0
	dd exsrt_iface_nomina.ab_introitu, exsrt_iface_nomina.ab_introitu_len
	dd exsrt_iface_nomina.sym_ab_introitu, exsrt_iface_nomina.sym_ab_introitu_len
	dd EXS_IFACE_D_LECTOR
	dd exsrt_iface_signa.ab_introitu
	dd 0, 0

; 10: functio lege_octeto(l: Lector) -> u16
;    One raw byte from `l`'s descriptor, or 256 at end of input. `lege` is
;    spec 3.3's own worked root (`leg-` + the third-conjugation imperative
;    3.4 calls `-e`) and `octeto` is `scribe_octeto`'s ablative qualifier, so
;    the pair reads as one family.
;
;    PROVISIONAL, and marked APERTUM for `scribe`'s reason: spec 11 as
;    amended wants `eventus`, which has no syntax, so the result is a `u16`
;    with 256 for "no byte" -- and EOF and error are NOT distinguished (the
;    blob's own comment says so at the routine). When `eventus` lands this
;    becomes `eventus<u8>` and the sentinel goes.
	db EXS_IFACE_K_FN, EXS_IFACE_F_PUBLICA or EXS_IFACE_F_APERTUM
	db EXS_IFACE_CAP_NONE, 0
	dd exsrt_iface_nomina.lege_octeto, exsrt_iface_nomina.lege_octeto_len
	dd exsrt_iface_nomina.sym_lege_octeto, exsrt_iface_nomina.sym_lege_octeto_len
	dd EXS_IFACE_D_LECTOR
	dd exsrt_iface_signa.lege_octeto
	dd 0, 0

assert $ - exsrt_iface_decls = EXS_IFACE_DECL_COUNT * EXS_IFACE_DECL_SIZE

end if	; EXS_IFACE_EMIT_DECLS

; =============================================================================
; PART C -- what has NO declaration, and the H6 request
; =============================================================================
; Blob labels reachable from emitted code that this table does NOT declare,
; because the emitter reaches them by a HARDCODED label rather than by
; resolving an IR `@name`:
;
;   exsrt_retain  exsrt_release  exsrt_retain_c  exsrt_release_c
;       `retain`/`release` are IR INSTRUCTIONS, not names (IR 2.8). The
;       emitter picks the `_c` form from the value's type (`ref` vs `refc`,
;       IR 2.3).
;   exsrt_abort
;       `bfausr_trap` should become `mov edi, 1 / jmp exsrt_abort`
;       (runtime.md 2.5) -- a request to the backend agent.
;
; And blob labels that DO carry the `bfausr_exsrt_` prefix but have no row
; here, because they have no surface spelling to declare:
;
;   exsrt_alloc_da  exsrt_alloc_reconde  exsrt_alloc_dimitte
;
; RUNTIME.MD H6, STATED AS A REQUEST. A user function named `exsrt_...` is
; emitted as `bfausr_exsrt_...` and collides with a blob label at assembly
; time, naming neither file. The rows above make the DECLARED names a class-B
; redefinition at the checker; the three above and every private `exsrt_`
; name have no row and the collision is fasmg's to report, badly. The fix H6
; itself names is to reserve the whole prefix:
;
;   EXS_IFACE_PRAEFIXUM_RESERVATUM = 'exsrt_'
;
; A module-level declaration whose name begins with those six bytes should be
; refused by the checker with an existing section 13 code, or the backend
; should mangle. Neither is this agent's to write; both are in the report.
; [OPEN] until one lands.
