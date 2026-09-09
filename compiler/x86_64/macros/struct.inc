; compiler/x86_64/macros/struct.inc
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
; struct / end struct: named, typed field declarations with assemble-time
; constant offsets (e.g. `Span.start`, `Span.length`), so field access reads
; as a name instead of a hand-tracked integer.
;
; Copy-adapted (not `include`d) from vendor/fasmg-x86/format/elf64.inc's own
; ~15-line `struct?`/`end?.struct?!` macro pair, which is built entirely on
; fasmg's NATIVE `struc?` / `virtual at` / `namespace` directives -- verified
; present (grep) in elf64.inc, elf32.inc, coff.inc and coffms.inc, and each of
; those files `purge`s it after use (see e.g. elf64.inc line 61: `purge
; struct?`). It is not reachable from this project's fixtures: `format ELF64
; executable 3` (what tests/unit/*.asm uses) routes through
; vendor/fasmg-x86/format/elfexe.inc, which never includes elf64.inc -- so
; this file is not a duplicate include, it is the only place this repo's own
; code can reach a `struct`/`end struct` pair from. Copy-adapting rather than
; including also means this macro survives independently of which object
; format (or none) a given translation unit happens to pull in.
;
; Closes with `end struct`, NOT `ends` -- see docs/asm-conventions.md's
; struct section ("3.3 macros/struct.inc") amendment: `ends` never appears
; anywhere in vendor/fasmg-x86; every block
; there closes two-token (`end macro`, `end virtual`, `end struc`), and this
; macro literally expands `end struct` into `end struc` (native) underneath.
;
; Field syntax: BOTH forms below are accepted, freely mixed, in one struct
; body --
;   name  dd ?          -- bare native `struc` field (this is what elf64.inc
;                          itself uses throughout; needs no help from this
;                          file, `struc` already parses it)
;   field name, dd      -- sugar for the same thing, defined here in terms of
;                          the bare form (see `field?`, below)
;
; Usage (verified -- see tests/unit/struct_fields.asm):
;   struct Span
;           field start,  dd
;           length dd ?
;   end struct
;
;   mov     eax, [rdi + Span.start]      ; Span.start / Span.length / sizeof.Span
;   mov     [rdi + Span.length], ecx     ; are plain assemble-time constants
;
; Multiple `struct ... end struct` blocks in one file do not collide (each
; gets its own `namespace .` scope during the measuring pass) -- verified
; with two structs in the same translation unit.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §6.3 (struct-typed layouts back
; every rt/ data structure: Arena, Vec, Map, Str, ...)
; -----------------------------------------------------------------------------

macro struct? name
	macro end?.struct?!
			end namespace
		esc end struc
		virtual at 0
			name name
			sizeof.name = $
		end virtual
		purge end?.struct?
	end macro
	esc struc name
		label . : sizeof.name
		namespace .
end macro

; `field name, decl` sugar -- expands to the bare native form `name decl ?`.
; Defined once, at file scope (not scoped to a single `struct` block): it is
; a plain module-owned helper, not something that needs to be shadowed or
; purged between structs, and struct.inc is the one place in this repo that
; is allowed to define it (macros/ is asm-rt-owned).
macro field? fieldname, decl
	fieldname decl ?
end macro
