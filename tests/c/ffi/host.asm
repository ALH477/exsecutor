; tests/c/ffi/host.asm -- the reference-side externus host for
; tests/programs/ffi_externus/. SPDX-License-Identifier: GPL-3.0-or-later
;
; The emitted program calls `bfausr_triplicare` (the reference emitter's
; spelling of the externus the source declares). The definition is the
; host's job -- same as the C side, where tests/c/ffi/host.c defines
; `triplicare`. Plain SysV AMD64: rdi in, rax out, matching the emitted
; `call bfausr_triplicare`.
;
; tests/run.sh injects this file into the executable segment before the
; emitted text when a program's TEST directive names externus-host=
; (the mechanism is run_program_tests's externus-host= lane).
bfausr_triplicare:
    mov rax, rdi
    add rax, rdi
    add rax, rdi
    ret
