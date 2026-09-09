; tests/unit/sys_syscalls.asm
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
; asm-rt fixture for compiler/x86_64/rt/sys.inc.
;
; Exercises every wrapper in the file at least once, both the success path
; (CF clear) and the host-failure path (CF set, eax = -errno):
;
;   1. sys_write to stdout, return value equals the byte count requested
;   2. sys_openat on a path that must exist on any Linux system
;      (`/dev/null`) -- success, fd >= 0
;   3. sys_read from that fd -- `/dev/null` always reads 0 bytes (EOF), and
;      must not set CF (0 bytes is a valid, successful read count, not an
;      error)
;   4. sys_lseek on that fd -- must not set CF
;   5. sys_close on that fd -- must not set CF
;   6. sys_openat on a path that cannot exist -- CF set, eax = -ENOENT (-2)
;      (the host-failure channel this wave's report documents: CF set,
;      eax = the kernel's own negative return value, scoped to rt/sys.inc)
;   7. sys_mmap a page, write through the returned pointer, read it back,
;      then sys_munmap it -- round-trips this compiler's only source of
;      backing memory (there is no libc `malloc`)
;
; `rt/sys.inc` must be `include`d AFTER `format ELF64 executable 3` (see
; that file's own header for why -- its `proc`/`endp` pairs emit real
; instructions immediately, which need the x86 mnemonic set already
; loaded).
;
; Exit 0 = all checks passed; 10+N = check N failed (see tests/README.md).
;
; TEST: run=yes expect-exit=0 audit=pass

include 'format/format.inc'

format ELF64 executable 3
entry start

include '../../compiler/x86_64/rt/sys.inc'

segment readable executable
  start:
	; check 1: sys_write
	mov	rdi, 1
	lea	rsi, [msg]
	mov	rdx, msg.len
	call	sys_write
	jc	.fail1
	cmp	rax, msg.len
	jne	.fail1

	; check 2: sys_openat success (/dev/null always exists on Linux)
	mov	edi, -100		; AT_FDCWD
	lea	rsi, [devnull]
	xor	edx, edx		; O_RDONLY
	xor	ecx, ecx
	call	sys_openat
	jc	.fail2
	cmp	eax, 0
	jl	.fail2
	mov	ebx, eax		; stash the fd -- rbx, NOT r15: r15 is
					; pinned as the compilation-context
					; pointer (docs/asm-conventions.md
					; "1.2 The r15 pin") and this fixture
					; keeps to that discipline even though
					; no real context is live here, so nothing
					; that copies this file learns the wrong
					; habit. rbx is safe: none of rt/sys.inc's
					; wrappers declare a `uses` list (they
					; touch only caller-saved registers), so
					; nothing here clobbers it.

	; check 3: sys_read from /dev/null -- 0 bytes, must NOT set CF
	mov	edi, ebx
	lea	rsi, [readbuf]
	mov	rdx, 16
	call	sys_read
	jc	.fail3
	cmp	rax, 0
	jne	.fail3

	; check 4: sys_lseek on that fd -- must NOT set CF
	mov	edi, ebx
	xor	rsi, rsi
	xor	edx, edx		; SEEK_SET
	call	sys_lseek
	jc	.fail4

	; check 5: sys_close -- must NOT set CF
	mov	edi, ebx
	call	sys_close
	jc	.fail5

	; check 6: sys_openat failure -- CF set, eax = -ENOENT
	mov	edi, -100
	lea	rsi, [nosuchfile]
	xor	edx, edx
	xor	ecx, ecx
	call	sys_openat
	jnc	.fail6
	cmp	eax, -2
	jne	.fail6

	; check 7: sys_mmap / write-through / sys_munmap
	xor	rdi, rdi
	mov	rsi, 4096
	mov	edx, 3			; PROT_READ|PROT_WRITE
	mov	ecx, 0x22		; MAP_PRIVATE|MAP_ANONYMOUS
	mov	r8d, -1
	xor	r9, r9
	call	sys_mmap
	jc	.fail7
	mov	rbx, rax
	mov	dword [rax], 0xDEADBEEF
	cmp	dword [rax], 0xDEADBEEF
	jne	.fail7
	mov	rdi, rbx
	mov	rsi, 4096
	call	sys_munmap
	jc	.fail7

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
  .fail6: mov eax,231
	mov edi,16
	syscall
  .fail7: mov eax,231
	mov edi,17
	syscall

segment readable writeable
  msg db 'sys.inc: exercised', 10
  .len = $ - msg
  devnull db '/dev/null', 0
  nosuchfile db '/no/such/file/exsecutor-test', 0
  readbuf: rb 16
