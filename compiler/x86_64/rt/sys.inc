; compiler/x86_64/rt/sys.inc
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
; The ONLY file in this repo permitted to emit a raw `syscall` instruction
; (docs/asm-conventions.md §6 "Syscall discipline"; CLAUDE.md "The compiler
; is freestanding"). One wrapper per allowlisted syscall, closed set:
; read(0) write(1) close(3) fstat(5) lseek(8) mmap(9) munmap(11)
; openat(257) exit_group(231). `tools/syscall-audit.sh` checks the emitted
; BINARY against this exact set independently of this convention; routing
; every syscall through here is what keeps that audit meaningful to a human
; reader reviewing source, not what the audit itself trusts.
;
; Calling convention: plain SysV via macros/proc.inc, per
; docs/asm-conventions.md's calling-convention section. Every wrapper here
; is a thin, mechanical argument shuffle from the SysV arg registers
; (rdi,rsi,rdx,rcx,r8,r9) macros/proc.inc spills its named arguments from,
; into the Linux x86-64 raw-syscall arg registers -- rdi,rsi,rdx,**r10**,r8,r9.
; The 4th argument moves from rcx to r10 because `syscall` itself clobbers
; rcx (it holds the return address) and rcx is that ABI's 4th arg register
; under the C calling convention macros/proc.inc's own argument spilling
; uses -- the kernel's raw syscall ABI sidesteps that collision by using r10
; there instead. Since every argument here is read back from its spilled
; stack home (macros/proc.inc always spills before a body runs), there is
; no register-clobber hazard from the mismatch: `mov r10, [a4]` reads
; whatever `proc` put in that named argument's home slot, regardless of
; which register it arrived in.
;
; Failure-channel taxonomy (this wave's synthesis over CLAUDE.md/spec §13 --
; flagged as synthesis, not derived from prior text; see this wave's
; report and the note in macros/assert.inc's header):
;
;   Diagnosable about the source being compiled   -> CF set, eax = EXS-E
;                                                     numeric part (out of
;                                                     scope for this file --
;                                                     no wrapper here can
;                                                     see source-level intent)
;   Host/OS syscall failure (THIS FILE)            -> CF set, eax = the
;                                                     kernel's own negative
;                                                     return value (already
;                                                     -errno; no further sign
;                                                     flip needed)
;   Expected "absent" result (e.g. map_get miss)   -> sentinel; not this file
;   Internal contract violation (a compiler bug)   -> rassert trap; not
;                                                     this file (no wrapper
;                                                     here rasserts its own
;                                                     arguments -- a bad
;                                                     fd/pointer is the
;                                                     KERNEL's failure to
;                                                     report, via the error
;                                                     path above, not this
;                                                     file's to pre-validate)
;
; The Linux raw-syscall error convention: success is any rax outside
; [-4095,-1]; a return value in that range is `-errno`. Checked here as the
; standard unsigned trick, `cmp rax, -4096` / `ja .error` -- values -1
; through -4095, reinterpreted as unsigned 64-bit, are all numerically
; above -4096 (also reinterpreted), and every real success value (a valid
; pointer, fd, byte count, or signed-but-non-error offset) falls at or
; below it.
;
; INCLUDE ORDER, verified empirically: this file (and every rt/ file built
; on macros/proc.inc) must be `include`d AFTER `format ELF64 executable 3`
; has run, not before. `proc`/`endp` do not just define macros -- each
; `proc ... endp` pair in this file emits real instructions (`push rbp`,
; ...) the moment fasmg processes it, and those instructions need the x86
; mnemonic set (loaded transitively by `format ELF64 executable 3`, via
; vendor/fasmg-x86/format/elfexe.inc's own `include 'x64.inc'`) already
; active. Including this file between `include 'format/format.inc'` and
; `format ELF64 executable 3` fails with "symbol 'x86.mode' is undefined or
; out of scope" at the first `push` -- confirmed directly while writing
; this file's own test fixture. The working order every fixture in this
; tree uses:
;
;   include 'format/format.inc'
;   format ELF64 executable 3
;   entry start
;   include 'sys.inc'            ; (or any rt/ file built on proc.inc)
;   segment readable executable
;     start:
;       ...
;
; Spec: docs/spec/exsecutor-spec-v0.4.md §9.3 (the closed syscall allowlist
; is this document's mechanism for the purity contract's "no network
; access, ever, at any phase" -- no socket-family number appears below, and
; `make audit`/`tools/syscall-audit.sh` verify the binary, not just this
; source file, per CLAUDE.md).
; -----------------------------------------------------------------------------

include '../macros/proc.inc'

; sys_read(fd, buf, len) -> rax = bytes read (0 = EOF), CF clear on success;
;                           CF set, eax = -errno on failure.
proc sys_read, fd, buf, len
	locals
	endl
	mov	edi, [fd]
	mov	rsi, [buf]
	mov	rdx, [len]
	xor	eax, eax		; syscall nr 0 = read (self-zeroing idiom)
	syscall
	cmp	rax, -4096
	ja	.error
	return
  .error:
	mov	eax, eax		; keep only the low 32 bits (== eax already)
	stc
	jmp	.__proc_epilogue
endp

; sys_write(fd, buf, len) -> rax = bytes written, CF clear on success;
;                            CF set, eax = -errno on failure.
proc sys_write, fd, buf, len
	locals
	endl
	mov	edi, [fd]
	mov	rsi, [buf]
	mov	rdx, [len]
	mov	eax, 1			; write
	syscall
	cmp	rax, -4096
	ja	.error
	return
  .error:
	stc
	jmp	.__proc_epilogue
endp

; sys_close(fd) -> rax = 0, CF clear on success; CF set, eax = -errno.
proc sys_close, fd
	locals
	endl
	mov	edi, [fd]
	mov	eax, 3			; close
	syscall
	cmp	rax, -4096
	ja	.error
	return
  .error:
	stc
	jmp	.__proc_epilogue
endp

; sys_fstat(fd, statbuf) -> rax = 0, CF clear on success; CF set,
;                           eax = -errno. `statbuf` must point at a
;                           caller-owned buffer at least sizeof(struct stat)
;                           (144 bytes on x86-64 Linux) -- this file does not
;                           define that layout; nothing in this wave's rt/
;                           reads individual stat fields yet.
proc sys_fstat, fd, statbuf
	locals
	endl
	mov	edi, [fd]
	mov	rsi, [statbuf]
	mov	eax, 5			; fstat
	syscall
	cmp	rax, -4096
	ja	.error
	return
  .error:
	stc
	jmp	.__proc_epilogue
endp

; sys_lseek(fd, offset, whence) -> rax = new file offset, CF clear on
;                                  success; CF set, eax = -errno.
proc sys_lseek, fd, offset, whence
	locals
	endl
	mov	edi, [fd]
	mov	rsi, [offset]
	mov	edx, [whence]
	mov	eax, 8			; lseek
	syscall
	cmp	rax, -4096
	ja	.error
	return
  .error:
	stc
	jmp	.__proc_epilogue
endp

; sys_mmap(addr, len, prot, flags, fd, offset) -> rax = mapped address,
;                                                 CF clear on success;
;                                                 CF set, eax = -errno.
; This is rt/'s only source of backing memory -- there is no libc `malloc`
; (spec §6.3 "arena allocation via `alloc`"; CLAUDE.md, syscall allowlist).
proc sys_mmap, addr, len, prot, flags, fd, offset
	locals
	endl
	mov	rdi, [addr]
	mov	rsi, [len]
	mov	edx, [prot]
	mov	r10d, [flags]
	mov	r8d, [fd]
	mov	r9, [offset]
	mov	eax, 9			; mmap
	syscall
	cmp	rax, -4096
	ja	.error
	return
  .error:
	stc
	jmp	.__proc_epilogue
endp

; sys_munmap(addr, len) -> rax = 0, CF clear on success; CF set,
;                          eax = -errno.
proc sys_munmap, addr, len
	locals
	endl
	mov	rdi, [addr]
	mov	rsi, [len]
	mov	eax, 11			; munmap
	syscall
	cmp	rax, -4096
	ja	.error
	return
  .error:
	stc
	jmp	.__proc_epilogue
endp

; sys_openat(dirfd, pathname, flags, mode) -> rax = fd, CF clear on
;                                             success; CF set, eax = -errno.
; `dirfd` is AT_FDCWD (-100) for a plain relative-to-cwd or absolute path --
; callers pass it explicitly; this file has no ambient-cwd concept of its
; own (spec §9.3: no implicit anything).
proc sys_openat, dirfd, pathname, flags, mode
	locals
	endl
	mov	edi, [dirfd]
	mov	rsi, [pathname]
	mov	edx, [flags]
	mov	r10d, [mode]
	mov	eax, 257		; openat
	syscall
	cmp	rax, -4096
	ja	.error
	return
  .error:
	stc
	jmp	.__proc_epilogue
endp

; sys_exit_group(code) -- never returns.
proc sys_exit_group, code
	locals
	endl
	mov	edi, [code]
	mov	eax, 231		; exit_group
	syscall
endp
