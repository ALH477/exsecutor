; tests/unit/indeterminate_syscall.asm
;
; Negative fixture: the syscall number is moved through a second register
; (`mov ebx,231` then `mov eax,ebx`) instead of being loaded into eax/rax
; directly. tools/syscall-audit.sh's linear sweep only trusts an immediate
; `mov` or a self-zeroing `xor`/`sub` to resolve rax; a register-to-register
; move is neither, so this must be reported INDETERMINATE and counted as a
; FAILURE -- not silently skipped, and not (incorrectly) resolved to 231
; by accident. This is the specific behavior the task brief this suite was
; built against calls out by name: "a register-indirect or computed rax
; must be reported as INDETERMINATE and treated as a FAILURE."
;
; Not executed (`run=no`): rax's real value at the syscall is, deliberately,
; whatever this fixture's author put in ebx -- exercising it at runtime
; proves nothing the assembly itself doesn't already show, and an
; indeterminate syscall site is exactly the kind of thing this suite
; shouldn't casually execute.
;
; TEST: run=no audit=fail

include 'format/format.inc'

format ELF64 executable 3
entry start

segment readable executable
  start:
	mov	ebx,231     ; exit_group's number -- but NOT loaded into eax
	mov	eax,ebx     ; register-to-register: not a resolving form
	xor	edi,edi
	syscall
