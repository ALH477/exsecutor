; tests/unit/struct_fields.asm
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
; asm-rt fixture for compiler/x86_64/macros/struct.inc.
;
; Proves, at assemble time AND at runtime:
;   - the bare native-struc field syntax (`name decl ?`)
;   - the `field name, decl` sugar syntax
;   - BOTH accepted in the SAME struct body, freely mixed
;   - two separate `struct ... end struct` blocks in one file do not collide
;   - `Type.field` and `sizeof.Type` are real assemble-time constants usable
;     in `[reg + Type.field]`-style addressing, not just symbolic
;
; Assemble-time checks (1-4) use `err` directly -- a mismatch here fails the
; build outright, so they are not part of the runtime exit-code convention
; below. Runtime checks (5-6) follow this suite's "exit 0 = all passed,
; 10+N = check N failed" convention (see tests/README.md and
; .claude/agents/asm-rt.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'
include '../../compiler/x86_64/macros/struct.inc'

; struct 1: bare syntax only
struct Span
	start  dd ?
	length dd ?
end struct

; struct 2: field sugar only
struct Point
	field x, dd
	field y, dd
end struct

; struct 3: BOTH syntaxes mixed in one body
struct Mixed
	field a, dq
	b dd ?
	field c, dw
	d db ?
end struct

; -- assemble-time checks: offsets and sizeof are correct plain constants --
if Span.start <> 0
	err 'check 1 FAILED: Span.start should be 0'
end if
if Span.length <> 4
	err 'check 1 FAILED: Span.length should be 4'
end if
if sizeof.Span <> 8
	err 'check 1 FAILED: sizeof.Span should be 8'
end if

if Point.x <> 0
	err 'check 2 FAILED: Point.x should be 0'
end if
if Point.y <> 4
	err 'check 2 FAILED: Point.y should be 4'
end if
if sizeof.Point <> 8
	err 'check 2 FAILED: sizeof.Point should be 8'
end if

if Mixed.a <> 0
	err 'check 3 FAILED: Mixed.a should be 0'
end if
if Mixed.b <> 8
	err 'check 3 FAILED: Mixed.b should be 8'
end if
if Mixed.c <> 12
	err 'check 3 FAILED: Mixed.c should be 12'
end if
if Mixed.d <> 14
	err 'check 3 FAILED: Mixed.d should be 14'
end if
if sizeof.Mixed <> 15
	err 'check 4 FAILED: sizeof.Mixed should be 15'
end if

format ELF64 executable 3
entry start

segment readable executable
  start:
	; check 5: Span fields addressable through a base register
	lea	rdi, [buf]
	mov	dword [rdi + Span.start], 111
	mov	dword [rdi + Span.length], 222
	mov	eax, [rdi + Span.start]
	cmp	eax, 111
	jne	.fail5
	mov	eax, [rdi + Span.length]
	cmp	eax, 222
	jne	.fail5

	; check 6: Mixed fields (both syntaxes in one struct) addressable and
	; independently writable without overlap
	lea	rsi, [buf]
	mov	rax, 0xAABBCCDD11223344
	mov	[rsi + Mixed.a], rax
	mov	dword [rsi + Mixed.b], 0x55667788
	mov	word  [rsi + Mixed.c], 0x99AA
	mov	byte  [rsi + Mixed.d], 0xEF
	mov	rax, [rsi + Mixed.a]
	mov	rcx, 0xAABBCCDD11223344
	cmp	rax, rcx
	jne	.fail6
	mov	eax, [rsi + Mixed.b]
	cmp	eax, 0x55667788
	jne	.fail6
	movzx	eax, word [rsi + Mixed.c]
	cmp	eax, 0x99AA
	jne	.fail6
	movzx	eax, byte [rsi + Mixed.d]
	cmp	eax, 0xEF
	jne	.fail6

	mov	eax, 231
	xor	edi, edi
	syscall

  .fail5:
	mov	eax, 231
	mov	edi, 15
	syscall
  .fail6:
	mov	eax, 231
	mov	edi, 16
	syscall

segment readable writeable
  buf: rb 32
