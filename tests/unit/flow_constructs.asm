; tests/unit/flow_constructs.asm
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
; asm-rt fixture for compiler/x86_64/macros/flow.inc.
;
; Exercises every construct the pre-freeze checklist requires -- named
; `flow_if`/`flow_elseif`/`flow_else`/`flow_endif`, `flow_while`/`flow_endw`,
; `flow_for`/`flow_endf`, `flow_switch`/`flow_case`/`flow_default`/
; `flow_endsw` here, not the dot-prefixed spelling docs/asm-conventions.md's
; flow section proposed -- see flow.inc's header for why that spelling does
; not work inside a real code segment (a real, load-bearing dialect limit,
; not a style choice).
;
;   1. `flow_if`/`flow_elseif`/`flow_else`/`flow_endif` -- all three branches
;      taken across three separate checks, plus a condition-false `flow_if`
;      with no `flow_elseif`/`flow_else` (body correctly skipped), plus two
;      independent `flow_if` blocks back to back (label reuse across
;      sibling, not nested, constructs)
;   2. `flow_while`/`flow_endw` -- a real loop (sum 1..5), and NESTED
;      while-in-while
;   3. `flow_for`/`flow_endf` -- a real loop (sum 0..15), zero iterations
;      when start==end, and NESTED for-in-for
;   4. `flow_switch`/`flow_case`/`flow_default`/`flow_endsw` -- a
;      single-value match, a multi-value `flow_case`, and fall-through to
;      `flow_default` -- this construct's own `flow_case`/`flow_switch`
;      interaction had a real bug (the first case unconditionally skipped
;      the whole switch) caught by exactly this kind of check; see
;      flow.inc's header
;   5. cross-construct nesting 4 deep: `flow_switch` > `flow_while` >
;      `flow_for` > `flow_if`/`flow_elseif`/`flow_else`
;
; Exit 0 = all checks passed; 10+N = check N failed (see tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'
include '../../compiler/x86_64/macros/flow.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	; ---- check 1: if / elseif / else, all branches + skip + reuse ----
	mov	rax, 5
	mov	rbx, 0
	flow_if rax eq 5
		mov	rbx, 1
	flow_elseif rax eq 6
		mov	rbx, 2
	flow_else
		mov	rbx, 3
	flow_endif
	cmp	rbx, 1
	jne	.fail1

	mov	rax, 6
	mov	rbx, 0
	flow_if rax eq 5
		mov	rbx, 1
	flow_elseif rax eq 6
		mov	rbx, 2
	flow_else
		mov	rbx, 3
	flow_endif
	cmp	rbx, 2
	jne	.fail1

	mov	rax, 99
	mov	rbx, 0
	flow_if rax eq 5
		mov	rbx, 1
	flow_elseif rax eq 6
		mov	rbx, 2
	flow_else
		mov	rbx, 3
	flow_endif
	cmp	rbx, 3
	jne	.fail1

	mov	rax, 1
	mov	rbx, 0
	flow_if rax eq 999
		mov	rbx, 111
	flow_endif
	cmp	rbx, 0
	jne	.fail1

	mov	rax, 1
	mov	rbx, 0
	flow_if rax eq 1
		mov	rbx, 10
	flow_endif
	flow_if rax eq 1
		add	rbx, 10
	flow_endif
	cmp	rbx, 20
	jne	.fail1

	; ---- check 2: while, including nested while-in-while ----
	mov	rax, 0
	mov	rcx, 1
	flow_while rcx le 5
		add	rax, rcx
		inc	rcx
	flow_endw
	cmp	rax, 15
	jne	.fail2

	mov	rax, 0
	mov	rcx, 0
	flow_while rcx lt 3
		mov	rdx, 0
		flow_while rdx lt 3
			inc	rax
			inc	rdx
		flow_endw
		inc	rcx
	flow_endw
	cmp	rax, 9
	jne	.fail2

	; ---- check 3: for, including zero-iteration and nested for-in-for ----
	mov	rax, 0
	flow_for ecx, 0, 16
		add	eax, ecx
	flow_endf
	cmp	eax, 120
	jne	.fail3

	mov	rax, 999
	flow_for ecx, 5, 5
		mov	rax, 0
	flow_endf
	cmp	rax, 999
	jne	.fail3

	mov	rax, 0
	flow_for ecx, 0, 3
		flow_for edx, 0, 3
			inc	eax
		flow_endf
	flow_endf
	cmp	eax, 9
	jne	.fail3

	; ---- check 4: switch / case / default, single + multi-value + default
	mov	eax, 2
	mov	ebx, 0
	flow_switch eax
	flow_case 1
		mov	ebx, 100
	flow_case 2
		mov	ebx, 200
	flow_case 3, 4, 5
		mov	ebx, 300
	flow_default
		mov	ebx, 999
	flow_endsw
	cmp	ebx, 200
	jne	.fail4

	mov	eax, 4
	mov	ebx, 0
	flow_switch eax
	flow_case 1
		mov	ebx, 100
	flow_case 3, 4, 5
		mov	ebx, 300
	flow_default
		mov	ebx, 999
	flow_endsw
	cmp	ebx, 300
	jne	.fail4

	mov	eax, 42
	mov	ebx, 0
	flow_switch eax
	flow_case 1
		mov	ebx, 100
	flow_case 3, 4, 5
		mov	ebx, 300
	flow_default
		mov	ebx, 999
	flow_endsw
	cmp	ebx, 999
	jne	.fail4

	; ---- check 5: 4-deep cross-construct nesting ----
	; switch > while > for > if/elseif/else
	; per outer while iteration (2 total), inner for (edx 0..2) contributes
	; 100 (edx=0, else) + 1 (edx=1, if) + 10 (edx=2, elseif) = 111
	; total = 111 * 2 = 222
	mov	rax, 0
	mov	rcx, 0
	flow_switch rcx
	flow_case 0
		flow_while rcx lt 2
			flow_for edx, 0, 3
				flow_if edx eq 1
					inc	rax
				flow_elseif edx eq 2
					add	rax, 10
				flow_else
					add	rax, 100
				flow_endif
			flow_endf
			inc	rcx
		flow_endw
	flow_default
		mov	rax, 999999
	flow_endsw
	cmp	rax, 222
	jne	.fail5

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
  .fail5: mov eax,231
	mov edi,15
	syscall
