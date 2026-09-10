# Security

## What a security report is, here

Exsecutor's claims are about authority. The compiler is a pure function of
`(source, ego, lockfile, flags)` with no network access ever (spec §9.3), and
that is enforced by `make audit`, which disassembles `build/exsc` and fails on
any syscall outside a closed allowlist of nine. A compiled program's authority
is its declared capability closure (§4, §10.3), and the same audit in
`--potestates` mode checks a program's binary against the syscalls its atoms
admit. §2.4 of the spec is a table of things that are supposed to be
unrepresentable -- ambient locale in program logic, a bidi override in source,
a homoglyph identifier, byte order left implicit at a wire boundary, a
capability reaching a module-level mutable -- each with the error code that
rejects it.

The bug class that matters most is a program, or the compiler itself, reaching
ambient state that the audit or the type system did not show: a syscall the
audit missed or misresolved, a path past a §2.4 rejection, a way for output to
depend on something outside the declared inputs, an `EXS-E` code that should
fire and does not. A report of that shape is a security report even when the
program in question is a test fixture.

Ordinary crashes, wrong diagnostics, and missing features are issues, not
security reports, unless they open one of the paths above.

## How to report

Open a **GitHub security advisory** on this repository (the "Security" tab,
"Report a vulnerability"). Do not open a public issue for it. There is no
email address for security reports; do not use any address you find in the
tree.

Include the input, the command, the commit, and what you expected the audit or
the compiler to say. A reproduction that follows this project's evidence rules
-- a fixture that fails for the reason it claims -- is the most useful form.

## What to expect

This is one person's project with no security team and no service-level
promise. Advisories will be read and acknowledged, and a confirmed report gets
a fix, a regression fixture, and a commit message that says what was found and
what was run. If a fix needs a new error code, that is a spec amendment to §13
first (`CONTRIBUTING.md`). Credit is given in the fix unless you ask otherwise.
