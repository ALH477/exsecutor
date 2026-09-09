# Exsecutor — Language Specification v0.3

**Status:** design complete, nothing implemented. Consolidates v0.1, Addenda A–C, Stage 0 measurements, the adversary audit, and the capability-row prototype. Supersedes all prior documents. v0.3 retires the v0.1–v0.2 placeholder name and records the implementation decision in §18; no design decision above §18 changed.

**Evidence base:** every load-bearing claim in this document is backed by a benchmark, a prototype, or a cited CVE. Claims that are not are marked `[OPEN]` or `[UNTESTED]`. Three earlier versions asserted things that measurement then contradicted; the practice since has been that prose designs are hypotheses until code runs.

**Name:** resolved. *Exsecutor* — Latin, "one who carries out."

The name is well-formed under this document's own §3 rules, which is the reason it was chosen:

| part | rule | note |
|---|---|---|
| `ex-` | §3.5 prefix, "out of" | |
| `secut-` | §3.3 root table, supine of `sequ-` "follow" | already in the published table |
| `-or` | §3.4 suffix, supine stem + agent | |

§3.6 concatenates without assimilating: `ex` + `secut` + `or` = `exsecutor`. That is also the classical Latin spelling, so unlike `conlege`, `transscribe`, and `inlege` the name carries none of the deliberate wrong-Latin friction. §3.8 caps composition at two affixes; this is exactly two. §17 names §3 as the largest adoption risk in the project — a language that can name itself in its own lexicon is worth the sentence it takes to say so.

**Collision check**, discharging the instruction the v0.2 placeholder carried. "Exsecutor" as an exact string is unclaimed: no language, compiler, or notable project. The anglicised "Executor" is heavily overloaded — Java's `Executor`, the C++ executors proposal, Spark executors, the Mac 68k emulator — so the Latin `-s-` spelling is search-unique at the accepted, recorded cost of being autocorrected constantly. The retired placeholder "Nomos" collided with a CMU research language (Das & Hoffmann, resource-aware session types).

**What was given up.** "Nomos" was chosen because νόμος (convention) opposes φύσις (nature) — the classical distinction between what holds by human agreement and what is intrinsic, which is §1's thesis in one word. *Exsecutor* means "enforcer," not that distinction. It fits a compiler whose entire character is refusing to compile, but it is a weaker thematic fit than the name it replaces. Recorded rather than argued away.

**Extension:** `.xsc` — **Interface file:** `ego.xsc`

---

# 1. Thesis

> **Locale and target are capabilities, never ambient state.**

No operation may implicitly read the host locale, encoding, byte order, pointer width, clock, or environment. If a computation depends on a human language or on a machine, that dependency appears in its signature or the program does not compile.

This cannot be a library — the point is closing the implicit path. It cannot be a lint — lints are opt-in and do not compose across dependencies. It has to be in the type system and the module system.

The same rule turned on the compiler makes Exsecutor Nix-native rather than Nix-compatible. A compiler with no ambient authority is a pure function of its inputs by construction.

**A second, independent thesis** (§3): public API names are a checked artifact, not free text. This is where the Latin/Greek does real work — the layer the evidence identifies as the actual barrier for non-English speakers.

## 1.1 Non-goals

- Not a type-theory research vehicle. Nothing here needs a novel type system.
- No borrow checking. No linear types. See §6.
- Not multilingual in keywords. One canonical ASCII lexicon, no aliases, no dialects. The AppleScript and Excel evidence is decisive.
- Not optimising for terseness. Dense notation is a documented readability loss.
- Not a DSP language, though it must be a good host for one.

## 1.2 Prior art

The capability mechanism is not new. Honest accounting:

- **Object capabilities / ambient authority** — Dennis & Van Horn (1966); Mark Miller's E.
- **Austral** (Borretti) — capability-based effects in a systems language.
- **Pony** — capabilities-secure, `Env` passed to `Main`, no ambient authority.
- **Koka, Eff, Frank** — effect rows; §4.2 is a restricted form.
- **Swift** — ARC, value types, witness tables, resilient-by-default separate compilation.
- **Ada** — representation clauses: the best prior art for declared byte order and layout.
- **Erlang** — bit syntax with endianness in the pattern language.
- **Rust** — UTF-8 `str` with no integer indexing, `--remap-path-prefix`, `confusable_idents`.
- **Haskell `ST` / Ghosts of Departed Proofs** (Noonan 2018), Rust's `generativity` — branded types (§5.1).
- **Nix** — the build model and the `build`/`host`/`target` trichotomy.
- **Zig** — cross-compilation as default rather than special mode.
- **Esperanto** — regular productive derivation (§3); **Toki Pona** — the opacity failure mode it must avoid.

The novel combination: object-capability discipline applied to *locale, encoding, and target* rather than only I/O; a build model derived from the same rule; and a morphologically checked public lexicon.

---

# 2. Falsifiability

The design claims specific bug classes become unrepresentable. Stage 0 tested whether those classes are real.

## 2.1 Strong — verified, current, exploited

**Locale-dependent case folding and Unicode normalization.** Producing unauthenticated RCEs now, not historically.

- **CVE-2025-49003** (DataEase): validation before case conversion fails to recognise Turkish dotless ı (U+0131 → ASCII I) and long ſ (U+017F → ASCII S); post-conversion processing treats them as ASCII forming a payload. Network, unauthenticated, no user interaction, RCE.
- **CVE-2026-24895** (FrankenPHP): case folding changes UTF-8 byte length, shifting a path split so `SCRIPT_FILENAME` points at an uploaded `.txt`. The lesson given — *never compute an index on a transformed string and apply it to the original* — covers two rows of this table at once.
- **GitHub auth bypass**: password reset tokens keyed on email; an address normalizing to another delivers one user's token to another account.

**This class leads the design and the pitch.**

## 2.2 Moderate — real but table stakes

**Trojan Source** (CVE-2021-42574 bidi, CVE-2021-42694 homoglyph). Universal across compilers, but severity is contested — Rapid7 scores it ~5.6 against the assigned 9.8, Red Hat rated both Moderate, exploitation needs repo or CI access, and no in-the-wild exploitation was found. Rust shipped mitigations in 1.56.1; GCC has `-Wbidirectional`.

Rejecting bidi at the lexer is correct and cheap. **It is not a differentiator.**

## 2.3 Weak — retained, demoted

**Endianness at wire boundaries.** Best evidence is CVE-2024-6284 (`google/nftables` encoded IP addresses in the wrong byte order, producing rules that failed to block intended addresses). One good CVE. Byte order in the type costs nothing and prevents a real class, but it is not a headline.

## 2.4 The unrepresentability table

| Class | Mechanism | Error |
|---|---|---|
| Locale case fold in program logic | `plica_unicode` total and locale-free; `plica_sermone` requires `sermo` | — |
| Index computed on transformed text | Branded offsets (§5.1) | `EXS-E0332` |
| Bidi source attack | Lexer rejects unbalanced controls | `EXS-E0103` |
| Homoglyph identifier attack | UTS #39 over import closure | `EXS-E0105` |
| NFC/NFD identifier divergence | Source must be NFC | `EXS-E0102` |
| Decimal separator drift in serialization | `sermo` absent from machine paths | — |
| Endianness at a wire boundary | Byte order in the type | `EXS-E0321` |
| Padding disclosure in wire structs | `@transitus` forbids implicit padding | `EXS-E0322` |
| Byte-index panic / grapheme miscount | `octeti`/`scalares`/`grapha` distinct | `EXS-E0311` |
| Ambient authority | §4 capability model | `EXS-E0421`, `E0500`, `E0501`, `E0510` |
| Refcount race via FFI | Non-atomic `refero` cannot cross `externus` | `EXS-E0520` |
| Refcount overflow → UAF | 64-bit saturating refcounts | runtime abort |
| Build nondeterminism from host state | §9 purity contract | — |

---

# 3. The lexicon

**This is where the Latin/Greek is load-bearing.** Prototype: `prototypes/lexicon/lexicon.py`.

## 3.1 Rule

> Every **public** name must decompose into (prefixes + root + suffix) drawn from a declared, versioned morpheme table. The compiler checks the decomposition against the declared type.

Locals, private functions, and struct fields are free-form. Same principle as capabilities: explicit at boundaries, free inside.

## 3.2 Why this layer

Guo (CHI 2018, 840 respondents, 86 countries, 74 native languages) found the code-reading barrier concentrated in **identifiers and API names**, not the ~40 reserved words. v0.1 put Latin in the keywords — the layer that doesn't matter, which is why it was decorative and why English keywords could be swapped in with no loss.

## 3.3 Roots

Latin verbs carry **two stems**, present and supine. Affixes attach to one or the other: the agent of `leg-` is `lector`, not `lecttor`. Every root declares both.

| root | supine | gloss |
|---|---|---|
| `leg-` | `lect-` | read |
| `scrib-` | `script-` | write |
| `sec-` | `sect-` | cut |
| `plic-` | `plicat-` | fold |
| `numer-` | `numerat-` | count |
| `ordin-` | `ordinat-` | order |
| `sequ-` | `secut-` | follow |
| `cap-` | `capt-` | take |
| `pon-` | `posit-` | place |
| `ten-` | `tent-` | hold |
| `vert-` | `vers-` | turn |
| `mitt-` | `miss-` | send |
| `iung-` | `iunct-` | join |
| `solv-` | `solut-` | loosen |

Greek roots cover what Latin lacks: `crypt-`, `graph-`, `metr-`, `morph-`, `chron-`, `top-`, plus the combining forms `poly-`, `mono-`, `iso-`, `auto-`.

## 3.4 Suffixes carry type contracts

| suffix | stem | kind | must declare |
|---|---|---|---|
| `-e` | present | the action | `functio` |
| `-or` | supine | agent | `structura` |
| `-ibilis` | present | can-be-Xed | `interfacies` |
| `-io` | supine | reified operation | `structura` |
| `-us` | supine | result | `typus` |
| `-orium` | supine | instrument | `structura` |

A name whose suffix disagrees with its declaration is `EXS-E0602`. This is a well-formedness condition relating a name to a type, not a style lint.

## 3.5 Prefixes carry signature laws

| prefix | gloss | law |
|---|---|---|
| `re-` | again | same signature as the bare form |
| `de-` | reverse | base's return type becomes first parameter |
| `ex-` | out of | first parameter is the source type |
| `in-` | into | first parameter is the destination type |
| `trans-` | across | parameter and return types differ |
| `con-` | together | takes two or more of the base's operand type |
| `prae-`, `sub-` | before, under | positional only |

Violating a prefix law is `EXS-E0603`.

Prefix laws constrain **`functio` signatures**. On a `structura`, `typus`, or `interfacies` declaration a prefix is positional and semantic only — there is no signature for it to bind. (The language's own name is an `-or` agent noun carrying `ex-`; see §Name.)

## 3.6 No assimilation — deliberately

Real Latin assimilates: `con-` + `leg-` → *collega*; `trans-` + `scrib-` → *transcribe*; `in-` + `leg-` → *illeg-*. **Exsecutor concatenates without assimilating**: `conlege`, `transscribe`, `inlege`.

This is wrong Latin on purpose. Assimilation destroys guessability and greppability, which are the two properties the scheme exists to provide.

## 3.7 What this buys

**Derivability.** One root plus the table yields the family without lookup:

```
lege        functio      read
lector      structura    reader
legibilis   interfacies  can be read
lectio      structura    a read operation
lectus      typus        read result
lectorium   structura    read instrument
relege      functio      again read
```

**Mechanical glossing.** Each morpheme has a gloss per language, so the whole API renders anywhere by table lookup — `exsc documenta --sermo ja` is not a translation project:

```
lector       en=reader              es=el que leer          ja=読む器
legibilis    en=can be read         es=se puede leer        ja=読むことができる
lectio       en=a read operation    es=operación de leer    ja=読む操作
```

Requires per-language frames for **suffixes as well as roots**; root-only glossing leaks English (`es=leer-er`).

**ISV by construction.** The generated forms — `versio`, `positio`, `sectio`, `functio`, `solutio`, `lector`, `scriptor`, `iunctio` — are already international scientific vocabulary. The Advanced Research finding was that Latin/Greek is more universal than English *only within ISV*; derivation lands names inside ISV automatically. The original premise, narrowed to where the evidence supports it.

**Why English cannot.** English derivation is irregular and unproductive; its productive technical affixes (`-able`, `-ation`, `re-`, `de-`, `-ize`, `poly-`) are themselves Latin and Greek borrowings. A regular scheme in English is a Latin scheme with English roots — losing the ISV property.

## 3.8 Constraints

- Composition depth: **two affixes maximum**. The Toki Pona failure (`kili jelo` for banana) is opacity through unbounded composition.
- The morpheme table is a content-addressed, versioned dependency in every `ego`.
- `[OPEN]` **Root coinage governance.** There is no Latin for hash, socket, or mutex. They are coinable (`dispersio`, `receptaculum`, `exclusor`) but coining requires judgment and judgment requires a process. Unsolved.

---

# 4. Capabilities

Prototype: `prototypes/capcheck/exsecutor_check.py`, 468 lines, validated against six attacks and two legitimate programs. `[UNREPRODUCED]` — that artifact is absent from the tree. The checker is being rebuilt from this section, which is a re-derivation, not a restoration.

## 4.1 Rules

1. Capability types are **unforgeable**. No literal, no cast, no default.
2. The only root is the `Mundus` passed to `initium`. All others derive from it, explicitly and fallibly.
3. Capability sets are part of **`functio` types**, not only declarations.
4. **A capability becomes available in a scope in exactly three ways — bound by `sub`, received as a parameter, or held in a field of the receiver — and all three are visible in the interface.** `poscit` means *drawn from the enclosing scope*, nothing else.
5. `publica` functions declare `poscit` explicitly. Private functions infer it from their bodies, transitively.
6. A function with no `poscit` and no capability parameters is **pure with respect to ambient state**. It may allocate and diverge; it may not observe the host.
7. **No module-level mutable state.** (`EXS-E0500`; with a capability, `EXS-E0501`.)

## 4.2 Rows and substitution

A higher-order function requires whatever its function argument requires:

```exsecutor
publica functio applica(v: f32, f: functio(f32) -> f32) -> f32 poscit alloc, sicut f {
    redde f(construe(v))
}
```

> **Substitution rule.** A row variable in a callee's `poscit` names one of the **callee's** parameters. At each call site it is substituted **positionally** with the capability row of the actual argument.

Without this, unioning the callee's row propagates a name with no referent in the caller (the prototype produced `requires [sicut f]` inside a function having no `f`). Substitution is what makes laundering impossible rather than merely annotated:

```exsecutor
publica functio nocens(x: f32) -> f32 poscit rete { … }

publica functio exterior(v: f32) -> f32 poscit alloc {
    redde applica(v, nocens)      // EXS-E0421: requires [rete], undeclared
}
```

`[OPEN]` **Closure capture is unimplemented.** The prototype handles named functions and function-typed parameters. A lambda capturing a capability from an enclosing `sub` is the sharpest form of this attack and is untested. Highest-priority remaining work.

## 4.3 Capability-bearing types

A type with a capability-typed field, transitively, is **capability-bearing**, must be declared so, and the mark propagates into the `ego`'s `potestates`.

```exsecutor
structura ScriptorRetis { sock: rete }      // capability-bearing: [rete]
```

## 4.4 Dynamic dispatch

`dyn Trait poscit P`. Constructing a trait object from an implementation whose mark exceeds `P` is `EXS-E0510`.

Trait capability needs otherwise dissolve into the receiver — a file writer's authority *is* the handle in its field — so `poscit` on trait methods should be rare. If you need one, the capability is probably misplaced. But that convenience is exactly what would hide capabilities from the audit, hence the bound.

## 4.5 Ergonomics

```exsecutor
potestas Hospes = { alloc, archivum, horologium, ambitus }
```

`sub` binds for a scope; a statement form (`sub alloc = a;` to end-of-scope) exists alongside the block form so programs are not permanently indented.

`sub` resolution is **lexical and non-searching**: one value per capability type per scope, shadowing is an error, no implicit conversion, no inference across module boundaries. If it grows a search algorithm, it has failed.

## 4.6 The capability set

`Mundus` (root), `alloc`, `sermo` (human language), `horologium` (clock), `archivum` (filesystem), `rete` (network), `fortuna` (randomness), `ambitus` (process environment), `Filum` (threads), `Crudum` (raw pointers, unchecked casts, manual lifetimes).

`alloc` appears in roughly 70% of non-kernel `poscit` clauses and therefore carries little signal; the audit view suppresses it by default (§10.3).

---

# 5. Types

## 5.1 Text and branded offsets

```exsecutor
firma t: textus = "café"
t.octeti().numerus()      // 5   bytes
t.scalares().numerus()    // 4   scalar values
t.grapha().numerus()      // 4   grapheme clusters
t[0]                      // EXS-E0311: textus has no integer index
```

UTF-8 storage. Slicing by byte offset, O(1), returns `eventus`, never panics. `octeti`, `scalares`, `grapha` are distinct types, not coercing views. Grapheme segmentation ships in the standard library.

**Branded offsets.** `quaere` returns `positio<'t>`, generatively branded to the buffer it indexed. `sectio` accepts only its own brand:

```exsecutor
firma abassus = via.plica_unicode()
firma ubi     = abassus.quaere("/")?      // positio<abassus>
firma pars    = via.sectio(0..ubi)?       // EXS-E0332: branded to `abassus`
```

There is no conversion from `positio<'t>` to bare `mensura` outside `Crudum`. This makes CVE-2026-24895's class unrepresentable rather than discouraged — the highest-value change the CVE research produced.

**Case folding:**

```exsecutor
"I".plica_unicode()        // "i" — always, no capability, no locale
"I".plica_sermone(sermo)   // "ı" under tr-TR — requires the capability
```

Collation follows the same shape: `ordina_binarie()` total, `ordina_sermone(sermo)` capability-gated.

## 5.2 Integers, byte order, `@transitus`

Byte order is in the type. `:nativus` is legal in-process and **illegal in any `@transitus` type** (`EXS-E0321`).

```exsecutor
@transitus
publica structura Capitulum {
    signum:    u32:maior
    versio:    u8
    genus:     u8
    reserva:   u16:maior      // explicit; implicit padding is EXS-E0322
    longitudo: u32:maior
}
```

`@transitus` types may have **no implicit padding** — explicit `reserva` fields or rejection. This closes the uninitialized-memory disclosure class.

`mensura` (`usize`) is target-dependent. Mixing it with a fixed-width type requires an explicit widening that names the target assumption.

## 5.3 FFI

```exsecutor
externus("C", abi: sysv_amd64) {
    functio strlen(s: *octetus) -> mensura
}
```

Explicit ABI and layout; no "whatever C does." The frontend implements the C ABI itself — unavoidable, and the reason QBE (which implements it in full) is attractive as a backend.

**Non-atomic `refero` may not cross an `externus` boundary** (`EXS-E0520`) — see §6.4.

Minimum ABI coverage for v1: SysV AMD64, AArch64 AAPCS, RISC-V lp64d.

---

# 6. Memory

## 6.1 Model

**ARC. Value types by default. Deterministic destruction always. Arenas via `alloc`. No tracing GC.**

Nix pulls two ways: cross-compilation and closure size want a small runtime; separate compilation and content-addressed early cutoff want uniform representation. ARC plus dictionary-passing generics satisfies both. Swift is the existence proof.

**Both Rust features that cost the most — monomorphization and borrow checking — are the two that fight Nix alignment hardest.** Dropping both is coherent, not a compromise. Result: Swift-shaped semantics on a Nix-shaped build model, and 8–12 months off the schedule.

## 6.2 Measured evidence

Xeon 2.10 GHz, gcc 13.3, `-O2`. Sources in `prototypes/stage0-bench/` — `[UNREPRODUCED]`, the sources are absent from the tree and the figures below are carried forward from v0.2 unverified. All variants verified **bit-identical** by numpy and confirmed by objdump to compile to identical arithmetic (9 scalar FP ops, no vectorization in any variant).

**Raw refcount cost:**

| | cycles |
|---|---|
| non-atomic retain/release pair | **~2.9** |
| atomic retain/release pair | **~25.4** (8.74×) |

**DSP inner loop** — 4-stage biquad, 512-sample blocks, 51.2M samples:

| variant | ns/sample | vs baseline |
|---|---|---|
| no refcounting | 7.007 | — |
| retain/release per **callback** | 6.993 | **−0.20%** |
| retain/release per **sample**, atomic | 15.590 | **+122%** |
| retain/release per **sample**, non-atomic | 6.889 | **−1.69%** |

**Object graph** — 4.19M-node AST, build + 8 traversals + teardown:

| variant | vs arena | vs malloc |
|---|---|---|
| arena, borrowed walk | — | — |
| malloc/free, borrowed walk | +179% | — |
| ARC atomic, borrowed walk | +308% | **+46%** |
| ARC non-atomic, borrowed walk | +159% | **−7%** |
| ARC atomic, *retaining* walk | +654% | +170% |

## 6.3 The six decisions, ordered by measured effect

1. **Arena allocation via `alloc`.** 2.8× over malloc — larger than every refcounting effect combined. `Arena.reconde()` resets in O(1). Documentation leads with **preallocate-and-reset**; creating an arena inside an audio callback is itself an allocation.
2. **Non-atomic refcounts where nothing crosses a thread boundary.** The difference between +46% and free. Load-bearing, not an optimization.
3. **Parameters are borrowed by default** (`guaranteed` convention). Retaining on each visit costs +85%.
4. **`structura` is a value type. Always.** No implicit boxing.
5. **Arrays of value types are unboxed and contiguous.** `acies<f32>` is a flat buffer.
6. **Reference semantics are explicit**: `refero<T>`.

## 6.4 Reference types and FFI

- `refero<T>` — non-atomic. **Cannot cross `externus`** (`EXS-E0520`). A foreign thread racing a non-atomic refcount is a use-after-free, and the type system cannot see the escape.
- `refero_communis<T>` — atomic. May cross.
- A module holding both `Crudum` and non-atomic references is flagged loudly in the audit.

## 6.5 Refcount representation

**64-bit on every target, including ilp32, saturating with hard abort.** A 32-bit refcount overflows at 2³², which is reachable — CVE-2016-0728 (Linux keyring) is exactly this, overflow → UAF → local root. RISC-V ilp32 is in scope; the target's word size does not get to choose.

## 6.6 Destruction

RAII everywhere. Destructor **resurrection** — re-referencing an object under destruction — aborts.

## 6.7 Accepted costs

- Object-graph code pays **+46%** over manual malloc/free with atomic refcounts, free with non-atomic. Both documented honestly.
- **Reference cycles leak.** No collector. Weak references and discipline. Attacker-controlled input inducing cycles is a DoS vector; arenas are the recommended pattern for cycle-prone parsing of untrusted input. `[OPEN]`, no fix available.
- Cross-module ARC elision is limited by dictionary-passing generics — a genuine interaction cost between two independently correct decisions.

---

# 7. Generics and modes

## 7.1 Generics: dictionary passing

Generic code compiles **once**, using witness tables. Monomorphization is a link-time optimization within a compilation closure, never a semantic requirement.

- A module's artifact does not depend on consumers' instantiations → content addressing works.
- Editing a generic body without changing its interface does not rebuild dependents.
- Stable ABI becomes achievable, which matters for dynamic linking under Nix.
- Cost: dictionary indirection in unspecialized code; recovered at link time where it matters.

`[OPEN]` The three-way interaction of generics × capability rows × dictionary layout is unprototyped. Capabilities are values, so a required row can travel in the witness table — but that makes dictionary layout depend on the row.

## 7.2 Modes

> **Modes may remove permissions. Modes may never change representation.**

Restriction modes work (Rust's `unsafe`, `use strict`, D's `@safe`): the restricted subset is valid in the permissive superset, so the ecosystem never splits. Representation modes fracture (D's `-betterC` breaks most of Phobos; Nim's `--mm` is whole-program precisely because per-module mixing was intractable). Haskell's 100+ `LANGUAGE` pragmas are the cognitive-tax evidence; Racket's `#lang` succeeds only because the runtime beneath is uniform.

**Capabilities already are the mode mechanism. Do not build a second one.** "C mode" is a function requesting no `alloc` — it compiles into an ISR, a WASM sandbox, and a desktop app unchanged, because representation never varies.

| contention | verdict |
|---|---|
| C: manual memory, no runtime | supported — as absence of `alloc` |
| C++: RAII | always on, not a mode |
| C++: monomorphization | rejected as default (§7.1) |
| Haskell: purity | already present; extended to allocation |
| Haskell: laziness | rejected — thunks are a representation change. Offered as a type, `pigra<T>` |
| Haskell: dictionary passing | adopted (§7.1) |
| Rust: `unsafe` | adopted as `potestas Crudum` |
| Rust: ownership/borrowing | deferred. ARC does not preclude adding opt-in ownership later, as Swift did |

---

# 8. Lexical layer and diagnostics

## 8.1 Source

- **UTF-8 only.** No BOM — a BOM is `EXS-E0101`, not a skipped byte.
- **LF only.** CRLF is `EXS-E0106`. The formatter converts; the compiler does not.
- **NFC required.** Non-NFC is `EXS-E0102`.
- Bidi and invisible controls (U+202A–202E, U+2066–2069, U+200B–200F, U+061C) anywhere in source, **including strings and comments**, are `EXS-E0103`. Escapes are the only way to produce them.

## 8.2 Identifiers

- UAX #31 `XID_Start`/`XID_Continue` plus `_`.
- UTS #39 **Moderately Restrictive**. Mixed-script is `EXS-E0104`.
- **Confusable detection is scoped to the import closure**, not the compilation unit (`EXS-E0105`). Module A exporting Cyrillic `аdd` and module B calling `add` contains no confusable *pair* in either unit. The `ego` files make whole-closure checking cheap.
- Comparison is byte equality after NFC. Identifiers are case-sensitive, so locale case folding never reaches identifier resolution.
- **Non-ASCII identifiers are allowed. Non-ASCII keywords are not.** Private names in any script; ~40 keywords learned once. Public names additionally obey §3.

## 8.3 Diagnostics

- Stable machine-readable codes (`EXS-E0104`). Codes are permanent; text is not. Tools match codes, never English prose.
- Every diagnostic carries a source span — not retrofittable, which is why the CST is not optional.
- **All source echoed in a diagnostic is escaped.** A diagnostic rendering raw bidi makes the error message the attack surface.
- English text is canonical. Translation, if ever, is a lookup keyed on code. Rust's Fluent effort stalled because translation was entangled with 400+ formatting sites and there is now a proposal to remove it; do not repeat that.
- Capability and lexicon errors ship machine-applicable fixes; `exsc emenda` applies them. Both classes are unusually suited to auto-fix because the required edit is mechanically derivable.

---

# 9. Compilation and the build model

## 9.1 Pipeline

```
source → lossless CST → typed AST → SSA IR → backend
```

- **Lossless CST** (red-green tree, Roslyn/rowan). Error-tolerant, preserves trivia. Required for LSP and formatter.
- **Typed AST**, spans on every node.
- **SSA IR** built from the AST via Braun et al. (CC 2013) — on-the-fly, no prior analysis, minimal and pruned.

"Straight from IR" means **owning the mid-level SSA IR**, not skipping the AST. Skipping it costs diagnostics, LSP, and macros, and cannot be undone later.

## 9.2 Backend

**C backend first**, behind a clean SSA boundary. Reaches RISC-V and embedded Linux immediately, bootstraps trivially, pairs with `zig cc`.

Compile speed was measured and is **not** the constraint I previously claimed. `[UNREPRODUCED]` — the measurement harness is absent from the tree; figures carried forward from v0.2. gcc `-O0` on backend-style generated C:

| shape | kloc/s |
|---|---|
| many small functions | 40–66 |
| 2 × 20,000-statement functions (pathological) | 26 |

Projected full rebuild at 3× source-to-C expansion, single core, no parallelism: **10k Exsecutor LOC → 0.76 s**. Superlinear degradation on huge functions is real (2.5×) but not the exponential blowup feared; a backend emitting one C function per source function stays in the favourable range.

**QBE stays at Stage 5**, not Stage 3. The earlier recommendation to pull it forward is withdrawn — it was based on an assumption the measurement contradicted. The remaining argument for QBE is generated-C debug info, which is a debuggability question judged on its own in Stage 3.

Later options: QBE (~8k LOC, SSA, implements the C ABI in full, targets riscv64), Cranelift (Rust-native, mature), LLVM (release builds, once there are users).

**Do not write an optimizing backend.** Zig's team took 2020–2025 to make a self-hosted x86-64 backend the debug default.

## 9.3 Compiler purity contract

`exsc` is a pure function of (source, `ego`, lockfile, flags):

- Reads **no** environment variables except an explicit `--env KEY=VALUE` allowlist.
- Never calls `setlocale`; internal text handling obeys the same rules imposed on user code.
- No `$HOME`, no user config, no dotfile discovery.
- **No network access, ever, at any phase.**
- No implicit clock. No `__DATE__`. Timestamps come from `--epoch`.
- Paths remapped by default, not opt-in.
- Deterministic symbol emission, hash iteration, section ordering.
- Byte-identical output for identical inputs across directories, times, locales, hostnames.

`exsc proba-reproducibilitatem` builds twice under deliberately divergent ambient conditions and diffs. Ships in v1, runs in CI.

**Runtime:** calls `setlocale(LC_ALL, "C")` at startup. The FFI documentation states plainly that the locale guarantee stops at the `externus` boundary — a C library may call `setlocale` itself, which is the mechanism behind CVE-2025-49003.

## 9.4 No build scripts

There is no `build.rs` equivalent. Arbitrary build-time code destroys purity, breaks cross-compilation, and is the largest source of Nix packaging pain in the Rust and Python ecosystems.

Code generation uses **declared generators**: a manifest entry naming a generator, its inputs, and its outputs. A generator is a sandboxed Exsecutor package. **Generators may not declare `Crudum`** — otherwise this is arbitrary code with the purity claim intact only on paper.

`[OPEN]` Validate that the generator model covers bindgen, protobuf, resource embedding, and Faust before committing.

## 9.5 Cross-compilation

Nix's trichotomy as language-level concepts: `buildPlatform`, `hostPlatform`, `targetPlatform`. Conditional compilation keys on **`hostPlatform`**, never an ambiguous "current platform."

**`exsc` with no `--hospes` is an error.** No default-to-build-platform. This is not a cross-compilation mode — it falls directly out of "target is a capability, not ambient state," and it is the most Nix-aligned decision in the document. Native compilation is the case where `build == host`, spelled out.

## 9.6 The two-hash invariant

> **The interface hash governs whether dependents rebuild. Cache identity is the full content hash of the module plus its transitive input closure. These are two different hashes and must never be conflated.**

Keying a build cache on the interface hash lets an attacker change only an implementation and ship it under an unchanged key — a poisoned shared or remote cache serves it to everyone. Named as an invariant because it is subtle enough to be got wrong by implementation judgment.

---

# 10. The `ego` file

*ego* (ἐγώ, "I") — the module declaring itself. One format, per module, from leaf to root package. It is a C header and a Nix flake combined: the module's **interface** and its **dependency closure**, in one declarative, total, non-Turing-complete file, evaluable without compiling the implementation.

## 10.1 Format

```exsecutor
// ego.xsc — generated by `exsc ego --emitte`, verified in CI. Never hand-edited.

ego norma.textus {
    versio    "0.4.1"
    licentia  "LGPL-3.0-or-later"

    fontes {
        norma.nucleus  sha256-1a2b3c…
        unicode.data   sha256-9f8e7d…      // required wherever text is used
        lexicon.norma  sha256-3e5a91…      // the morpheme table
    }

    hospites [ x86_64-linux, aarch64-linux, riscv64-linux, wasm32-wasi, none-eabi ]

    potestates { }                          // observes nothing

    publica typus       textus
    publica typus       grapha
    publica interfacies legibilis
    publica structura   lector
    publica functio     lege(l: lector) -> lectus
    publica functio     plica_unicode(t: &textus) -> textus
    publica functio     plica_sermone(t: &textus) -> textus  poscit sermo

    exitus [ lib, dev, doc ]
}
```

`potestates { }` alongside `none-eabi` in `hospites` is a machine-checked claim that this module runs on bare metal.

## 10.2 Properties

- **Evaluable without building.** The whole dependency graph, capability closure, and platform compatibility compute from `ego` files alone — no compilation, no network.
- **Interface identity is separate from implementation identity.** Changing a body without changing a signature leaves the `ego` hash unchanged, so dependents do not rebuild. Combined with dictionary-passing generics (§7.1), this actually works. See §9.6 for the hash that must *not* be reused.
- **Generated, never written.** `exsc ego --emitte` derives it; CI fails if stale; the LSP shows drift inline. Same discipline as a lockfile — this avoids the OCaml `.mli` sync tax.
- **Parsed, never included.** No preprocessor, at any layer, ever.

## 10.3 The capability audit

Because capabilities are part of interfaces (§4.1 rule 3, §4.3, §4.4), the transitive closure computes from headers:

```
$ exsc ego --potestates
ffi.codec_vorbis     Crudum (⇒ ALL)
retis.cliens         rete
tempus.horarium      horologium
demod.dsp.biquad     —
demod.dcf.frame      —
─────────────────────────────────────
totalis:             Crudum (⇒ ALL), rete, horologium
```

- **`Crudum` renders as `Crudum (⇒ ALL)` and sorts first.** Raw pointers can fabricate any capability, so a module with `Crudum` has all authority; listing it as a peer of `rete` misleads the reviewer.
- **`alloc` is suppressed by default** (§4.6) — it appears in ~70% of clauses and carries almost no signal. `--omnia` shows it.

A dependency that gains `rete` in a new version is a one-line diff in a checked-in file. No other package manager offers this.

---

# 11. Standard library principles

- No function reads ambient state. Anything that would takes a capability.
- **Machine serialization is locale-free by construction** — `sermo` is simply not in scope, so a locale decimal separator cannot appear.
- Human formatting requires `sermo`, always.
- Paths are an abstract type with `hostPlatform`-dependent semantics, not strings.
- Time is a capability (`horologium`); time zones are data.
- Grapheme segmentation ships in the core, not a third-party package. This was Rust's mistake.
- **The Unicode data version is a content-addressed dependency** of every `ego` transitively using text. `plica_unicode` is stable only against a pinned table.

---

# 12. Tooling

Non-negotiable; a language without these is a toy.

- **LSP server** — needs the lossless CST. First-class component, budgeted, not an afterthought. `[OPEN]` **Deferred** under the assembly implementation; see §18.2.
- **Formatter** — canonical, no options. Normalizes to ASCII where an ASCII spelling exists.
- **Debugger** — DWARF. The weak point of the C backend.
- **Package manager** — thin, because `ego` is declarative and resolution is separate from building.

One tool: `exsc aedifica | proba | forma | lsp | ego | emenda | documenta | novum | lexicon | curre`.

`exsc novum` scaffolds a working project with a valid `ego.xsc` in one command — first running program under 60 seconds or onboarding has failed.

Scratch work without ambient authority: `exsc curre --potestates omnes scratch.xsc`. Grants typed on the command line — explicit, not ambient, one flag of friction.

**The demo:** `exsc aedifica --hospes riscv64-linux` on a Mac, no toolchain setup, static binary, byte-identical to CI. Zig's cross-compilation demo is most of why Zig got noticed; here it falls out of the design rather than being engineered. **Dropped** under the assembly implementation (§18.2): the compiler is x86-64 machine code and a Mac is aarch64. The nearest surviving demo is the same command from an `x86_64-linux` host.

---

# 13. Error registry

| code | meaning |
|---|---|
| `EXS-E0101` | source not UTF-8, or BOM present |
| `EXS-E0102` | source not NFC |
| `EXS-E0103` | bidi or invisible control character in source |
| `EXS-E0104` | mixed-script identifier (UTS #39) |
| `EXS-E0105` | confusable identifiers in import closure |
| `EXS-E0106` | CRLF line ending |
| `EXS-E0311` | integer index applied to `textus` |
| `EXS-E0321` | `:nativus` in a `@transitus` type |
| `EXS-E0322` | implicit padding in a `@transitus` type |
| `EXS-E0332` | branded offset applied to the wrong buffer |
| `EXS-E0421` | undeclared capability (atom or row) |
| `EXS-E0500` | module-level mutable state |
| `EXS-E0501` | capability stored in module-level state |
| `EXS-E0510` | capability escapes a `dyn` bound |
| `EXS-E0520` | non-atomic `refero` crossing `externus` |
| `EXS-E0601` | public name does not decompose into the morpheme table |
| `EXS-E0602` | suffix contract disagrees with declared type |
| `EXS-E0603` | prefix signature law violated |
| `EXS-E0610` | composition depth exceeded (max two affixes) |

---

# 14. Conformance suite

Ships with v1. Each entry must **fail to compile**, or in the last two cases produce identical bytes.

1. Turkish dotless-ı case fold in program logic → `plica_sermone` without `sermo`
2. Index computed on a folded copy, applied to the original → `EXS-E0332`
3. Bidi override in a comment → `EXS-E0103`
4. Cyrillic homoglyph across two modules → `EXS-E0105`
5. Non-NFC identifier → `EXS-E0102`
6. `:nativus` in a wire struct → `EXS-E0321`
7. Implicit padding in a wire struct → `EXS-E0322`
8. Integer index on `textus` → `EXS-E0311`
9. HOF calling a function parameter without a row → `EXS-E0421`
10. Laundering through a correctly polymorphic HOF → `EXS-E0421`
11. Capability in module-level mutable → `EXS-E0501`
12. Capability-bearing impl behind a bare `dyn` → `EXS-E0510`
13. Non-atomic `refero` through `externus` → `EXS-E0520`
14. `-or` name declared as `functio` → `EXS-E0602`
15. Refcount saturation → runtime abort, not wraparound
16. Same source, different directory/time/locale/hostname → byte-identical output
17. `exsc aedifica --hospes riscv64-linux` from x86_64 → byte-identical to native CI build

---

# 15. Open problems

Worst first.

1. **Closure capture in capability rows** (§4.2). The substitution foundation is built and validated against six attacks; a lambda capturing a capability from an enclosing `sub` is untested. Blocks Stage 1.
2. **Lexicon derivation test** (§16). Cannot be retired by more engineering — needs human subjects. No longer gates whether §3 is load-bearing (ADR 0005); §3 is retained regardless. What remains open is the *size of its cost*, which is unmeasured.
3. **Reference cycles** (§6.7). No answer. Accepted cost, with a DoS exposure to document.
4. **Root coinage governance** (§3.8). No Latin for hash, socket, mutex. Coinable, but coining needs a process.
5. **Generics × capability rows × dictionary layout** (§7.1). Unprototyped three-way interaction.
6. **Generator model coverage** (§9.4). "No build scripts" may not survive real FFI binding generation.
7. **Generated-C debug info** (§9.2). If stepping through Exsecutor is unusable, QBE moves earlier.
8. **Ecosystem bootstrapping.** Unaddressed by anything in this document, and the actual reason languages die.

---

# 16. Roadmap

**Stage 0 — validation.** Four of five kill criteria retired with evidence: CVE gate passed (§2), ARC measured (§6.2), compile speed measured (§9.2), capability rows prototyped (§4.2).

Remaining, before Stage 1:
- **Closure capture in the prototype.** ~1 week.
- **The derivation test.** Print the affix table and twenty roots; give twenty derivation tasks; score against recall accuracy on an equivalent English API. Days, no compiler, no engineering. **Cheapest high-value experiment in the project.**
  - **No longer a kill criterion.** v0.3 originally read: *"if derivation accuracy does not clearly beat English recall, §3 is decorative — and everything else in this spec survives unchanged with English roots in the same derivational frame."* That branch has been closed by decision — §3 is retained whatever the number says, because the lexicon is an identity commitment rather than a hypothesis (ADR 0005). The test is still worth running as **calibration**: it measures what §3 costs, which affixes and roots produce errors, and therefore which of `EXS-E0601`–`EXS-E0610` need the best diagnostics and the widest `exsc emenda` coverage. The English control is kept because it makes that cost measurable rather than anecdotal.

**Stage 1 — frontend (3–4 months).** Lexer with the full §8.1 policy. Lossless CST. Typed AST. Diagnostics with spans and stable codes.
*Kill:* diagnostics quality is not retrofittable. Bad here → stop and fix.

**Stage 2 — types and capabilities (4–6 months).** Type checker. Capability rows with substitution and inference. Capability-bearing types and `dyn` bounds. Text types and brands. `@transitus` enforcement. Lexicon checking.
*Kill:* if `sub` resolution needs a search algorithm, redesign.

**Stage 3 — C backend, runtime, Nix (3–4 months).** SSA lowering via Braun. C emission. Cross-compile to RISC-V and embedded Linux. `buildExsecutorPackage`. `proba-reproducibilitatem` green in CI.
*Kill:* undebuggable generated C → move to QBE.

**Stage 4 — tooling (4–8 months).** LSP, formatter, package manager.
*Kill:* if any is intractable because of a language decision, the decision is wrong. Change it.

**Stage 5 — users, then optimization.** QBE or Cranelift. Self-hosting. LLVM for release.

**Estimate: 14–22 months** to something usable, assuming this is not the only project competing for attention. Treat as optimistic: three earlier estimates in this project were revised after measurement, all in the direction of more surprises rather than fewer.

---

# 17. What would make this fail

- The derivation test comes back negative and §3 costs more than it returns. Since ADR 0005 retains §3 regardless, this failure mode is now **accepted rather than mitigated** — the documented fallback of English roots in the same derivational frame has been closed by choice. The risk below is the same risk, undiluted.
- Closure capture cannot be checked soundly, and the audit view — the strongest artifact here — becomes theatre.
- Naming constraints prove to be the thing developers will not tolerate. This is the largest adoption risk in the project, larger than ARC or capabilities.
- The no-build-scripts constraint blocks real FFI work.
- It never gets users, which is how almost all of them fail.

Design against these. Do not design around them.

---

# 18. Implementation

Recorded so the constraints are not rediscovered later. Nothing here changes a design decision above; it fixes how they get built.

- **Host language: x86-64 assembly**, freestanding — no libc, no dynamic linking, direct syscalls only.
- **Assembler: fasmg**, for its macro engine and because it is architecture-neutral: the macro dialect, the generated Unicode tables, and the test harness survive the eventual aarch64 and riscv64 ports even though the compiler body does not.
- **Design probes stay in Python** (`prototypes/`). They are instruments for answering design questions — never shipped, never on the build closure.

## 18.1 What this buys

- **§9.3's purity contract stops being a discipline and becomes a property of the artifact.** A freestanding static binary cannot call `setlocale`, read `$HOME`, or discover a dotfile, because the code to do so is not linked into it. "No network access, ever, at any phase" becomes checkable rather than promised: extract every `syscall` site and its `rax` value from the binary and diff against the declared allowlist. CI enforces this. No other candidate host language offered a guarantee of this shape.
- **Byte-identical output (§9.3) is close to free.** Insertion-ordered maps, no allocator nondeterminism, explicit symbol and section emission order — all of it under direct control rather than inherited from a runtime.
- **The build closure is `{fasmg}` plus a vendored macro package.** §1's Nix-native claim applied to the compiler itself rather than only to what it produces. The qualification is load-bearing, and was found by measurement rather than assumed: fasmg is architecture-neutral in the strong sense — the binary knows no machine instructions at all, and `mov eax, 60` on its own is `Error: illegal instruction`. The x86-64 instruction set and the ELF64 executable writer are *macro packages*, ordinary fasmg source, and the nixpkgs derivation ships only `bin/fasmg`. They are vendored at `vendor/fasmg-x86/`, which also retires the last network dependency in the build: upstream publishes to a rolling URL whose bytes have already drifted from the hash nixpkgs pins, so the package builds today only from cache. See `vendor/fasmg-x86/PROVENANCE.md`.
- **§9.2's C backend emits text, not machine code** — among the easier things to do in assembly. The costly parts of the pipeline are the CST (§9.1) and the Unicode layer (§8), not codegen.

## 18.2 Accepted costs

- **§12's demo is dropped.** `exsc aedifica --hospes riscv64-linux` on a Mac is impossible when the compiler is x86-64 machine code. `buildPlatform` is pinned to `x86_64-linux`. Cross-compilation to RISC-V is unaffected — that is `targetPlatform` — but the demo §12 identified as the adoption hook is gone until an aarch64 host exists, and §17 lists never getting users as the top failure mode.
- `[OPEN]` **The LSP is deferred.** §12 calls it non-negotiable and that judgement stands unchanged; incremental reparse over a red-green tree in assembly is not Stage 1 work. This is an amendment, not an omission.
- **aarch64 and riscv64 hosts are full rewrites** of the architecture-specific tree. §5.3's minimum ABI coverage is about what `exsc` can target and is unaffected; this is about where `exsc` can run.
- **Debugging degrades.** §12 already flags DWARF as the C backend's weak point; hand-written assembly does not improve it. Mitigated by stage dumps at every pipeline boundary rather than by a debugger.
- **§16's revise-on-measurement loop gets expensive**, in direct tension with this document's method (see the evidence-base note above: prose designs are hypotheses until code runs). The Python probe layer is the mitigation — design questions get answered where iteration is cheap, and only settled answers are written in assembly.
