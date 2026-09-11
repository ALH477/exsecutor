; tests/unit/bfa_verify_dot_ok.asm
; TEST: run=yes expect-exit=0 audit=skip
; Scratch smoke test while building verify.inc -- exercises passA (rules
; 1/6/7) + rule 9 against the @dot worked example from ssa-ir.md section
; 2.11 (the same text bfa_parse_dot.asm parses). Expect verdict 0.
include 'format/format.inc'
format ELF64 executable 3
entry start
; backend_fasmg/ir.inc no longer includes rt/ (the consumer brings it, as
; every other module's does), so this fixture brings the chain itself (span.inc includes intern.inc).
include '../../compiler/x86_64/rt/span.inc'
include '../../compiler/x86_64/backend_fasmg/verify.inc'
segment readable executable
  start:
	lea	rdi, [ar]
	mov	rsi, 16777216
	call	arena_init
	jc	.fail1
	lea	rdi, [scr]
	mov	rsi, 1048576
	call	arena_init
	jc	.fail1
	lea	rdi, [var]
	mov	rsi, 4194304
	call	arena_init
	jc	.fail1

	lea	rdi, [ar]
	lea	rsi, [scr]
	lea	rdx, [srctext]
	mov	rcx, srctext.len
	call	bfa_parse_module
	mov	[modout], rax

	mov	rdi, [modout]
	mov	rdi, [rdi + BfaModule.funcs]
	xor	rsi, rsi
	call	vec_get
	mov	rax, [rax]
	mov	[fn], rax

	mov	rdi, [modout]
	mov	rsi, [fn]
	lea	rdx, [var]
	lea	rcx, [outblk]
	lea	r8, [outinst]
	call	bfa_verify_func
	cmp	eax, 0
	jne	.fail2

	mov	eax, 231
	xor	edi, edi
	syscall

  .fail1: mov eax,231
	mov edi,11
	syscall
  .fail2: mov eax,231
	mov edi,12
	syscall

segment readable writeable
  ar: rb sizeof.Arena
  scr: rb sizeof.Arena
  var: rb sizeof.Arena
  modout: dq 0
  fn: dq 0
  outblk: dd 0
  outinst: dd 0
  srctext:
  db "functio @dot (ptr ptr u64) -> f32 numeri ad_parem vetita explicita conservata {", 10
  db "b0:", 10
  db "%0 = param ptr 0", 10
  db "%1 = param ptr 1", 10
  db "%2 = param u64 2", 10
  db "%3 = iconst u64 0", 10
  db "%4 = redinit f32 fadd ordinata 0", 10
  db "jmp b1", 10
  db "b1:", 10
  db "%5 = phi u64 b0 %3 b2 %11", 10
  db "%6 = cmp.lt u64 %5 %2", 10
  db "br %6 b2 b3", 10
  db "b2:", 10
  db "%7 = index %0 %5 4", 10
  db "%8 = load f32 %7 0 nativus", 10
  db "%9 = index %1 %5 4", 10
  db "%10 = load f32 %9 0 nativus", 10
  db "%11 = addw u64 %5 1", 10
  db "%12 = fmul f32 %8 %10", 10
  db "contrib %4 %12", 10
  db "jmp b1", 10
  db "b3:", 10
  db "%13 = redfin f32 %4", 10
  db "ret %13", 10
  db "}", 10
  .len = $ - srctext
