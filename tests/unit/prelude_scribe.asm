; tests/unit/prelude_scribe.asm
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
; The prelude blob, assembled ALONE and run: no compiler output exists yet, so
; this harness plays the part backend_fasmg/program.inc will play -- it opens
; the three segments, defines the eleven EXS_POTESTAS_* constants and
; EXS_MXCSR, includes the two blobs verbatim, and supplies a hand-written
; `bfausr_initium` in place of an emitted one. `emit.inc` was proven the same
; way against hand-written IR.
;
; WHAT IT PROVES (docs/design/runtime.md 3, 6):
;   1. `exsrt_start` records rsp0, loads MXCSR and calls `bfausr_initium`
;      with the Mundus carrier in rdi.
;   2. `exsrt_mundus_ambitus` derives ExsAmbitus from rsp0 and returns it.
;   3. `exsrt_scriptor_ad_exitum` builds a Scriptor over stdout -- checked by
;      reading `descriptor` through interface.inc's OWN stated offset, which
;      is what runtime.md H4 asks this fixture to do: the blob and the
;      interface are two copies of one layout and this is where they meet.
;   4. `exsrt_scriptor_scribe` writes 101 bytes and RETURNS 101, which
;      `exsrt_start` turns into the process exit status. `expect-exit=101` is
;      therefore a claim about the return value, not just about survival.
;   5. The literal is byte-identical to `examples/saluta.expected`: the bytes
;      below are hex `db` lines, exactly the form runtime.md 2.1 requires of
;      `OUT` (a fasmg string cannot hold a raw newline and this literal holds
;      six), and the fixture memcmps them at runtime against the golden file
;      pulled in with `file`. A transcription error exits 22, not 101.
;   6. The binary's syscall surface is `write` and `exit_group` and nothing
;      else -- runtime.md 2.6's row for {Mundus, ambitus}. `audit=pass` is the
;      allowlist check; the exact set is in the prelude agent's report.
;
; WHAT IT CANNOT PROVE. A partial write and an EINTR retry both need a second
; process (a pipe with a small buffer, a signal), which tests/run.sh does not
; have. Those two paths of `scribe` are `[UNTESTED]` and runtime.md 3 says so.
;
; Exit codes: 101 = everything above. 21 = Scriptor.descriptor was not 1.
; 22 = the literal does not match examples/saluta.expected.
;
; TEST: run=yes expect-exit=101 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry exsrt_start

; ---- what a wrapper must define (prelude/README.md) -------------------------
; Spec 4.6 order. For this fixture the closure is {Mundus, ambitus}: the same
; closure `examples/initium.exsc` has, and the same one runtime.md 5 walks
; through.
EXS_POTESTAS_MUNDUS	= 1
EXS_POTESTAS_ALLOC	= 0
EXS_POTESTAS_SERMO	= 0
EXS_POTESTAS_HOROLOGIUM	= 0
EXS_POTESTAS_ARCHIVUM	= 0
EXS_POTESTAS_RETE	= 0
EXS_POTESTAS_FORTUNA	= 0
EXS_POTESTAS_AMBITUS	= 1
EXS_POTESTAS_FILUM	= 0
EXS_POTESTAS_MACHINA	= 0
EXS_POTESTAS_CRUDUM	= 0
EXS_MXCSR		= 0x1F80	; ad_parem + conservata (runtime.md 2.7)

segment readable executable

; ---- the blob, verbatim -----------------------------------------------------
include '../../compiler/x86_64/prelude/prelude.asm'

; ---- the interface, AFTER the blob so its H4 cross-asserts actually fire ----
; interface.inc's offset asserts are guarded by `if defined EXS_SCRIPTOR_A`.
; Included before the blob they would be silently skipped, and the check
; runtime.md H4 exists for would report green while seeing nothing.
include '../../compiler/x86_64/prelude/interface.inc'

; ---- the module: what the lowering will emit for examples/initium.exsc ------
; runtime.md 5's IR, hand-written in the shape emit.inc produces: hand-rolled
; SysV prologue, one 8-byte stack slot per value, every operand reloaded.
;
;   functio @initium (ptr) -> u8 {
;     %0 = param ptr 0
;     %1 = call ptr @exsrt_mundus_ambitus %0
;     %2 = slot 16 8
;     call void @exsrt_scriptor_ad_exitum %2 %1
;     %3 = slot 16 8
;     ... (saluta's two stores, inlined here as the literal view)
;     %4 = call u64 @exsrt_scriptor_scribe %2 %3
;     ret %4 (as u8)
;   }
bfausr_initium:
	push	rbp
	mov	rbp, rsp
	sub	rsp, 48			; [rbp-16] Scriptor, [rbp-32] textus,
					; [rbp-40] ambitus. 48 keeps rsp
					; 16-aligned at every call below.

	call	bfausr_exsrt_mundus_ambitus	; rdi is already the Mundus carrier
	mov	[rbp - 40], rax

	lea	rdi, [rbp - 16]			; hidden return ptr first (IR 2.9)
	mov	rsi, rax			; a
	call	bfausr_exsrt_scriptor_ad_exitum

	; runtime.md H4: read the field through the INTERFACE's offset, not the
	; blob's. If the two ever disagree this is where it shows.
	mov	eax, [rbp - 16 + EXS_IFACE_SCRIPTOR_DESCRIPTOR]
	cmp	eax, 1
	jne	.descriptor_malus

	; The literal must be the golden file, byte for byte.
	lea	rsi, [bfausr_g1]
	lea	rdi, [exsfx_golden]
	mov	ecx, EXSFX_GOLDEN_LEN
  .conferre:
	mov	al, [rsi]
	cmp	al, [rdi]
	jne	.litera_mala
	inc	rsi
	inc	rdi
	dec	ecx
	jnz	.conferre

	; textus is a two-word view (runtime.md 2.3): {ptr, len}, no header.
	lea	rax, [bfausr_g1]
	mov	[rbp - 32 + EXS_IFACE_TEXTUS_PTR], rax
	mov	rax, EXSFX_G1_LEN
	mov	[rbp - 32 + EXS_IFACE_TEXTUS_LEN], rax

	lea	rdi, [rbp - 16]			; s
	lea	rsi, [rbp - 32]			; t
	call	bfausr_exsrt_scriptor_scribe	; -> rax = bytes written

	add	rsp, 48
	pop	rbp
	ret

  .descriptor_malus:
	mov	eax, 21
	add	rsp, 48
	pop	rbp
	ret

  .litera_mala:
	mov	eax, 22
	add	rsp, 48
	pop	rbp
	ret

; ---- the data segment: string literals in global id order -------------------
; `data $1 101 1 <hex>` -> a label and its bytes. Readable and NOT writeable,
; so immutability of a literal is a property the MMU enforces rather than one
; the checker promises (runtime.md 2.1).
segment readable

bfausr_g1:
	db	0x41,0x76,0x65,0x2C,0x20,0x6D,0x75,0x6E,0x64,0x75,0x73,0x2E
	db	0x0A,0x0A,0x45,0x78,0x20,0x73,0x69,0x6C,0x65,0x6E,0x74,0x69
	db	0x6F,0x20,0x73,0x75,0x72,0x67,0x69,0x74,0x20,0x66,0x6F,0x72
	db	0x6D,0x61,0x2E,0x0A,0x45,0x78,0x20,0x73,0x69,0x67,0x6E,0x6F
	db	0x20,0x6E,0x61,0x73,0x63,0x69,0x74,0x75,0x72,0x20,0x76,0x6F
	db	0x78,0x2E,0x0A,0x45,0x78,0x20,0x63,0x6F,0x64,0x69,0x63,0x65
	db	0x20,0x66,0x69,0x74,0x20,0x6C,0x75,0x6D,0x65,0x6E,0x2E,0x0A
	db	0x0A,0x48,0x6F,0x64,0x69,0x65,0x20,0x69,0x6E,0x63,0x69,0x70
	db	0x69,0x6D,0x75,0x73,0x2E
EXSFX_G1_LEN = $ - bfausr_g1

; The same bytes, taken straight from the golden file rather than typed. Not
; part of `OUT`: this copy exists so the hex above cannot drift from
; examples/saluta.expected without the fixture noticing.
exsfx_golden:
	file '../../examples/saluta.expected'
EXSFX_GOLDEN_LEN = $ - exsfx_golden

assert EXSFX_G1_LEN = 101
assert EXSFX_GOLDEN_LEN = 101

; ---- the prelude's mutable state, verbatim ----------------------------------
segment readable writeable
include '../../compiler/x86_64/prelude/prelude_data.asm'
