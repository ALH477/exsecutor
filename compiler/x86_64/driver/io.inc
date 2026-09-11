; compiler/x86_64/driver/io.inc
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
; Reading the §12 compilation unit -- one file or many -- and writing the one
; artifact §12 names, `-o OUT`. Five syscalls, every one of them already on the
; closed allowlist and every one of them reached through rt/sys.inc's wrappers:
; `openat(257)`, `fstat(5)`, `read(0)`, `write(1)`, `close(3)`. This file adds
; nothing to the allowlist -- emitting a program cost a flag bit, not a
; syscall -- and emits no raw `syscall` (docs/asm-conventions.md, "6. Syscall
; discipline"). `execve` is absent and stays absent: §12 makes OUT emitted TEXT
; that `exsc` never assembles.
;
; THE UNIT IS ONE CONTIGUOUS BUFFER, AND THAT IS FORCED, NOT PREFERRED. §12:
; *"The files named form one compilation unit -- one module."* `cst_parse`
; reads `Lexer.src_ptr` for every leaf's text and for every byte of trivia
; between two tokens, so the token stream it parses must index ONE buffer;
; `ast_from_cst` and `chk_set_source` each take one source as well. So the
; unit's files are read BACK TO BACK into a single allocation, in command-line
; order, and driver/run.inc rewrites each file's token spans into offsets
; within it. Nothing is inserted between two files: the buffer is the exact
; concatenation of their bytes, so no byte the CST holds came from anywhere
; but a source file.
;
; TWO PASSES, ONE DESCRIPTOR AT A TIME. `arena_init` needs the unit's total
; size before a single byte can be stored, and the only way to learn a file's
; size here is `fstat` on an open descriptor (`stat` is not on the allowlist).
; So every file is opened, `fstat`ed and closed in pass 1, then opened, read
; and closed in pass 2. The alternative -- holding all N descriptors open
; between the passes -- saves one `openat` per file and costs a hard
; dependency on RLIMIT_NOFILE being larger than the unit, which is exactly the
; kind of ambient host fact §9.3 exists to keep out of the result. The cost is
; a window in which a file can change: one that SHRANK yields what is actually
; there (the read loop stops at EOF and the following file starts at the
; running cursor, so the concatenation has no hole), and one that GREW is
; truncated to its pass-1 size -- the same two outcomes the single-file path
; already had between its own `fstat` and its own `read`.
;
; NO SOCKET SYSCALL APPEARS IN THIS FILE OR ANYWHERE ELSE UNDER driver/.
; §9.3: *"No network access, ever, at any phase."* `make audit` checks the
; assembled binary rather than this claim -- which is the point of writing it
; as a check instead of a sentence.
;
; `AT_FDCWD`, NOT AN AMBIENT CWD OF OUR OWN. rt/sys.inc's header is explicit
; that `sys_openat` has "no ambient-cwd concept of its own" and that callers
; pass `dirfd` themselves. `exsc` resolves a relative SOURCE against the
; process's working directory, which is where the invoker pointed it -- that
; is a property of the invocation, not ambient state §9.3 forbids, and it is
; the same directory the path was written for.
;
; THE ARENA IS SIZED FROM THE UNIT, WHICH IS WHY IT IS CREATED HERE.
; rt/arena.inc is deliberately fixed-capacity: *"`arena_alloc` past the
; capacity given to `arena_init` `rassert`s ... rather than transparently
; mapping more"*, and `[UNIMPLEMENTED]: auto-growth / chunked arenas`. A
; driver that picked a constant capacity would therefore trap (SIGILL, exit
; 132) on a file bigger than someone guessed. Instead `fstat` runs first and
; the arena is `arena_init`ed at DRV_ARENA_FLOOR + DRV_ARENA_FACTOR times the
; file's size.
;
; WHERE DRV_ARENA_FACTOR COMES FROM, since a magic number that is only a
; guess is worse than no number. The lexer allocates three things out of this
; arena, and each is bounded per source byte:
;   - the source text itself: 1x.
;   - the token vector: at worst one `Tok` (16 bytes) per source byte, and
;     rt/vec.inc's doubling growth "abandons the old backing block rather
;     than resizing it in place", so a vector that reached N bytes has
;     consumed about 2N of arena. 32x.
;   - the diagnostic vector: `sizeof.Diag` is 128 bytes (96 until the Stage 1 review added note, related span and argument) and the densest
;     possible run of diagnostics is one `EXS-E0103` per three-byte
;     dangerous codepoint -- 32 bytes of Diag per source byte, doubled the
;     same way. ~85x (64x at 96 bytes).
; 96 covered the worst of those plus the source and the ~20 KiB of NFC
; scratch `lex_init` takes.
;
; 96 IS NO LONGER ENOUGH, and the number below is a MEASUREMENT, not a
; second guess. The parser and the AST walker allocate from this same arena
; -- the green node array and its child arrays and identity-map entries
; (cst/green.inc), then the node, declaration and `extra` vectors of the
; typed tree (ast/build.inc) -- all with the same doubling growth that
; abandons the old block. Measured, by reading `Arena.cur - Arena.base` after
; a full lex + parse + `ast_from_cst` on two generated files:
;
;   279 KB of ordinary declarations and expressions ->  16.9 MB,  61x
;   197 KB of token-dense `a.b.b.b...` chains       ->  23.0 MB, 117x
;
; The second shape is the adversarial one: nearly every source byte is its
; own token, so the token vector, the green tree, the identity map and the
; AST all hit their worst per-byte ratio at once. On top of THAT sits the
; diagnostic case above (64x), whose densest input is a different file but
; whose cost adds if a file manages both. 256 is the next power of two above
; 117 + 64 + 1, and it is an over-estimate on purpose: the region is
; anonymous `mmap`, so an unused reservation costs address space and no
; pages, and a reservation the kernel refuses comes back through
; `arena_init`'s CF as a clean host error, not as a trap. DRV_SRC_MAX bounds
; the reservation itself at 4 GiB.
;
; 256 IS NO LONGER ENOUGH EITHER, FOR THE SAME REASON TWICE OVER: on the `-o`
; path this arena now also backs the SSA IR (`lwr_module`'s `ir_arena`) and
; the emitted text of OUT (`bfa_emit_program`'s `arena`), neither of which
; existed when 256 was measured. The factor is 384. Where the extra 128 comes
; from, stated as the arithmetic it is rather than as a measurement it is not:
; docs/design/lowering.md section 4 budgets a 10 000-line module (~50 k nodes,
; which at the ~6 bytes per node of the two files measured above is ~300 KB of
; source) at ~6.4 MiB of IR, doubled by `vec_push`'s abandon-and-grow to ~13
; MiB -- about 43x the source. The emitted text is the printed form of that IR
; plus a fixed ~31 KB of prelude blob, and `bfa_emit_program`'s output vector
; doubles the same way. 128x is threefold headroom over 43x and is an
; OVER-estimate on purpose, for the reason the paragraph above already gives:
; an anonymous `mmap` reservation costs address space, not pages.
;
; MEASURED, on the only unit that has ever been through the whole pipeline --
; the three-file hello world, 3,249 source bytes -- by reading
; `Arena.cur - Arena.base` at two points on the `-o` path:
;
;   after the front end and the checker      148,648 bytes    45.8x
;   after the lowering and the emission      226,808 bytes    69.8x
;
; so the lowering's IR plus the emitted text cost 78,160 bytes, 24.1x the
; source, of which 33,330 is OUT itself and 65,536 is `bfa_emit_program`'s
; initial output vector. The reservation for that unit is 4 MiB + 384 x 3,249
; = 5,441,920.
;
; THE HELLO WORLD DOES NOT RE-MEASURE THE 61x AND 117x ABOVE AND DOES NOT
; CLAIM TO: those were measured on 279 KB and 197 KB of generated source,
; where `vec_push`'s doubling dominates and a 3 KB unit's fixed costs do not.
; Three thousand bytes is the wrong size to bound a factor with. What the
; numbers above do establish is the SHAPE -- the back half is a fraction of
; the front half, not a multiple of it -- and 128x of headroom for it is
; therefore an over-estimate rather than a guess. `[UNREPRODUCED]` for
; anything larger: no module of that size exists to compile.
;
; `[UNIMPLEMENTED]`, stated rather than buried: a source file whose
; diagnostics somehow exceed that bound still traps through `arena_alloc`'s
; `rassert` instead of failing cleanly. The real fix is a growable arena, and
; that is rt/'s to build, not this file's to work around.
;
; THE LOWERING'S SECOND ARENA IS NOT THIS ONE AND IS NOT SIZED HERE.
; `lwr_module` takes two (docs/design/lowering.md 2.1) and RESETS the second
; at every function, so it cannot be an arena holding anything that must
; survive -- not this one, which holds the AST and the IR, and not
; `DrvCtx.scratch`, which is the checker's. It is `DrvCtx.lwrscr`, a fourth
; arena `driver/run.inc` creates on the `-o` path alone.
;
; THE CHECKER (compiler/x86_64/checker/), WIRED IN BY driver/run.inc, DOES NOT
; CHANGE THIS NUMBER. Its own bookkeeping -- the frame stack, the bind stack,
; the buffered-diagnostic vector, the capability-use list, and the two module/
; member tables -- comes out of `DrvCtx.scratch`, a separate, fixed-capacity
; arena (`DRV_SCRATCH_CAP`, below) that this 256x factor says nothing about;
; see driver/run.inc's checker-wiring comment for why reusing it there is
; safe. The one thing the checker DOES allocate from THIS arena is
; `__chk_fix_text` (checker/resolve/resolve.inc): a borrowed `DiagFix` needs
; to outlive the checker's own scratch, so its handful of bytes -- a fixed
; prefix plus one capability atom's name, per §13-listed diagnostic that
; carries a required fix -- come from `Ast.arena`, i.e. this one. That is
; bounded by diagnostic COUNT, not by source bytes, and a checker diagnostic
; is at most one per declaration or per capability draw, never one per byte
; the way §8.1's dangerous-codepoint case above is -- negligible next to the
; 64x diagnostic-density term this factor already budgets, and not
; re-measured here for that reason. If a future checker pass ever allocates
; per-byte from `DrvCtx.arena` (not `.scratch`), THIS comment is where that
; measurement belongs, updated by running it the way the two figures above
; were, not by guessing a new factor.
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.3 (purity, the syscall surface),
; §8.1 (what the bytes are then held to).
; -----------------------------------------------------------------------------

include 'cli.inc'

DRV_AT_FDCWD = -100		; openat: resolve relative to the process cwd
DRV_O_RDONLY = 0

; Writing OUT (spec 12: EMITTED TEXT -- `exsc` never assembles it). The three
; flags docs/design/runtime.md section 6 names for the driver, plus 0644,
; spelled here rather than as bare integers at the call site. x86-64 Linux
; values; nothing derives them from a host header, because there is no libc
; in this binary to derive them from.
DRV_O_WRONLY  = 0x001
DRV_O_CREAT   = 0x040
DRV_O_TRUNC   = 0x200
DRV_OUT_FLAGS = DRV_O_WRONLY or DRV_O_CREAT or DRV_O_TRUNC
DRV_OUT_MODE  = 0x1A4		; 0644 -- rw-r--r--, the mode a NEWLY created
				; OUT gets; an existing file keeps its own.

; x86-64 Linux `struct stat`: 144 bytes, st_mode at 24, st_size at 48.
; Named here rather than spelled as bare integers at the use site;
; rt/sys.inc deliberately does not define this layout ("nothing in this
; wave's rt/ reads individual stat fields yet"), so the first reader of a
; stat field defines it, and this is that reader.
DRV_STAT_SIZE   = 144
DRV_ST_MODE_OFF = 24
DRV_ST_SIZE_OFF = 48
DRV_S_IFMT      = 0xF000
DRV_S_IFREG     = 0x8000

; A bound on ONE file and, separately, on the whole unit: the arena is
; DRV_ARENA_FACTOR times the unit's bytes, so a unit of 256 maximum-size files
; would ask for a terabyte. Both refusals name the number they hit.
DRV_SRC_MAX      = 16 * 1024 * 1024
DRV_ARENA_FLOOR  = 4 * 1024 * 1024
DRV_ARENA_FACTOR = 384

drv_m_open   db 'cannot open: ',0
drv_m_stat   db 'cannot stat: ',0
drv_m_read   db 'cannot read: ',0
drv_m_notreg db 'not a regular file: ',0
drv_m_close  db 'cannot close: ',0
drv_m_wopen  db 'cannot open for writing: ',0
drv_m_write  db 'cannot write: ',0
drv_m_toobig db 'source is larger than this build accepts: ',0
drv_m_bigunit db 'the SOURCE files given add up to more than this build accepts in one compilation unit, bytes ',0
drv_m_bytes  db ', bytes ',0
drv_m_nomem  db 'cannot reserve memory for the compilation, errno ',0
drv_m_errno  db ', errno ',0

; drv_cstr_dup(arena, ptr, len) -> rax = a fresh `len + 1` byte copy of the
; `len` bytes at `ptr`, NUL-terminated, out of `arena`. FOR THE SYSCALL
; BOUNDARY ONLY -- `openat` takes a C string and nothing else in this
; compiler does. Never for identity or comparison; rt/intern.inc's IDs are
; what identity is.
;
; rt/str.inc's `str_to_cstr` is this routine, and is unreachable from the
; build for the reason driver/cli.inc's `drv_arg_is` records in full.
; Cannot fail (or `rassert`s transitively through `arena_alloc`); leaves CF
; clear.
proc drv_cstr_dup, arena, ptr, len
	uses	rbx
	locals
	endl
	mov	rdi, [arena]
	mov	rsi, [len]
	inc	rsi
	call	arena_alloc
	mov	rbx, rax
	mov	rdi, rbx
	mov	rsi, [ptr]
	mov	rcx, [len]
	cld
	rep	movsb
	mov	rcx, [len]
	mov	byte [rbx + rcx], 0
	mov	rax, rbx
	return
endp

; __drv_stat_size(drs_p, drs_n, drs_szo) -> eax = 1 with [drs_szo] set to the
; file's size in bytes, or eax = 0 having ALREADY said what went wrong. Opens,
; `fstat`s, checks that it is a regular file of an admissible size, and closes
; again -- pass 1 of drv_slurp_unit holds no descriptor across another file's
; open (see this file's header).
;
; A PREDICATE, deliberately, not the CF/eax channel: the failure has already
; been reported here, and what the caller needs back is the decision, not the
; errno. Same reasoning `drv_slurp_unit`'s 0 return carries.
proc __drv_stat_size, drs_p, drs_n, drs_szo
	uses	rbx, r12
	locals
		slot st,    DRV_STAT_SIZE
		slot cpath, dq
		slot fsize, dq
		slot erno,  dd
		slot pad0,  dd
	endl
	; A NUL-terminated copy for the kernel, out of the interner's arena
	; (never reset). argv's own bytes already carry a NUL, but this routine
	; takes a pointer and a length and must not assume one byte past the
	; length is readable, let alone zero.
	mov	rdi, [r15 + DrvCtx.iarena]
	mov	rsi, [drs_p]
	mov	rdx, [drs_n]
	call	drv_cstr_dup
	mov	[cpath], rax

	mov	rdi, DRV_AT_FDCWD
	mov	rsi, [cpath]
	mov	rdx, DRV_O_RDONLY
	xor	rcx, rcx
	call	sys_openat
	jc	.open_failed
	mov	rbx, rax		; fd, live until one of the exits below

	mov	rdi, rbx
	lea	rsi, [st]
	call	sys_fstat
	jc	.stat_failed

	mov	eax, [st + DRV_ST_MODE_OFF]
	and	eax, DRV_S_IFMT
	cmp	eax, DRV_S_IFREG
	jne	.not_regular

	mov	r12, [st + DRV_ST_SIZE_OFF]
	mov	[fsize], r12
	cmp	r12, DRV_SRC_MAX
	ja	.too_big

	mov	rdi, rbx
	call	sys_close
	jc	.close_failed

	mov	rax, [drs_szo]
	mov	[rax], r12
	mov	eax, 1
	return

  ; ---- the refusals -------------------------------------------------------
  ; Each names the file, ESCAPED (§8.3 -- a path is not ours, and one
  ; containing a bidi override must not be able to rewrite the message that
  ; reports it), and carries the kernel's own errno so the reason is not
  ; guessed at. Written out one block per case rather than funnelled through
  ; a shared tail: the shared version had to shuffle five arguments into
  ; place from three different branches and got them out of order.
  ;
  ; `eax` holds the wrapper's `-errno` at each of these labels
  ; (docs/asm-conventions.md, "1.3 Error protocol": the errno channel);
  ; `neg` turns it into the number a reader can look up.
  .open_failed:
	neg	eax
	mov	[erno], eax
	lea	rdi, [drv_m_open]
	mov	rsi, [drs_p]
	mov	rdx, [drs_n]
	lea	rcx, [drv_m_errno]
	mov	r8d, [erno]
	call	drv_msgu
	xor	eax, eax
	return

  .stat_failed:
	neg	eax
	mov	[erno], eax
	mov	rdi, rbx
	call	sys_close
	lea	rdi, [drv_m_stat]
	mov	rsi, [drs_p]
	mov	rdx, [drs_n]
	lea	rcx, [drv_m_errno]
	mov	r8d, [erno]
	call	drv_msgu
	xor	eax, eax
	return

  .close_failed:
	neg	eax
	mov	[erno], eax
	lea	rdi, [drv_m_close]
	mov	rsi, [drs_p]
	mov	rdx, [drs_n]
	lea	rcx, [drv_m_errno]
	mov	r8d, [erno]
	call	drv_msgu
	xor	eax, eax
	return

  .not_regular:
	mov	rdi, rbx
	call	sys_close
	lea	rdi, [drv_m_notreg]
	mov	rsi, [drs_p]
	mov	rdx, [drs_n]
	xor	rcx, rcx
	call	drv_msg
	xor	eax, eax
	return

  .too_big:
	mov	rdi, rbx
	call	sys_close
	lea	rdi, [drv_m_toobig]
	mov	rsi, [drs_p]
	mov	rdx, [drs_n]
	lea	rcx, [drv_m_bytes]
	mov	r8, [fsize]
	call	drv_msgu
	xor	eax, eax
	return
endp

; __drv_read_file(drs_p, drs_n, drs_dst, drs_cap, drs_got) -> eax = 1 with
; [drs_got] set to how many bytes were actually read, or eax = 0 having
; already said why. Opens the file named by `drs_p`/`drs_n`, reads at most
; `drs_cap` bytes into `drs_dst`, and closes.
;
; A LOOP, NOT ONE `read`: a short read is legal and this compiler has no libc
; stdio to hide that. It stops on 0 (EOF) rather than on having read `drs_cap`
; bytes, so a file that shrank since pass 1 yields what is actually there
; instead of trailing garbage -- and the caller advances its cursor by what
; came back, so the unit buffer stays an exact concatenation with no hole.
proc __drv_read_file, drs_p, drs_n, drs_dst, drs_cap, drs_got
	uses	rbx, r12, r13, r14
	locals
		slot cpath, dq
		slot erno,  dd
		slot pad0,  dd
	endl
	mov	rdi, [r15 + DrvCtx.iarena]
	mov	rsi, [drs_p]
	mov	rdx, [drs_n]
	call	drv_cstr_dup
	mov	[cpath], rax

	mov	rdi, DRV_AT_FDCWD
	mov	rsi, [cpath]
	mov	rdx, DRV_O_RDONLY
	xor	rcx, rcx
	call	sys_openat
	jc	.open_failed
	mov	rbx, rax		; fd

	mov	r12, [drs_dst]
	mov	r13, [drs_cap]
	xor	r14, r14		; bytes read so far
  .read:
	cmp	r14, r13
	jae	.read_done
	mov	rdi, rbx
	lea	rsi, [r12 + r14]
	mov	rdx, r13
	sub	rdx, r14
	call	sys_read
	jc	.read_failed
	test	rax, rax
	jz	.read_done		; EOF
	add	r14, rax
	jmp	.read
  .read_done:

	mov	rdi, rbx
	call	sys_close
	jc	.close_failed

	mov	rax, [drs_got]
	mov	[rax], r14
	mov	eax, 1
	return

  .open_failed:
	neg	eax
	mov	[erno], eax
	lea	rdi, [drv_m_open]
	mov	rsi, [drs_p]
	mov	rdx, [drs_n]
	lea	rcx, [drv_m_errno]
	mov	r8d, [erno]
	call	drv_msgu
	xor	eax, eax
	return

  .read_failed:
	neg	eax
	mov	[erno], eax
	mov	rdi, rbx
	call	sys_close
	lea	rdi, [drv_m_read]
	mov	rsi, [drs_p]
	mov	rdx, [drs_n]
	lea	rcx, [drv_m_errno]
	mov	r8d, [erno]
	call	drv_msgu
	xor	eax, eax
	return

  .close_failed:
	neg	eax
	mov	[erno], eax
	lea	rdi, [drv_m_close]
	mov	rsi, [drs_p]
	mov	rdx, [drs_n]
	lea	rcx, [drv_m_errno]
	mov	r8d, [erno]
	call	drv_msgu
	xor	eax, eax
	return
endp

; drv_slurp_unit(drs_tot) -> rax = a pointer to the WHOLE unit's bytes, with
; [drs_tot] set to how many there are; or rax = 0, having ALREADY said what
; went wrong. Reads every `DrvCtx.srcs` entry, in command-line order, into one
; contiguous buffer, and fills in each entry's `sptr`, `slen` and `sbase`.
; On success `DrvCtx.arena` at `r15` has been `arena_init`ed and owns it.
;
; THE 0 RETURN IS THE SENTINEL CHANNEL, not a fourth meaning for `CF`/`eax`.
; docs/asm-conventions.md's "1.3 Error protocol" lists four failure channels
; and this is the third of them ("an expected 'absent' result ... a sentinel;
; `CF` is not meaningful"): a successful `mmap`-backed allocation is never
; address 0, so 0 is unambiguous. It is NOT the errno channel, because this
; routine reports its own failure rather than handing an errno up: it is the
; only place that knows which of four syscalls failed, on which file, and on
; which pass, and a caller reconstructing that from a bare `-errno` would be
; guessing. What it does hand up is the decision -- the caller chooses the
; exit status.
;
; An empty file is a success contributing 0 bytes, not a failure; a unit whose
; files are all empty is a success with [drs_tot] = 0. §8.1 has nothing to say
; against a zero-byte source and the lexer tokenizes it to a single `TOK_EOF`;
; refusing it here would be this file inventing a rule.
;
; `rassert`s that at least one SOURCE was given: `drv_aedifica` checks
; `DrvCtx.nsrc` and reports §12's own message long before this call, so
; reaching here with an empty unit is an internal contract violation.
proc drv_slurp_unit, drs_tot
	uses	rbx, r12, r13, r14
	locals
		slot sz,    dq
		slot tot,   dq
		slot cur,   dq
		slot buf,   dq
		slot erno,  dd
		slot pad0,  dd
	endl
	mov	eax, [r15 + DrvCtx.nsrc]
	rassert	eax gt 0
	mov	qword [tot], 0

	; ---- pass 1: how many bytes is this unit? ---------------------------
	xor	rbx, rbx
  .sizes:
	cmp	ebx, [r15 + DrvCtx.nsrc]
	jae	.sized
	mov	rdi, rbx
	call	drv_src_ptr
	mov	r12, rax
	mov	rdi, [r12 + DrvSrc.spath]
	mov	rsi, [r12 + DrvSrc.spathn]
	lea	rdx, [sz]
	call	__drv_stat_size
	test	eax, eax
	jz	.refused		; it has already said why
	mov	rax, [sz]
	mov	[r12 + DrvSrc.slen], rax	; provisional -- pass 2 replaces
						; it with what was actually read
	add	[tot], rax
	mov	rax, [tot]
	cmp	rax, DRV_SRC_MAX
	ja	.unit_too_big
	inc	rbx
	jmp	.sizes
  .sized:

	; ---- the arena, sized from the unit ---------------------------------
	mov	rax, [tot]
	mov	rcx, DRV_ARENA_FACTOR
	mul	rcx			; rdx:rax; tot <= DRV_SRC_MAX so rdx = 0
	add	rax, DRV_ARENA_FLOOR
	mov	rdi, [r15 + DrvCtx.arena]
	mov	rsi, rax
	call	arena_init
	jc	.no_memory

	; An empty unit still gets a one-byte allocation, so the buffer pointer
	; is a real address and never collides with this routine's own
	; 0-means-failure sentinel.
	mov	rdi, [r15 + DrvCtx.arena]
	mov	rsi, [tot]
	test	rsi, rsi
	jnz	.have_size
	mov	rsi, 1
  .have_size:
	call	arena_alloc
	mov	[buf], rax

	; ---- pass 2: read them back to back ---------------------------------
	mov	qword [cur], 0
	xor	rbx, rbx
  .reads:
	cmp	ebx, [r15 + DrvCtx.nsrc]
	jae	.done
	mov	rdi, rbx
	call	drv_src_ptr
	mov	r12, rax
	mov	r13, [buf]
	add	r13, [cur]
	mov	[r12 + DrvSrc.sptr], r13
	mov	rax, [cur]
	mov	[r12 + DrvSrc.sbase], rax
	mov	rdi, [r12 + DrvSrc.spath]
	mov	rsi, [r12 + DrvSrc.spathn]
	mov	rdx, r13
	mov	rcx, [r12 + DrvSrc.slen]
	lea	r8,  [sz]
	call	__drv_read_file
	test	eax, eax
	jz	.refused
	mov	rax, [sz]
	mov	[r12 + DrvSrc.slen], rax
	add	[cur], rax
	inc	rbx
	jmp	.reads

  .done:
	mov	rax, [drs_tot]
	mov	rcx, [cur]
	mov	[rax], rcx
	mov	rax, [buf]
	return

  .refused:
	xor	eax, eax
	return

  .unit_too_big:
	lea	rdi, [drv_m_bigunit]
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	mov	r8, [tot]
	call	drv_msgu
	xor	eax, eax
	return

  .no_memory:
	neg	eax
	mov	[erno], eax
	lea	rdi, [drv_m_nomem]
	xor	rsi, rsi
	xor	rdx, rdx
	xor	rcx, rcx
	mov	r8d, [erno]
	call	drv_msgu
	xor	eax, eax
	return
endp

; drv_write_out(drs_p, drs_n, drs_bp, drs_bn) -> eax = 1 on success, or eax = 0
; having ALREADY said what went wrong. Creates or truncates the file named by
; the `drs_n` bytes at `drs_p` and writes the `drs_bn` bytes at `drs_bp` into
; it. This is spec 12's `-o OUT`, and OUT IS TEXT: `exsc` writes it and never
; assembles it, because `execve` is not on the syscall allowlist and locating
; an assembler by `PATH` is the ambient state 9.3 forbids.
;
; THREE SYSCALLS, ALL THREE ALREADY ON THE CLOSED ALLOWLIST and all three
; through rt/sys.inc: `openat(257)`, `write(1)`, `close(3)`. Nothing is added
; to the allowlist by being able to emit a program -- the fourth flag bit this
; file needed was a flag, not a syscall.
;
; A LOOP, NOT ONE `write`, for the same reason `__drv_read_file` loops: a short
; write is legal and there is no libc stdio here to hide it. A zero-byte return
; is treated as a failure rather than spun on -- unlike stderr, where
; `drv_say` swallows it, a truncated OUT is a wrong artifact and must be said.
;
; THE PREDICATE CHANNEL, not `CF`/`eax`: the failure is reported here, where
; which of three syscalls failed is still known, and what the caller needs back
; is the decision. Same reasoning `drv_slurp_unit`'s 0 return carries.
;
; A FAILED WRITE LEAVES A PARTIAL OUT ON DISK, stated rather than hidden: the
; file has already been created and truncated by the time a `write` can fail.
; Removing it would need `unlinkat`, which is not on the allowlist and is not
; worth adding for this -- the exit status is nonzero and says so, and the
; next successful run truncates it again.
proc drv_write_out, drs_p, drs_n, drs_bp, drs_bn
	uses	rbx, r12, r13, r14
	locals
		slot cpath, dq
		slot erno,  dd
		slot pad0,  dd
	endl
	mov	rdi, [r15 + DrvCtx.iarena]
	mov	rsi, [drs_p]
	mov	rdx, [drs_n]
	call	drv_cstr_dup
	mov	[cpath], rax

	mov	rdi, DRV_AT_FDCWD
	mov	rsi, [cpath]
	mov	rdx, DRV_OUT_FLAGS
	mov	rcx, DRV_OUT_MODE
	call	sys_openat
	jc	.open_failed
	mov	rbx, rax		; fd

	mov	r12, [drs_bp]
	mov	r13, [drs_bn]
	xor	r14, r14		; bytes written so far
  .write:
	cmp	r14, r13
	jae	.write_done
	mov	rdi, rbx
	lea	rsi, [r12 + r14]
	mov	rdx, r13
	sub	rdx, r14
	call	sys_write
	jc	.write_failed
	test	rax, rax
	jz	.write_stalled
	add	r14, rax
	jmp	.write
  .write_done:

	mov	rdi, rbx
	call	sys_close
	jc	.close_failed
	mov	eax, 1
	return

  ; ---- the refusals -------------------------------------------------------
  ; Each names the file ESCAPED, exactly as this file's read-side refusals do
  ; and for the same 8.3 reason: the path is the invoker's bytes, not ours.
  .open_failed:
	neg	eax
	mov	[erno], eax
	lea	rdi, [drv_m_wopen]
	mov	rsi, [drs_p]
	mov	rdx, [drs_n]
	lea	rcx, [drv_m_errno]
	mov	r8d, [erno]
	call	drv_msgu
	xor	eax, eax
	return

  .write_failed:
	neg	eax
	mov	[erno], eax
	mov	rdi, rbx
	call	sys_close
	lea	rdi, [drv_m_write]
	mov	rsi, [drs_p]
	mov	rdx, [drs_n]
	lea	rcx, [drv_m_errno]
	mov	r8d, [erno]
	call	drv_msgu
	xor	eax, eax
	return

  ; A write that returns 0 without setting CF has made no progress and will
  ; not on the next call either; treating it as success would report a
  ; complete artifact for a truncated file. Reported with errno 0, which is
  ; what the kernel gave.
  .write_stalled:
	mov	dword [erno], 0
	mov	rdi, rbx
	call	sys_close
	lea	rdi, [drv_m_write]
	mov	rsi, [drs_p]
	mov	rdx, [drs_n]
	lea	rcx, [drv_m_errno]
	mov	r8d, [erno]
	call	drv_msgu
	xor	eax, eax
	return

  .close_failed:
	neg	eax
	mov	[erno], eax
	lea	rdi, [drv_m_close]
	mov	rsi, [drs_p]
	mov	rdx, [drs_n]
	lea	rcx, [drv_m_errno]
	mov	r8d, [erno]
	call	drv_msgu
	xor	eax, eax
	return
endp
