; tests/unit/smoke.asm
;
; The toolchain smoke test: proves fasmg + vendor/fasmg-x86 can produce a
; freestanding static ELF64 that actually runs, independent of anything
; else in this repo. This is the exact program independently verified
; (fasmg src.asm out; chmod +x; run) before any tooling here existed:
; assembles to 241 bytes, prints its message, exits 0.
;
; NOTE on its audit verdict: this program ends with `mov eax,60` --
; exit(60), not exit_group(231). CLAUDE.md's closed syscall allowlist
; names exit_group specifically, not exit. tools/syscall-audit.sh is
; built against that literal allowlist, so it correctly reports this
; fixture as an audit FAIL (nr=60 "unknown", not in the closed list) even
; though the program itself is harmless. That is deliberate: it is kept
; exactly as originally verified (see tests/README.md) rather than quietly
; "fixed", and tests/unit/clean_syscalls.asm is the fixture that uses
; exit_group and is expected to pass. This is not a bug in the fixture or
; in the audit -- it is the closed-allowlist model working as designed:
; "harmless" is not the same claim as "allowlisted".
;
; TEST: run=yes expect-exit=0 audit=fail

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	mov	eax,1
	mov	edi,1
	lea	rsi,[msg]
	mov	edx,msg.len
	syscall
	mov	eax,60
	xor	edi,edi
	syscall

segment readable writeable
  msg db 'exsecutor: fasmg toolchain live',10
  .len = $ - msg
