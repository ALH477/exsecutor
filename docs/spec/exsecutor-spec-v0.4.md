# Exsecutor — Language Specification v0.4

**Status:** design complete, implementation begun — the compiler's macro dialect and runtime are under construction; no Exsecutor source compiles yet. Consolidates v0.1, Addenda A–C, Stage 0 measurements, the adversary audit, and the capability-row prototype. Supersedes all prior documents.

**v0.3 → v0.4.** Two changes, both recorded as ADRs rather than argued here. **§15 #2, §16, §17:** the lexicon derivation test is no longer a kill criterion on §3 — the Latin lexicon is retained regardless of the result, as an identity commitment rather than a hypothesis (ADR 0005). The test survives as calibration; §17's adoption risk is now accepted rather than mitigated, and the English-roots fallback §16 offered is closed by choice, not by evidence. **§18.1:** the build closure is `{fasmg}` *plus a vendored macro package* — corrected against measurement, fasmg being architecture-neutral and shipping no instruction set (ADR 0003). No other design decision changed. v0.3 retired the v0.1–v0.2 placeholder name and recorded the implementation decision in §18.

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

**Extension:** `.exsc` — **Interface file:** `ego.exsc`

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

**`-e` is a morphological category, not a spelling.** It is the present-stem
imperative, and Latin realises that differently by conjugation: `-e` in the
third (`leg-` → `lege`), **`-a` in the first** (`plica-` → `plica`,
`applica-` → `applica`, `saluta-` → `saluta`). The table names one surface
form because §3.3's worked root, `leg-`, is third-conjugation.

This was stated as a spelling, and taken literally it rejects most of this
document's own function names — `applica` (§4.2), `plica_unicode` and
`plica_sermone` (§5.1, §10.1) all end in `-a`. A checker built from the table
as written would have failed the spec before it failed any user. Corrected
here rather than left for the checker to discover.

Two names in this document remain **unresolved** against §3.4 and are flagged
rather than quietly excused. `nocens` (§4.2) is a present participle, a form
the table does not list. `exterior` (§4.2) ends in `-or`, which the table
assigns to agents declared `structura` — and §14 entry 14 is precisely
*"`-or` name declared as `functio` → `EXS-E0602`"*. It is genuine Latin (a
comparative in `-ior`), so a longest-suffix matcher and a morphological one
disagree about it. `[OPEN]` — both are illustrative names in examples, and
whether §3.4 grows a participle row or the examples get renamed is a decision
for when the lexicon checker is written.

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
- **Root coinage governance.** There is no Latin for hash, socket, or mutex. They are coinable (`dispersio`, `receptaculum`, `exclusor`) but coining requires judgment and judgment requires a process. §3.9 is that process. `[UNTESTED]` — defined, never exercised.

## 3.9 Root coinage governance

`[UNTESTED]` — the process is defined; no root has been coined through it.

A lexicon that cannot grow is a lexicon that gets abandoned the first time
someone needs to name a hash table. A lexicon that grows without discipline
stops being checkable, and §3's entire claim is that names are *mechanically*
verifiable. This section is the process between those failures.

### 3.9.1 Exhaustion — when a root may be coined

A new root is admissible only after showing the concept cannot be expressed by
the existing table within §3.8's two-affix ceiling. The proposal records the
combinations tried and why each fails. Most apparently-new concepts are
compounds of roots already present, and the exhaustion argument is where that
gets discovered.

Failing to find a word is not the same as one not existing. The burden is on
the proposer.

### 3.9.2 The ladder — how a root is coined

Descend only when the rung above genuinely fails.

1. **Attested classical Latin**, however rare. Prefer a real word nobody uses to
   an invented one everybody must learn.
2. **Late, medieval, or Neo-Latin scientific vocabulary.** Botany, medicine and
   taxonomy have been coining disciplined Latin for four centuries; that corpus
   is large and it is *already* the answer to "Romans had no word for this."
3. **Greek combining form**, where §3.3 already admits Greek.
4. **Descriptive compound** from attested roots, accepting a longer name.
5. **Marked loan** — the foreign word given a Latin declension and a morphology
   that behaves. Recorded as a loan (§3.9.5), never disguised as native.

A bare English word is never admissible. That is not a coinage, it is a
surrender, and it defeats §3 entirely.

### 3.9.3 What a proposal must contain

- the concept, and the exhaustion argument (§3.9.1);
- the candidates considered, and the rung of §3.9.2 each sits on;
- the chosen root with its **stems** — present and supine for verbal roots,
  since §3.4's suffix contracts bind to stems, not to citation forms;
- a collision check against every registered root, every reserved keyword, and
  **every module namespace** — `norma` is both the standard-library namespace
  and the Latin for a vector norm, and that class of collision must be caught
  before registration, not after;
- at least three derivations the new root is expected to support, demonstrating
  it composes under §3.4 and §3.5 rather than standing alone.

### 3.9.4 Review and registration

A coinage is reviewed by someone other than its proposer, who verifies the
exhaustion argument and the collision check rather than re-litigating taste.

Registration amends `lexicon.norma`, whose content hash therefore changes.
Because the morpheme table is a content-addressed dependency of every `ego`
(§3.8, §10.1), a coinage is visible to every dependent as a version bump —
lexicon growth is tracked by the same machinery as any other input, and cannot
happen silently.

**A registered root is permanent.** It may be *deprecated* — discouraged, still
valid, still compiling — but never reused for a different meaning. Public names
are interfaces; silently repointing a root changes what already-published code
means. This is §8.3's discipline for error codes, applied to morphemes for the
same reason.

### 3.9.5 The loan register is a measurement

Every rung-5 loan is recorded in a register carried with the table.

The register is not an embarrassment to be minimised out of existence; it is
**the running measurement of whether §3 scales.** §17 names naming as the
largest adoption risk in the project. A loan count that stays small is evidence
the derivational frame is doing real work. A loan count that climbs steadily as
the standard library grows is evidence it is not — and that is a falsifiable
signal worth having, given §16's derivation test can no longer supply one
(ADR 0005).

Report it. Do not bury it.

### 3.9.6 Domain sub-lexicons

`lexicon.norma` holds the core. A specialised domain may carry its own table —
`lexicon.algebra`, `lexicon.rete` — separately hashed and separately depended
upon, so that a program which never touches linear algebra does not inherit its
vocabulary. Sub-lexicons obey this section in full; they are not a relaxation.

### 3.9.7 Tooling

`exsc lexicon` (§12) checks a proposal mechanically: it runs the collision
check, verifies the stems compose under §3.4 and §3.5, confirms the claimed
derivations decompose, and reports the loan-register delta. Judgment stays with
the reviewer; the arithmetic does not.

---

---

# 4. Capabilities

Prototype: `prototypes/capcheck/exsecutor_check.py`, 468 lines, validated against six attacks and two legitimate programs. `[UNREPRODUCED]` — that artifact is absent from the tree. The checker is being rebuilt from this section, which is a re-derivation, not a restoration.

## 4.1 Rules

1. Capability types are **unforgeable**. No literal, no cast, no default.
2. The only root is the `Mundus` passed to `initium`. All others derive from it, explicitly and fallibly.
3. Capability sets are part of **`functio` types**, not only declarations.
4. **A capability becomes available in a scope in exactly four ways — bound by `sub`, received as a parameter, held in a field of the receiver, or **captured by a closure from an enclosing scope** — and all four are visible in the interface.** `poscit` means *drawn from the enclosing scope*, nothing else. Capture was absent from this list in v0.4 and earlier, which is precisely where the closure-capture hole lived: it is visible because a captured row appears in the closure's **type** (§4.2), not because it is bound or passed.
5. `publica` functions declare `poscit` explicitly. Private functions infer it from their bodies, transitively.
6. A function with no `poscit` and no capability parameters is **pure with respect to ambient state**. It may allocate and diverge; it may not observe the host.
7. **No module-level mutable state.** (`EXS-E0500`; with a capability, `EXS-E0501`.)

## 4.2 Rows and substitution

A higher-order function requires whatever its function argument requires:

```exsecutor
publica functio applica(v: f32, f: functio(f32) -> f32) -> f32 poscit alloc, sicut f {
    redde f(construe(v));
}
```

> **Substitution rule.** A capability row is part of a function **type**, not only of a function *declaration*: `functio(A) -> B poscit {R}`. A row variable in a callee's `poscit` names one of the **callee's** parameters; at each call site it is substituted **positionally** with the row carried by the actual argument's **type**.

*Rows travel with values.* The original rule said "the capability row of the actual argument" and resolved it by **name** at the call site — a named function's declared `poscit`, or an inline lambda's body. A closure that has escaped its constructor arrives as a bare value of function type with neither, so there was nothing for a row to attach to, and a genuine violation was accepted. Putting the row in the type removes the lookup: an escaped closure carries its captured authority in its own type, where a caller and the audit can both see it.

`sicut f` is unchanged as surface syntax. Its meaning is now *the row in parameter `f`'s type*, resolved by typing rather than by inspecting the call site.

Without this, unioning the callee's row propagates a name with no referent in the caller (the prototype produced `requires [sicut f]` inside a function having no `f`). Substitution is what makes laundering impossible rather than merely annotated:

```exsecutor
publica functio nocens(x: f32) -> f32 poscit rete { … }

publica functio exterior(v: f32) -> f32 poscit alloc {
    redde applica(v, nocens)      // EXS-E0421: requires [rete], undeclared
}
```

**Closure capture: found by measurement, fixed, and re-measured.** The probe (`prototypes/capcheck/`) first *accepted* `cases/bad_closure_capture.exsc`, a genuine violation in which a lambda closes over a `sub`-bound `rete`, escapes as a function-typed value, and is forwarded by a caller declaring only `alloc`. Every individual function in that file is correct; the rule could not see the composition.

Under the revised rule the same file is rejected as `EXS-E0421`, and `cases/ok_closure_declared.exsc` — the same shape with the row correctly declared — is still accepted. That second case is load-bearing: a checker that rejects the violation *and* the correct version is not a fix, and an earlier attempt at this change did exactly that by double-counting a `sicut`-declared row.

`[UNTESTED]` as a soundness claim. What exists is nine cases passing in a Python probe, not a proof and not a compiler. §15 #5's generics-by-dictionary-passing interaction is made harder by this change, not easier.

## 4.3 Capability-bearing types

A type with a capability-typed field, transitively, is **capability-bearing**, must be declared so, and the mark propagates into the `ego`'s `potestates`.

```exsecutor
structura ScriptorRetis { sock: rete }      // capability-bearing: [rete]
```

## 4.4 Dynamic dispatch

**A trait's declared rows are a ceiling on every implementation of it**, checked at each `interfacies X in T poscit R` declaration. An implementation whose mark exceeds what the trait declares is `EXS-E0510`, whether or not it is ever boxed.

`dyn Trait poscit {P}` narrows that ceiling further at the cast site; constructing a trait object from an implementation whose mark exceeds `P` is the same code.

**This was found by measurement, not design.** Until `prototypes/gendict/`, the exceeds-check was stated *only* for `dyn` construction — so an implementation reached solely through a generic `<T: Trait>` was never checked against anything. `bad_static_impl_exceeds_member_ceiling.exsc` is the demonstration: `Summable.combine` declares `poscit alloc`, `Right` declares `alloc, rete`, `total<T: Summable>` trusts the trait and declares `alloc`, and a call reaches `rete` undeclared. `Right` never becomes a `dyn` object, so §4.4 as written never fired. The probe also showed the hole is transitive — a generic could launder a value past a `dyn` bound that would have caught it directly (`bad_dyn_launder_via_generic.exsc`), and closing the ceiling closes that too.

The row is **braced** in type position. Written bare it is ambiguous inside a comma-separated parameter list, where the next `,` could belong to either the row or the list — §8.6 resolves this by taking a row on the two-token peek `poscit {`. This section previously wrote `poscit P` schematically, which is not a form that parses.

Trait capability needs otherwise dissolve into the receiver — a file writer's authority *is* the handle in its field — so `poscit` on trait methods should be rare. If you need one, the capability is probably misplaced. But that convenience is exactly what would hide capabilities from the audit, hence the bound.

## 4.5 Ergonomics

```exsecutor
potestas Hospes = { alloc, archivum, horologium, ambitus }
```

`sub` binds for a scope; a statement form (`sub alloc = a;` to end-of-scope) exists alongside the block form so programs are not permanently indented.

`sub` resolution is **lexical and non-searching**: one value per capability type per scope, shadowing is an error, no implicit conversion, no inference across module boundaries. If it grows a search algorithm, it has failed.

## 4.6 The capability set

`Mundus` (root), `alloc`, `sermo` (human language), `horologium` (clock), `archivum` (filesystem), `rete` (network), `fortuna` (randomness), `ambitus` (process environment), `Filum` (threads), `machina` (a separate compute device — §5.5), `Crudum` (raw pointers, unchecked casts, manual lifetimes).

`alloc` appears in roughly 70% of non-kernel `poscit` clauses and therefore carries little signal; the audit view suppresses it by default (§10.3).

**Standard input, output and error belong to `ambitus`.** They are handed to a process by its environment, not found on a filesystem: a program that writes to its terminal has touched nothing under `archivum`, and borrowing that atom for it would over-grant in exactly the way §10.3's audit exists to expose. `examples/README.md` recorded this as `[OPEN]` when the companion program was written; it is closed by placing the streams, not by spending a root on a twelfth atom.

## 4.7 The entry point

```exsecutor
publica functio initium(m: Mundus) -> u8 { … }
```

Rule 2 names `initium` and says `Mundus` is passed to it; this pins the rest. A program has exactly one `initium`; it is `publica`; its one parameter is the root, which is rule 4's second path — received as a parameter — so it carries no `poscit` and there is nothing to declare: the program's whole authority is that one value, and every other capability is derived from it, explicitly (rule 2). Its result is the process exit status. A module with no `initium` is a library, and `aedifica` on one yields a library artifact; "it runs" is a claim only a program can make.

Derivation is by method on the root — `m.ambitus()`, `m.archivum()` — and rule 2's *fallibly* is resolved at two different times. Where presence is a property of the host (`ambitus`, `archivum`: a `none-eabi` target has neither), it is decided at compile time by `--hospes` (§9.5) and the `hospites` list (§10.1), and the derivation itself is total. Where presence is a property of the run (`rete`), the derivation returns `eventus`. `[OPEN]` which atoms fall on which side beyond these three; the checker (`docs/design/checker.md`) decides per atom. `[OPEN]` a second `initium`, or one with the wrong signature, has no §13 code; it belongs to the Stage 2 amendment.

---

# 5. Types

## 5.1 Text and branded offsets

```exsecutor
firma t: textus = "café";
t.octeti().numerus();      // 5   bytes
t.scalares().numerus();    // 4   scalar values
t.grapha().numerus();      // 4   grapheme clusters
t[0];                      // EXS-E0311: textus has no integer index
```

UTF-8 storage. Slicing by byte offset, O(1), returns `eventus`, never panics. `octeti`, `scalares`, `grapha` are distinct types, not coercing views. Grapheme segmentation ships in the standard library.

**Branded offsets.** `quaere` returns `positio<'t>`, generatively branded to the buffer it indexed. `sectio` accepts only its own brand:

```exsecutor
firma abassus = via.plica_unicode();
firma ubi     = abassus.quaere("/")?;     // positio<abassus>
firma pars    = via.sectio(0..ubi)?;      // EXS-E0332: branded to `abassus`
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

### Bit-width fields

Real wire formats pack sub-byte fields, and the vocabulary above could not
express one. A field in a `@transitus` type may declare any width `uN` for
1 ≤ N ≤ 64, under four rules:

1. Sub-byte fields **pack MSB-first in declaration order**. The
   first-declared field occupies the high bits, which is what wire formats
   universally do and what a field table reads like.
2. A field wider than 8 bits **must begin on a byte boundary and be a whole
   number of bytes**. Byte order is an order over bytes; a 12-bit field has
   no defined one, and nothing here should have to invent a convention for
   the trailing nibble. So the widths are `u1`–`u7` (sub-byte, rule 1) and
   `u8 u16 u24 u32 u40 u48 u56 u64`. `u12` is not a type and does not parse.
   Widths in between were admitted by the first draft of this rule, which
   said only "must begin on a byte boundary" and left the *end* undefined.
3. **Byte order is required above 8 bits and is not part of the grammar at or
   below it.** An unannotated multi-byte field in a `@transitus` type *is*
   `:nativus` and is therefore already `EXS-E0321`; `u4:maior` does not parse,
   so it is `EXS-E0201`. Neither needs a new code.
4. Declared widths **must sum to a whole number of bytes**. A partial trailing
   byte is implicit padding — `EXS-E0322`, the rule above, applied at bit
   granularity. This is the load-bearing one: an extension that enforced "no
   implicit padding" only between bytes would quietly reopen the disclosure
   class the rule exists to close.

The worked example is not invented. It is the DCF DeModFrame
(`vendor/hydramesh-wire/`), a 17-byte quantum with eleven independent
implementations and a finite certificate:

```exsecutor
@transitus
publica structura DeModFrame {
    signum:  u8               // 0xD3, the first validity gate
    versio:  u4               // packs MSB-first with genus
    genus:   u4               // frame type
    numerus: u16:maior
    fons:    u16:maior
    meta:    u16:maior
    onus:    u32:maior
    tempus:  u24:maior        // 24-bit microsecond offset
    cursus:  u16:maior        // CRC-16/CCITT-FALSE over bytes 0..14
}
```

**Why this example and not another.** §5.2's rules were previously illustrated
only by `Capitulum` above, which was written for this document — every field a
whole number of bytes, every width one the language already had. A format
nobody had to satisfy is weak evidence that an annotation is sufficient.
DeModFrame was written by people not thinking about Exsecutor, and it did not
fit: `versio`/`genus` share a byte and `tempus` is 24 bits wide. Both gaps were
found by running `prototypes/wire/`, which first checks its own reading of the
format against all 246 certificate vectors, then attempts the declaration. The
rules above are what closed it, and the same probe checks that they do.

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

## 5.4 Numeric semantics

`[OPEN]` — designed, nothing implemented. Stage 2 for integers, Stage 3 for vectors.

The floating-point environment is ambient state, and this document exists to
retire ambient state. `-ffast-math` is `setlocale` for numbers: a global,
invisible switch that changes the semantics of code that never asked for it,
appears in no interface, and lets two libraries with byte-identical headers
compute different answers. MXCSR/FPCR rounding mode is worse — thread-local
mutable state that silently alters every result downstream of a call.

§5.2 puts byte order in the type. This section closes the same gap for
arithmetic.

### Floating point

Semantics are declared per module in the `ego`'s `numeri` block (§10.1), not
set by a compiler flag:

| declaration | meaning | default |
|---|---|---|
| `rotundatio` | rounding mode | `ad_parem` (nearest, ties to even) |
| `reassociatio` | may the compiler reassociate? | `vetita` — forbidden |
| `contractio` | FMA formation | `explicita` — only where written |
| `subnormales` | subnormal handling | `conservata` — no FTZ/DAZ |

`reassociatio vetita` is the default because reassociation is the part of
`-ffast-math` that silently changes results. A module wanting it must say so in
its interface, where a caller can see it.

**The fasmg backend is the reference for everything in this section** (§9.2).
`reassociatio`, `contractio` and `subnormales` are not expressible in portable
C, so a C target that cannot honour a declared `numeri` fails the build rather
than quietly producing different numbers. Where the two backends disagree, the
fasmg result is the answer.

**`numeri` is interface-affecting.** Changing it changes callers' results, so it
feeds the interface hash and dependents rebuild (§9.6). Calling between modules
with incompatible `numeri` requires an explicit coercion that names the
assumption — the same rule §5.2 applies to `mensura`.

Directed rounding (`ad_superius`, `ad_inferius`) is declarable because validated
numerics — interval arithmetic with provable error bounds — is impossible
without it.

### Reduction shape is semantics, not optimization

Floating-point addition is not associative, so a parallel reduction is a
*reassociation*. Summing across eight lanes gives a different answer than
summing left to right. Most languages let the optimizer choose, which is why a
dot product changes value when the core count changes.

**The reduction tree shape is part of the operation.**

```exsecutor
summa_ordinata(v)         // strict left-to-right, exactly reproducible
summa_arborea(v, 8)       // fixed-width pairwise tree
```

A declared tree shape is deterministic *and* parallel. The machine decides how
long the reduction takes; it never decides what the reduction computes. This is
what makes byte-identical numerical output achievable across core counts and
target ISAs.

**The remainder is part of the shape.** `arborea w` over `n` elements where `w`
does not divide `n` is defined as `ceil(n / w)` groups in index order, the last
holding the remainder, combined by the same rule. Leaving this to the
implementation would put the machine back in charge of what is computed, which
is the one thing this section exists to prevent. `summa_arborea(v, 8)` on a
20-element vector is groups `[0..8) [8..16) [16..20)`, in that order, always.

**A running accumulator is not readable inside the reduction body**
(`contrahe`, §8.5). If the body could observe the partial total, the summation
order would be observable, and no tree shape but strictly-left-to-right would
be a valid implementation — the declaration would become a lie the moment
anything used it. The accumulator is write-only until the reduction completes.

**`rumpe` is forbidden inside an iteration carrying a `contrahe`.** An early
exit makes the result depend on which iterations ran, and under `quisque` that
is not even well defined. Use `per` with an explicit accumulator if an
early-exit fold is what is wanted; it is then ordinary sequential code and
claims nothing about shape.

### Vectors

`acies<f32, 8>` carries its lane count **in the type**. Lane count is never
inferred from the host: a target-inferred width makes results machine-dependent,
which is the failure this document is written against. The compiler lowers to
AVX-512, AVX2, NEON, or a scalar loop as the target allows; the lowering changes
performance and never changes the value.

### Integers

- **Arbitrary widths.** `u7`, `i23`, `u1`. Width is exact, not a minimum.
- **No implicit promotion.** C's integer promotion is a defect factory. Widening
  is explicit and names the target assumption, as `mensura` already requires
  (§5.2).
- **Overflow behaviour is in the operator, not a compiler flag.** `+` traps,
  `+%` wraps, `+|` saturates, `+?` yields an optional. A flag that changes
  whether arithmetic traps is ambient state by another name.
- `@transitus` extends to explicit **bit** offsets. No implicit bit padding, for
  the same reason §5.2 forbids implicit byte padding.

---

## 5.5 Device placement and heterogeneous execution

`[OPEN]` — designed, nothing implemented. Stage 3 at the earliest.

A GPU is not "more cores." It is a separate device with a separate address
space, a separate ISA, its own floating-point behaviour, and no business being
reached by accident. Every one of those is ambient state if the language stays
quiet about it, so §5.5 says three things: where data lives is in the type,
reaching a device is a capability, and a device that cannot honour the declared
numerics is a compile error rather than a different answer.

### Placement is in the type

```exsecutor
acies<f32, 1024>              // host memory
acies<f32, 1024> apud machina // device memory
```

`apud` ("at, in the presence of") marks residence. Transfer is explicit and
visible; there is no unified-memory abstraction that silently turns a pointer
dereference into a PCIe round trip. This is §5.2's rule — representation
assumptions belong in the type — applied to locality.

**ARC does not cross the device boundary.** Refcounting across an interconnect
is the wrong mechanism. Device buffers are region-allocated, owned by the host,
and released as a block, which is exactly §6.3's arena model and exactly what
device memory wants anyway.

### Reaching a device is a capability

`machina` (§4.6) gates it. `poscit machina` in a signature means this call
dispatches to an accelerator, and §10.3's audit answers "does anything in my
dependency closure quietly use the GPU?" across the whole closure. Nothing
reaches a device without saying so in its interface.

### `@nucleus` — device-executable functions

`@nucleus` restricts a `functio` for device execution as `@transitus` (§5.2)
restricts a `structura` for wire safety. Same pattern, same reason: a context
with fewer guarantees needs a type that admits fewer programs.

A `@nucleus` function may not allocate, may not recurse unboundedly, and may
observe nothing of the host. §4.1 rule 6 already gives most of this — a function
with no `poscit` is pure with respect to ambient state — but rule 6 permits
allocation and divergence, and a kernel permits neither. `@nucleus` is that
tightening, and the capability system carries the rest without a new annotation.

### The rule that makes CPU and GPU agree

> **If a target cannot honour the module's declared `numeri` (§5.4), compilation
> fails. It never silently degrades.**

GPUs routinely force denormal flushing and contract aggressively into FMA. Under
this rule a module declaring `subnormales conservata` will not build for a device
that cannot deliver it. That is the intended behaviour: the alternative — which
every existing toolchain chooses — is the same source producing different
numbers on the accelerator, discovered later by whoever trusted them.

Combined with §5.4's declared reduction shape, this yields the property the
design is actually for:

> The same source, with the same declared decomposition, produces **the same
> bits** on CPU and on GPU.

The tile nest maps onto device hierarchy without reinterpretation — grid, block,
warp, lane are tiles, exactly as cache block and SIMD group are tiles. Because
the nest is declared rather than inferred, the summation order is identical on
both, and the accelerator changes only how long the work takes.

`[UNTESTED]`. Nothing about this has been measured, and the claim is strong
enough that it must be treated as a hypothesis until `proba-reproducibilitatem`
compares a CPU result against a device result byte for byte.

### Targets

`hospites` (§10.1) lists hosts. Devices are declared separately, because a
kernel target is not a host target — the host runs the driver:

```exsecutor
acceleratores [ spirv-vulkan, nvptx64-cuda, amdgcn-rocm ]
```

An empty `acceleratores` alongside `poscit machina` anywhere in the module is a
contradiction the `ego` checker rejects (§10.2), the same way `potestates { }`
with `none-eabi` is a machine-checked claim today.

---

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

`[OPEN]` The three-way interaction of generics × capability rows × dictionary layout **is now prototyped** (`prototypes/gendict/`, ten cases) and partly answered.

Witness-table layout is **stable** across instantiations — same slots, same arities, which §7.2's "modes may never change representation" requires anyway. What varies is the *row* attached to a slot's type, and the fix is §4.4's ceiling: because a trait's declared rows now bind every implementation, a generic can trust the trait's row rather than any particular impl's. Layout does not depend on the row after all; the row is bounded by the interface.

What remains `[OPEN]`: no syntax exists for a row on a **type parameter**. `poscit sicut T` parses — §8.6's `RowItem` does not distinguish a value parameter from a type parameter — and is semantically inert. The probe notes it reproduces the shape of the pre-§4.2 closure-capture defect one level up, except it fails **closed** rather than open, which is the tolerable direction.

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

# 8. Surface syntax and diagnostics

## 8.1 Source

- **UTF-8 only.** No BOM — a BOM is `EXS-E0101`, not a skipped byte.
- **LF only.** CRLF is `EXS-E0106`. The formatter converts; the compiler does not.
- **NFC required.** Non-NFC is `EXS-E0102`.
- Bidi and invisible controls (U+202A–202E, U+2066–2069, U+200B–200F, U+061C, U+FEFF) anywhere in source, **including strings and comments**, are `EXS-E0103`. Escapes are the only way to produce them.
- **U+FEFF is `EXS-E0101` at offset 0 and `EXS-E0103` anywhere else.** At the
  start it is a byte-order mark and the first bullet governs. Elsewhere it is a
  zero-width no-break space — invisible, and not covered by U+200B–200F, which
  stops at U+200F. It was missing from this list, which meant a file could
  carry an invisible character past the gate as long as it was not the first
  thing in the file. Found by the lexer implementing the rule.
- `[OPEN]` This class is enumerated, not derived. Unicode's
  `Default_Ignorable_Code_Point` property would cover it by construction —
  U+2060, U+180E and the variation selectors are all invisible and none is
  listed here. Deriving the set needs a generated table; enumerating it needs
  someone to keep remembering. Neither has happened.

## 8.2 Identifiers

- UAX #31 `XID_Start`/`XID_Continue` plus `_`.
- UTS #39 **Moderately Restrictive**. Mixed-script is `EXS-E0104`.
- **Confusable detection is scoped to the import closure**, not the compilation unit (`EXS-E0105`). Module A exporting Cyrillic `аdd` and module B calling `add` contains no confusable *pair* in either unit. The `ego` files make whole-closure checking cheap.
- Comparison is byte equality after NFC. Identifiers are case-sensitive, so locale case folding never reaches identifier resolution.
- **Non-ASCII identifiers are allowed. Non-ASCII keywords are not.** Private names in any script; the reserved words are enumerated in §8.4 and there are thirty of them, learned once. Public names additionally obey §3.

## 8.3 Diagnostics

- Stable machine-readable codes (`EXS-E0104`). Codes are permanent; text is not. Tools match codes, never English prose.
- Every diagnostic carries a source span — not retrofittable, which is why the CST is not optional.
- **All source echoed in a diagnostic is escaped.** A diagnostic rendering raw bidi makes the error message the attack surface.
- English text is canonical. Translation, if ever, is a lookup keyed on code. Rust's Fluent effort stalled because translation was entangled with 400+ formatting sites and there is now a proposal to remove it; do not repeat that.
- Capability and lexicon errors ship machine-applicable fixes; `exsc emenda` applies them. Both classes are unusually suited to auto-fix because the required edit is mechanically derivable.
- **A diagnostic may carry one note and one related span.** A note is a permanent small-integer `note_id` with at most one argument, and the argument is a compiler-owned token — a §8.4 reserved word or comparison word — never source text and never interpolated: the English for a note is a table lookup on the id, and the argument is appended after it. So the first bullet still holds with the note in place: tools match `code` (and may match `note_id`, which is equally permanent and equally not English); translation stays a lookup, now keyed on `(code, note_id)`. A related span is a second span with no text of its own — the opener that an `EXS-E0202` is unterminated from, for instance — and renders as a second gutter block in text and as `related` in JSON, or `null` when absent. Both are optional on every record; a diagnostic with neither still renders its code, span and fix. And a **`fix` is distinct from a `suggestion`**: a `fix` is machine-applicable and belongs only to the capability and lexicon classes the previous bullet names, where the edit is mechanically derivable; any other edit a diagnostic offers — a `;` a parser thinks is missing — is a `suggestion`, shown to a person and never applied by `exsc emenda`. The distinction is a property of the code, not of the record, so a tool can decide it without seeing the batch. (The review's D3 found that applying a parser's guesses as a batch made two files of eight worse.)

  > Added after the Stage 1 review (`docs/design/diagnostics-review.md`), which measured §16's kill criterion and found 37 of 46 diagnostics were the identical sentence, because the record had no field that could hold anything else. This is the smallest field that can, chosen so that the first bullet stays true rather than softened: a note is an id, not a format string.

## 8.4 Tokens

The keyword set is **closed** and listed here in full. This section is the
normative source: `compiler/x86_64/lexer/keywords.inc` is generated from it and
checked against it mechanically, the same arrangement §13 has with
`diag/codes.inc`. A keyword that appears in code but not in this table, or the
reverse, fails the build.

### Keywords are Latin, and §3 is not the reason

§3.1 scopes the lexicon rule to **public names**. §3.2 is explicit that keywords
are *"the layer that doesn't matter, which is why it was decorative and why
English keywords could be swapped in with no loss."* That judgement stands and
is not quietly reversed here.

Keywords are Latin because ADR 0005 made the lexicon an identity commitment, not
because §3 requires it. The distinction is load-bearing: §3's claim is
falsifiable and Guo-backed, and blurring it into an aesthetic preference is
precisely what made v0.1 decorative. **A reader who concludes from this table
that §3 governs keywords has read it wrong.**

One consequence: `dyn` (§4.4, §7.1) is an English abbreviation sitting among
Latin words. It stays — it is spec-established, it is a term of art, and
churning a settled term for consistency costs more than the documented
exception.

### Reserving a word spends root-space, permanently

§3.9.3 requires every proposed root to be collision-checked against **every
reserved keyword**. So each entry below is a word the morpheme table can never
use, for as long as the language exists. That is the reason the reserved set is
kept small and most of the vocabulary is *contextual* instead.

### Three tiers

**1. Reserved words** — never usable as an identifier, anywhere.

| group | words |
|---|---|
| declaration | `publica` `functio` `structura` `typus` `interfacies` `potestas` `externus` |
| binding | `firma` `mutabilis` |
| capability | `poscit` `sub` `sicut` `dyn` |
| value flow | `redde` `apud` `refero` |
| condition | `si` `sin` `aliter` |
| iteration | `dum` `per` `quisque` `in` `terminus` |
| reduction | `contrahe` `forma` |
| selection | `discerne` `casus` |
| jump | `rumpe` `perge` |

Thirty words. §8.2 says "~40 keywords learned once"; the real number is under
that, and it is now a count rather than an estimate.

**2. Contextual keywords** — meaningful only in a specific position, and
ordinary identifiers everywhere else.

- `ego`-file structure (§10.1): `ego` `versio` `licentia` `fontes` `hospites`
  `acceleratores` `potestates` `numeri` `exitus`
- `numeri` keys and values (§5.4): `rotundatio` `reassociatio` `contractio`
  `subnormales`; `ad_parem` `vetita` `explicita` `conservata`
- reduction shapes (§5.4): `ordinata` `arborea`
- comparison and connectives (§8.5, §8.6): `lt` `le` `gt` `ge` `eq` `ne`
  `et` `vel` — contextual because operator position is never operand
  position (§8.6), which is what lets `<` mean generic arguments and
  nothing else
- byte order (§5.2): `maior` `minor` `nativus`; FFI (§5.3): `abi`

These are contextual on purpose. `versio`, `numeri` and `forma` are good Latin
words and good variable names; reserving them globally would be hostile in a
language whose identifiers are Latin, and would spend root-space for nothing.

**3. Capability atoms** (§4.6) — `Mundus` `alloc` `sermo` `horologium`
`archivum` `rete` `fortuna` `ambitus` `Filum` `machina` `Crudum`. Identifiers in
the capability namespace, not reserved words.

### Operators and sigils

All already in evidence in this document, recorded here rather than introduced.

| token | meaning | first shown |
|---|---|---|
| `&T` | reference | §10.1 |
| `*T` | raw pointer — requires `Crudum` | §5.3 |
| `T:maior` | explicit byte order | §5.2 |
| `A..B` | range | §5.1 |
| `E?` | error propagation | §5.1 |
| `<…>` | generic arguments | §5.5 |
| `@nomen` | annotation — `@transitus`, `@nucleus` | §5.2, §5.5 |
| `->` | result type | §4.2 |
| `+` `+%` `+\|` | trapping, wrapping, saturating arithmetic | §5.4 |
| `( )` | parameter lists, argument lists, grouping | §4.2 |
| `[ ]` | indexing | §5.1 |
| `{ }` | blocks, capability rows, `ego` sections | §4.2, §10.1 |
| `,` | separator | §4.2 |
| `;` | statement terminator | §4.5 |
| `.` | field and method access | §5.1 |
| `=` | binding and assignment | §4.5 |
| `-` `-%` `-\|` | negation and subtraction, three overflow modes | §5.4 |
| `:` | type annotation | §4.2 |

The second half of that table was **missing** from the first draft, which
claimed everything was "already in evidence" and then omitted the punctuation
this document's own examples lean on hardest — `functio applica(v: f32, ...)`
uses four of them in one line. Found by the lexer, which had to tokenize `(`
and discovered §8.4 did not admit it. A closed token set that omits its own
examples' tokens is not closed; it is merely short.

The comparison words (`lt le gt ge eq ne`) and the connectives (`et vel`) are
**contextual** — §8.6 settles their precedence and shows why they can stay
contextual: operator position is never operand position. `/`, remainder,
shifts, negation and the `*%`/`*|` families remain `[OPEN]`, as does the
numeric literal grammar.

### Literals, comments, layout

- Comments are `//` to end of line. §8.1's bidi and invisible-control rule
  applies **inside them** — that is the Trojan Source class, and a lexer that
  scans only identifiers is defeated by an override sitting in a comment.
- String literals are `"…"`. §8.1's rule applies inside these too. Escapes are
  the only legal way to produce a bidi or invisible control codepoint.
- **A string literal may contain raw newlines.** They are part of the value,
  LF only (§8.1 rejects CRLF in source, so a literal cannot smuggle one in).
  There is no separate multiline form and no indentation stripping — stripping
  would mean the value depends on the source's leading whitespace, which is
  the ambient-state class §1 exists to remove.
- A malformed literal is `EXS-E0210`; an unterminated one is `EXS-E0202`.
- Layout is not significant. Blocks are `{ }`.

`[OPEN]` Numeric literal grammar — bases, separators, and float syntax — is not
settled and is deliberately not invented here.

## 8.5 Control flow

`[OPEN]` Design only. Nothing here is implemented, and the CST that would parse
it does not exist. Do not cite it as a property of the language.

### The claim

Three commitments already in this document are all properties of a **loop**:

- §5.4 — reduction *shape* is semantics, never an optimization.
- §5.5 — the declared tile nest maps onto CPU cache/SIMD and GPU grid/block/lane
  without reinterpretation.
- `certus` rule 6 (`docs/design/profile-certus.md`) — every loop carries a bound.

They were arrived at separately, for numerics, for devices, and for
certification. They converge:

> A loop that declares its shape is **simultaneously** deterministic,
> parallelizable, bounded, and certifiable.

Each declaration below is a promise the compiler can cash. None is decoration.

### Condition

```exsecutor
si x lt 0        { … }
sin x eq 0       { … }
aliter           { … }
```

`sin` is attested classical Latin for *"but if"* — Cicero's `si … sin …`
correlative. Every language in this lineage compounds the idea (`elif`,
`else if`, `elsif`); Latin supplies it as one morpheme, so Exsecutor takes it
for free. `aliter` ("otherwise") serves both this construct and `discerne`
below — one word, one meaning, two places.

### Selection

```exsecutor
discerne modus {
    casus arborea  { … }
    casus ordinata { … }
    aliter         { … }
}
```

The scrutinee is an ordinary expression. This example first read `discerne
forma` — using `forma`, which §8.4 reserves two lines above, as an identifier.
That is `EXS-E0220`, the code added in the same amendment, and the example
would have been rejected by the rule it was written to illustrate. Recorded
rather than silently corrected: it is the first evidence that §8.4's reserved
set is checkable against real text, and the first thing it caught was this
document.

**Exhaustive, with no fallthrough.** A missing arm is a diagnostic that *lists
the missing cases* and ships the edit — mechanically derivable, which is exactly
the class §8.3 says is suited to `exsc emenda`.

### Iteration

```exsecutor
dum COND terminus N { … }        // sequential, explicitly bounded
per i in 0..n { … }              // ordered: iteration k may observe k-1
quisque i in 0..n { … }          // independent: order unspecified
```

`per` and `quisque` differ in **one declared claim**: whether iterations observe
one another. That single bit is what everything downstream consumes.

```exsecutor
quisque i in 0..n
    contrahe summa: +
    forma arborea
{ … }
```

`contrahe` ("draw together") names the accumulator and its operator; `forma`
names the reduction shape. `ordinata` and `arborea` are not coined here — §5.4
already established them in `summa_ordinata` and `summa_arborea`.

### What each declaration buys

| declaration | what it buys |
|---|---|
| `terminus N` | termination, and `certus` rule 6 becomes **syntax** |
| `quisque` over `per` | licence to vectorize, thread, or dispatch |
| `contrahe … forma` | §5.4's declared shape → CPU/GPU **bit-identity** (§5.5) |

**`certus` rule 6 stops being an analysis.** The profile currently requires
"every loop carries a bound" and `profile-certus.md` section 5 lists loop-bound
syntax as work that does not exist. Under this design a `dum` without `terminus`
is a parse-level fact. A safety-critical rule that reduces to a missing keyword
is enormously cheaper than one requiring a call-graph pass.

### Three properties that make this worth the keywords

**1. Declaring independence is not a capability.** `quisque` asserts a property
of the *body* — iterations do not observe one another — not authority to spawn
anything. Whether the compiler vectorizes, threads, or does nothing is a
lowering decision, exactly as §5.4 already says of reduction shape. Dispatching
to a device still requires `machina` (§5.5). This falls out of existing rules
rather than needing new ones, and it means `certus` rule 12's ban on `Filum` in
the control path does not accidentally forbid parallel loops.

**2. A misdeclared `quisque` should be a compile error, not a race.** If the
body carries a cross-iteration dependency, the declaration is false and the
compiler says so. `[OPEN]` — this needs dependency analysis, it is the largest
unproven claim in this section, and it may not be affordable in a hand-written
assembly frontend. If it is not, `quisque` degrades to a *trusted assertion* —
still useful, considerably less attractive, and it must be labelled as such
rather than quietly downgraded.

**3. The compiler says what is being left on the table.** A `per` whose body has
no cross-iteration dependency earns a diagnostic naming what `quisque` would
buy, with the edit attached. §8.3 already promises machine-applicable fixes;
aiming them at the construct developers touch most is the most direct answer
available to §16's *"diagnostics quality is not retrofittable."*

### Mood, as a convention for new keywords

`poscit` is indicative — *"it requires"* — and describes. `redde` is imperative —
*"give back!"* — and acts. New keywords follow `redde`: `rumpe` (break),
`perge` (continue), `contrahe`, `discerne` are all imperatives, because they
tell the machine to do something.

Stated as an **adopted convention, not a law discovered across the existing
set**: `publica` and `firma` are ambiguous between imperative and adjective, and
claiming a clean pattern over them would be reading one in.

### What this section does not do

- It does **not** exercise §3.9. §3.9.3 requires a new root to be collision-
  checked *against* every reserved keyword, so keywords are a separate namespace
  from morpheme-table roots. §3.8 and §15 #4 stay `[UNTESTED]`; `norma.algebra`
  is still the coinage process's first real test.
- It spends root-space. Fourteen words here can never be roots (§8.4).
- Numeric literal grammar and `sub`'s interaction with
  loop scopes are unsettled and deliberately not invented.

## 8.6 Phrase grammar

`[UNTESTED]` Normative, and unexercised: no parser exists, and nothing below
has parsed a single file. What this section fixes is the grammar the CST
(§9.1) is built against; what it does not fix is listed at the end, marked.
The design record is `docs/design/phrase-grammar.md`; where the two disagree
this section wins.

### The constraint

The parser is hand-written recursive descent in freestanding x86-64 assembly,
producing the lossless, error-tolerant red-green CST of §9.1. Therefore the
grammar is **LL(1) with a fixed, enumerated set of two-token peeks** — no
backtracking, no unbounded lookahead. Every item and statement is decided by
its leading token; the parser always knows whether it is reading a type or an
expression; and §16's kill criterion is diagnostics, so a construct that makes
recovery hard is wrong even if it is parseable. Every peek is listed under
*Peeks*; a parser that needs one not listed there has found a defect in this
section.

### Six decisions

1. **`;` terminates every simple statement.** `firma`, `mutabilis`, the
   statement form of `sub`, `redde`, `rumpe`, `perge`, expression and
   assignment statements, and the module items `typus`, `firma`, `mutabilis`.
   Constructs ending in `}` take none. Layout is insignificant (§8.4), so
   without `;` a line beginning `(` `[` `-` `*` `&` is swallowed by the
   previous expression and misdiagnosed. Struct, `interfacies` and `externus`
   bodies are **separator-free**, as §4.3, §5.2, §5.3 and §10.1 write them: a
   type cannot be continued by an identifier or by `functio`, so the next
   member is decidable. A missing `;` is `EXS-E0201` with the insertion as its
   §8.3 fix payload.
2. **A lambda is `functio` in expression position** —
   `functio(x: f32) -> f32 { … }` — parameter types required, no `poscit`
   (§4.1 rule 5: its row is inferred and appears in its *type*, §4.2).
   `functio` has three roles that never share a position: item position is a
   declaration, type position a function type, expression position a lambda.
   `poscit` after a lambda's result type is `EXS-E0201`.
3. **`sub` has one production and two endings:** `sub P = e;` binds to the end
   of the enclosing block; `sub P = e { … }` binds for the block. Identical
   prefix, one-token decision, identical recovery (§4.5).
4. **Implementation reuses `interfacies`; the cast is `sicut`.**
   `interfacies Scriptura { … }` declares; `interfacies Scriptura in Scriptor
   poscit rete { … }` implements. One token after the head decides. `in` is
   already reserved, and the §10.1 line `publica interfacies legibilis`
   extends to `publica interfacies legibilis in lector` with no new word. The
   alternative `implet` was considered and rejected on §8.4's rule that a
   reserved word spends a root forever. The cast is postfix `sicut Type`:
   `n sicut u64` is §5.2's explicit widening; `w sicut dyn Scriptura poscit
   {rete}` is §4.4's trait object. After an operand `sicut` is a cast, inside
   a row a row item — the positions are disjoint. `impl`, `for` and `as`, as
   used by the probe fixtures, are inadmissible (§3.9.2).
5. **Module level** is `Annotation* ['publica'] Item`. `mutabilis` at module
   level **parses** and is rejected by the checker (`EXS-E0500`, `EXS-E0501`):
   a parse error can neither name the rule nor tell the capability-typed case
   apart. Module `firma` is a constant. There is no import statement; dotted
   paths are `Path`.
6. **Comparison is spelled with words, so `<` is never an operator.**
   `lt le gt ge eq ne` are contextual: an identifier in *operator* position is
   an operator, and identifiers otherwise occur only in *operand* position, so
   nothing collides and nothing is reserved. `<` after a path segment always
   opens generic arguments, and the lexer never forms `<<` `>>` `<=` `>=` —
   §8.4's closed token set does not contain them, so `acies<acies<f32, 4>, 4>`
   lexes as two `>` tokens. `a < b` is `EXS-E0201` **with the `<`→`lt` edit
   attached**. This settles what §8.4 left `[OPEN]` for the comparison words;
   `/` stays open.

   > An earlier draft specified the English message here — *"`<` opens
   > generic arguments; comparison is `lt`"*. That was wrong twice. §8.3 says
   > "codes are permanent; text is not" and that tools match codes rather
   > than prose, so pinning a sentence in a grammar section makes text
   > normative that the diagnostics section deliberately does not. And it
   > went unkept: `docs/design/diagnostics-review.md` measured this exact
   > case and found the span and the fix correct and the message
   > `unexpected token`, because `struct Diag` has no field to hold anything
   > else. What a grammar can normatively require is the code, the span and
   > the edit. It cannot require a sentence.

### Expressions

Precedence climbing over a fixed table, one-token peek per step.

| level | operators | assoc |
|---|---|---|
| 1 postfix | `.f` `(…)` `[…]` `?` `<…>` | left |
| 2 prefix | `-` `&` `*` | — |
| 3 cast | `sicut Type` | left |
| 4 multiplicative | `*` — `/`, remainder, and the `*%` `*\|` overflow forms are `[OPEN]` | left |
| 5 additive | `+` `+%` `+\|` `-` `-%` `-\|` | left |
| 6 range | `..` | none |
| 7 comparison | `lt` `le` `gt` `ge` `eq` `ne` | none |
| 8 conjunction | `et` | left |
| 9 disjunction | `vel` | left |

`&` and `*` are prefix in operand position and binary or type sigils
elsewhere; the position decides, never the token. Logical negation is
`[OPEN]`: no §8.4 token exists for it, and a word cannot serve — prefix
position *is* operand position, so `non(x)` would be a call. `et` and `vel`
are contextual words like `lt`, and are the only operators here not already
attested elsewhere in this document; they and the comparison words need a
§8.4 tier-2 entry. Assignment `=` is a **statement**, never an operator: it is
excluded from conditions, and the lvalue check is semantic.

Generic arguments and, when they exist, struct literals attach only directly
after a path segment (`IDENT`, `. IDENT`, or `<…>`). Struct-literal syntax is
`[OPEN]`; its position — `{` after a path segment — is reserved now, and
**`ExprNS`** (the expression grammar with that suffix disabled) is defined now,
so admitting it later touches nothing outside level 1. `ExprNS` is used in
every position where a `{` block follows an expression: `si`, `sin`, `dum`,
`terminus`, the `per`/`quisque` range, `discerne`, and the right-hand side of
`sub`. Array literals, tuples, slices, open-ended ranges, named arguments and
compound assignment are `[OPEN]`; none of them needs a new peek.

### Types

`Type ::= ('&' | '*')* CoreType (':' IDENT | 'apud' IDENT)*`. Prefixes are the
reference and raw-pointer sigils; suffixes are byte order (`u32:maior`) and
placement (`acies<f32, 1024> apud machina`, §5.5). `maior` `minor` `nativus`
are contextual. A `:` after a `CoreType` is a suffix; a `:` after an
identifier in a parameter, field, binding or generic parameter is an
annotation — `x: u32:maior` reads left to right with no peek.

**Bit-width types are decided by the parser.** In type position an identifier
of the form `u` followed by digits is a `BitType`, admitted only for widths
1–7 and 8, 16, 24, 32, 40, 48, 56, 64. This is what makes §5.2 rule 2 literally
true: `u12` *does not parse*, `EXS-E0201`. A byte-order suffix on a `BitType`
of width below 8 is likewise `EXS-E0201` (§5.2 rule 3). The check is one
table lookup on the token text.

Function types are `functio(A, B) -> C [poscit {R}]`, trait objects
`dyn Path [<…>] [poscit {R}]`. In type position the row is **braced**, because
these types sit in comma-separated lists where a bare row is ambiguous;
§4.4's `poscit P` is schematic for `poscit {P}`. Nested function types bind a
row innermost; parenthesise to override.

### Where `poscit` attaches

| position | form | ends at |
|---|---|---|
| function declaration, interface member, `externus` member | bare row after `-> Type` or `)` | first token that is not `,` |
| implementation head (`interfacies … in T poscit …`) | bare row | `{` |
| function type, `dyn` type | braced `poscit { … }` | `}` |
| lambda | none — `EXS-E0201` if present | — |

`RowItem ::= Path | 'sicut' IDENT` in both forms. `poscit {}` is an explicit
empty row; a bare `poscit` with no item is `EXS-E0201`. A type consumes
`poscit` **only when the next token is `{`** — so a declaration whose result
type is a function type still takes a bare row without parentheses, and a
body is never mistaken for a row. The one odd corner is deterministic:
`-> functio(A) -> B poscit {rete} poscit alloc {` is a braced row on the
result type followed by the declaration's own bare row, then the body.

### Grammar

Terminals are §8.4's tokens; `IDENT` `INT` `STRING` are the lexer's classes.
`[…]` optional, `(…)*` repetition, `|` alternation.

    Module        ::= Item* EOF
    Item          ::= Annotation* ['publica'] ItemBody
    Annotation    ::= ANNOT                                  (* one token: §8.4's `@nomen`; arguments [OPEN] *)
    ItemBody      ::= FunctionDecl | StructDecl | TypeDecl | InterfaceDecl
                    | PotestasDecl | ExternusBlock | BindingStmt
    FunctionDecl  ::= Signature Block
    Signature     ::= 'functio' IDENT [GenericParams] ParamList ['->' Type] [DeclRow]
    ParamList     ::= '(' [Param (',' Param)*] ')'
    Param         ::= IDENT ':' Type
    GenericParams ::= '<' GenericParam (',' GenericParam)* '>'
    GenericParam  ::= IDENT [':' Type]
    DeclRow       ::= 'poscit' RowItem (',' RowItem)*
    TypeRow       ::= 'poscit' '{' [RowItem (',' RowItem)*] '}'
    RowItem       ::= Path | 'sicut' IDENT
    StructDecl    ::= 'structura' IDENT [GenericParams] '{' Field* '}'
    Field         ::= IDENT ':' Type
    TypeDecl      ::= 'typus' IDENT [GenericParams] '=' Type ';'   (* sum types [OPEN] *)
    InterfaceDecl ::= 'interfacies' Path [GenericParams]
                      ( '{' Member* '}'
                      | 'in' Type [DeclRow] '{' FunctionDecl* '}' )
    Member        ::= Signature [Block]
    PotestasDecl  ::= 'potestas' IDENT '=' '{' [Path (',' Path)*] '}'
    ExternusBlock ::= 'externus' '(' STRING ',' IDENT ':' IDENT ')' '{' (['publica'] Signature)* '}'
    BindingStmt   ::= ('firma' | 'mutabilis') IDENT [':' Type] ['=' Expr] ';'

    Block         ::= '{' Stmt* '}'
    Stmt          ::= BindingStmt | SubStmt | JumpStmt | IfStmt | WhileStmt
                    | ForStmt | MatchStmt | Block | Expr ['=' Expr] ';'
    JumpStmt      ::= 'redde' [Expr] ';' | 'rumpe' ';' | 'perge' ';'
    SubStmt       ::= 'sub' Path '=' ExprNS ( ';' | Block )
    IfStmt        ::= 'si' ExprNS Block ('sin' ExprNS Block)* ['aliter' Block]
    WhileStmt     ::= 'dum' ExprNS ['terminus' ExprNS] Block
    ForStmt       ::= ('per' | 'quisque') IDENT 'in' ExprNS
                      Contrahe* ['forma' IDENT [INT]] Block
    Contrahe      ::= 'contrahe' IDENT ':' ArithOp
    MatchStmt     ::= 'discerne' ExprNS '{' MatchArm* ['aliter' Block] '}'
    MatchArm      ::= 'casus' Pattern Block
    Pattern       ::= Literal | Path                         (* constructors [OPEN] *)

    Expr          ::= Or           (* ExprNS: identical, level-1 '{' suffix disabled *)
    Or            ::= And ('vel' And)*
    And           ::= Cmp ('et' Cmp)*
    Cmp           ::= Range [CmpOp Range]
    CmpOp         ::= 'lt' | 'le' | 'gt' | 'ge' | 'eq' | 'ne'
    Range         ::= Add ['..' Add]
    Add           ::= Mul (('+' | '+%' | '+|' | '-' | '-%' | '-|') Mul)*
    Mul           ::= Cast ('*' Cast)*
    Cast          ::= Unary ('sicut' Type)*
    Unary         ::= ('-' | '&' | '*') Unary | Postfix
    Postfix       ::= Primary Suffix*
    Suffix        ::= '.' IDENT | '(' [Expr (',' Expr)*] ')' | '[' Expr ']' | '?'
                    | GenericArgs                            (* after a path segment only *)
    Primary       ::= Literal | '(' Expr ')' | Lambda | IDENT
    Lambda        ::= 'functio' ParamList ['->' Type] Block
    Literal       ::= INT | STRING                           (* INT grammar [OPEN], §8.4 *)
    ArithOp       ::= '+' | '+%' | '+|' | '-' | '-%' | '-|' | '*'

    Type          ::= ('&' | '*')* CoreType (':' IDENT | 'apud' IDENT)*
    CoreType      ::= BitType | Path [GenericArgs] | '(' Type ')'
                    | 'dyn' Path [GenericArgs] [TypeRow]
                    | 'functio' '(' [Type (',' Type)*] ')' '->' Type [TypeRow]
    BitType       ::= IDENT                                  (* 'u' + admitted width *)
    GenericArgs   ::= '<' TypeArg (',' TypeArg)* '>'
    TypeArg       ::= Type | INT
    Path          ::= IDENT ('.' IDENT)*

    EgoFile       ::= 'ego' Path '{' EgoEntry* '}' EOF
    EgoEntry      ::= 'versio' STRING | 'licentia' STRING
                    | 'fontes' '{' (Path HASH)* '}'
                    | ('hospites' | 'acceleratores' | 'exitus') '[' [Target (',' Target)*] ']'
                    | 'potestates' '{' [Path (',' Path)*] '}'
                    | 'numeri' '{' (IDENT IDENT)* '}'
                    | 'publica' ( Signature | 'typus' IDENT | 'structura' IDENT
                                | 'interfacies' Path ['in' Type] )
    Target        ::= IDENT ('-' IDENT)*

CST node kinds are these nonterminals plus `ERROR` and `MISSING`. The `ego`
file (§10.1) shares the lexer and the `Type`, `Signature` and `Path`
productions — the two must not drift — and has its own entry point, selected
by the driver and confirmed by the first token `ego`. Its entries are
keyword-led and self-delimiting, so it needs no `;`.

### Peeks

Every place the parser looks past the current token, and how it resolves.

| where | peek | resolution |
|---|---|---|
| statement start `functio` | `functio IDENT` | a nested named function: `EXS-E0201`, **no fix payload**; otherwise a lambda expression statement |
| after a function or `dyn` type | `poscit {` | braced `TypeRow` belongs to the type; bare `poscit` belongs to the enclosing declaration |
| struct / `interfacies` / `externus` body recovery | `IDENT :` or `}` | next member, or end of body |
| `TypeArg` | `INT` | value argument (`acies<f32, 1024>`); anything else is a `Type`; a named constant parses as `Path` and is resolved semantically |

Everything else is one token: `sub … ;` vs `sub … {`; `redde ;` vs
`redde Expr ;`; `interfacies X {` vs `<` vs `in`; `dum … terminus` vs
`dum … {`; `Expr =` vs `Expr ;`; `.` vs `..` (the lexer munches `..` first,
so `1..n` is `INT .. IDENT` — this depends on the `[OPEN]` numeric grammar
requiring a digit after a float's `.`); `casus` vs `aliter` vs `}`.

### Recovery

Synchronisation sets are small and fixed. Statement level: `;` and `}`, plus
the statement-leading keywords. Module level: `@`, `publica`, and the
item-leading keywords. Lists: `,` and the closing bracket. Bodies: the
`IDENT :` peek above. **No production consumes a `}` it did not open.** A
required token that is absent becomes a zero-width `MISSING` node; skipped
tokens go into an `ERROR` node; whitespace and comments are leading trivia of
the following token; and bytes the lexer consumed without tokenizing — an
`EXS-E0210` run, an `EXS-E0202` unterminated literal — reach the tree as
trivia, or the round trip is not byte-exact. **§8.1's rejected codepoints
never reach here at all**: §8.1 is a gate, so a file failing it is not
tokenized, and there is nothing to parse. An earlier draft of this paragraph
said they arrive as error tokens; that describes a lexer this project does
not have. Codes: `EXS-E0201` for any token where another was
required; `EXS-E0202` for a bracket, block, or `<…>` still open at end of
input; `EXS-E0203` for end of input inside any other construct; `EXS-E0220`
for a reserved word in identifier position; `EXS-E0210` is the lexer's. §13's
`02xx` range is coarse on purpose and nothing here needs a code it lacks.

### What this section corrects

`;` is now mandatory where §4.2 (`redde f(construe(v))`), §5.1 (`firma t:
textus = …`) and `examples/saluta.exsc` (`redde "…"`) omit it. Those were
written before this section and are wrong under it; the rule stands, because
the alternative is the misdiagnosis in decision 1. `examples/imprime.exsc`
parses as written. `interfacies` declaration bodies now hold `Member`, which
is §5.3's and §10.1's separator-free form, not a `;`-terminated one.

**`forma` takes a width, and the first draft of this grammar did not admit
one.** §5.4 defines `arborea w` and turns on it — the remainder rule is stated
in terms of `ceil(n / w)` — and `docs/design/ssa-ir.md` emits
`redinit f32 fadd arborea 8`. A bare `forma arborea` could not have been
lowered. The `INT` is **required after `arborea` and forbidden after
`ordinata`**, which is a semantic rule rather than a grammatical one: both
spellings parse, and a missing or surplus width is a checker diagnostic.
Found by `docs/design/typed-ast.md` asking what the tree must reserve.

### What building the parser corrected

`compiler/x86_64/cst/` implemented this section and reported ten findings.
Five were errors here and are fixed above: `Annotation` is **one** token
(§8.4's `@nomen`), not `'@' IDENT`; `abi` is a tier-2 contextual word, not a
terminal, and this notation did not distinguish the two kinds; `MatchArm` and
`Contrahe` needed names, because a CST cannot hold an unnamed repetition
group; the `functio IDENT` fix was specified as `firma nomen = functio(…) … ;`,
which is **three insertions at three places** where `diag/fix.inc` carries one
span and one text, so that diagnostic now carries no fix payload rather than a
partial one that would not compile; and recovery described error tokens that
§8.1's gate makes impossible.

Two were implementation latitude this section does not constrain and should
not. **A node per precedence level is not required where the level matched no
operator** — read literally, ten levels would make every atom ten nodes deep;
the parser emits a level's node only when that level consumed an operator.
And **`ExprNS` is today exactly `Expr`**, because struct literals are `[OPEN]`
and they are the only thing it excludes; it stays a separate entry point
because it is called from seven positions that will differ once they exist.

**What held is the part worth recording.** The parser needed *no two-token
peek this section does not list*. `-> functio(u32) -> u8 poscit {rete} poscit
alloc {` parses with zero diagnostics, as does `acies<acies<f32, 4>, 4>`, and
`a < b` is diagnosed at the `<` with a replace-fix to `lt`. Decision 6 —
refusing symbolic comparisons — is what makes that last one possible.

### Not settled here

- `[OPEN]` Numeric literal grammar (§8.4) — blocks the `..`/float rule above
  and the `HASH` token: `sha256-1a2b…` is neither identifier nor literal under
  any settled rule, so `fontes` cannot yet be lexed.
- `[OPEN]` Sum types and constructor patterns; `discerne`'s exhaustiveness
  presupposes an enumeration the language does not yet declare. The natural
  home is `typus` — keyword-led, LL(1)-harmless — but it is not decided.
- `[OPEN]` Brand syntax `positio<'t>` (§5.1): `'` is not a §8.4 token.
- `[OPEN]` Generic implementation heads: `interfacies Legibilis<T> in
  acies<T, N>` leaves `N` unbound.
- `[OPEN]` Operators: `/`, remainder, shifts, logical negation, the `*%` `*|`
  family. Shifts must not become `<<`/`>>` tokens.
- `[OPEN]` `refero` (§6.4) is reserved and has no phrase-level form here; `apud`
  is admitted only as a type suffix; annotation arguments; struct literals;
  labelled `rumpe`/`perge`; a `sub` list form; `si`/`discerne` as expressions.
- `[OPEN]` `sub` inside `per`/`quisque` bodies (§8.5).
- `[OPEN]` Whether an unbounded `dum` is admitted outside the `certus` profile.
  The grammar makes `terminus` optional so that its absence is a CST fact the
  profile can reject (§8.5); the full language's answer is not given.

---

# 9. Compilation and the build model

## 9.1 Pipeline

```
source → lossless CST → typed AST → SSA IR → backend
```

- **Lossless CST** (red-green tree, Roslyn/rowan). Error-tolerant, preserves trivia. Required for LSP and formatter.
- **Typed AST**, spans on every node. *Typed* means the slots exist, not that
  they are filled: §16 puts the AST in Stage 1 and the type checker in Stage 2,
  so the tree is built with empty type slots and Stage 2 fills them **in
  place**. It is an annotation pass, not a rewrite — the language has no
  implicit conversions (§5.2's `mensura`, §5.4's promotion, §6.3's boxing are
  all explicit), so a checked tree has the same shape as an unchecked one. See
  `docs/design/typed-ast.md`.
- **SSA IR** built from the AST via Braun et al. (CC 2013) — on-the-fly, no prior analysis, minimal and pruned.

"Straight from IR" means **owning the mid-level SSA IR**, not skipping the AST. Skipping it costs diagnostics, LSP, and macros, and cannot be undone later.

## 9.2 Backend

**Two backends, with distinct roles.** This replaces "C backend first," which
was stated without defending an asymmetry it created: the compiler is written
in fasmg assembly for the purity contract (§18.1), and then emitted C — a
routing decision nobody priced.

| backend | role |
|---|---|
| **fasmg** | **the reference.** Defines the semantics. Keeps the closure `{fasmg}` end to end, including through self-hosting. |
| **C** | **reach.** Every target a C compiler supports; RISC-V and embedded Linux immediately; pairs with `zig cc`. |

**The fasmg backend is normative.** Where the C backend cannot reproduce its
result, that is a documented limitation of the C *target*, never a second
dialect of the language. Both backends compiling the same module must produce
identical observable results, and a C target that cannot honour the declared
`numeri` **fails the build** rather than degrading silently — the rule §5.5
already sets for devices, applied to backends.

This is the third instance of one pattern, not a new idea: `vendor/hydramesh-wire/`
certifies eleven implementations against one reference, §5.5 requires CPU and
GPU to agree bit for bit, and `docs/design/amdgpu-backend.md` requires every
kernel to match a scalar reference. Reference-plus-differential-test is how
this project checks things.

**Why a reference backend is not optional.** §5.4's guarantees cannot be
expressed in portable C: `subnormales conservata` is MXCSR runtime state,
trapping arithmetic is UB, `reassociatio vetita` depends on flags passed to a
compiler downstream of the artifact — which leaks the guarantee out of `exsc`
and weakens §9.3. In assembly each is direct: emit the MXCSR setup, emit
`add`/`jo`, select the instructions yourself. **Through C alone, §5.4 is not
merely hard to honour — it is untestable**, and a criterion that cannot fail is
not a criterion. See ADR 0012.

The clean SSA boundary is what makes two backends affordable. Each is written
against the IR contract (`docs/design/ssa-ir.md`), so each can be built and
tested against hand-written IR, with no frontend and without the other.

Compile speed was measured and is **not** the constraint I previously claimed. `[UNREPRODUCED]` — the measurement harness is absent from the tree; figures carried forward from v0.2. gcc `-O0` on backend-style generated C:

| shape | kloc/s |
|---|---|
| many small functions | 40–66 |
| 2 × 20,000-statement functions (pathological) | 26 |

Projected full rebuild at 3× source-to-C expansion, single core, no parallelism: **10k Exsecutor LOC → 0.76 s**. Superlinear degradation on huge functions is real (2.5×) but not the exponential blowup feared; a backend emitting one C function per source function stays in the favourable range.

**QBE stays at Stage 5**, not Stage 3. The earlier recommendation to pull it forward is withdrawn — it was based on an assumption the measurement contradicted. The remaining argument for QBE is generated-C debug info, which is a debuggability question judged on its own in Stage 3.

Later options: QBE (~8k LOC, SSA, implements the C ABI in full, targets riscv64), Cranelift (Rust-native, mature), LLVM (release builds, once there are users).

**Do not write an optimizing backend.** Zig's team took 2020–2025 to make a self-hosted x86-64 backend the debug default.

That warning is about *optimizing* backends and does not forbid the reference
one. The fasmg backend is deliberately **naive** — every value spilled to a
stack slot, no register allocation, slow by construction and correct by
construction. Conflating the naive backend with a good one is the mistake that
would make this decision expensive. Speed is the C backend's job, and after it,
QBE's. `[OPEN]` — "naive is cheap" is an estimate, not a measurement, and the
register allocator that a fast backend needs is real engineering however it is
scheduled.

## 9.3 Compiler purity contract

`exsc` is a pure function of (source, `ego`, lockfile, flags):

- Reads **no** environment variables. `--env KEY=VALUE` *supplies* values on the command line instead (below).
- Never calls `setlocale`; internal text handling obeys the same rules imposed on user code.
- No `$HOME`, no user config, no dotfile discovery.
- **No network access, ever, at any phase.**
- No implicit clock. No `__DATE__`. Timestamps come from `--epoch`.
- Paths remapped by default, not opt-in.
- Deterministic symbol emission, hash iteration, section ordering.
- Byte-identical output for identical inputs across directories, times, locales, hostnames.

`exsc proba-reproducibilitatem` builds twice under deliberately divergent ambient conditions and diffs. Ships in v1, runs in CI.

### `--env` and `--epoch`

Both were named once each and never specified. A driver cannot be written
against that, and an implementer guessing is how a purity contract acquires an
undocumented exception.

**`--env KEY=VALUE` supplies a value; it does not permit a read.** `exsc` never
calls `getenv`, not even for an allowlisted key. The value comes from the
command line, which means it is already covered by *"a pure function of (source,
`ego`, lockfile, flags)"* — the ambient environment is not consulted, so there
is nothing to be non-deterministic about. An allowlist that permitted reads
would leave the value ambient and merely gated.

- Repeatable. `KEY` matches an identifier (§8.2); `VALUE` is the rest of the
  argument verbatim, including `=` and spaces.
- **A repeated `KEY` is an error, not a last-wins.** Last-wins makes the result
  depend on argument order, which is exactly the class this section removes.
- Order is therefore not observable, and two invocations differing only in
  `--env` order produce identical bytes.

**`--epoch N`** is decimal seconds since the Unix epoch. Optional, **defaulting
to 0**. A fixed default is deterministic — what §9.3 forbids is an *implicit
clock*, not a constant — and 0 has the useful property of being obviously not
now: a 1970 timestamp in an artifact is a visible signal that nobody set it,
where a default of "build time" would be silent and wrong.

**Neither is an `EXS-E` diagnostic.** §13 registers codes for the program being
compiled; a malformed flag is a usage error about the invocation, not about any
source. `exsc` exits nonzero with a message and no code, and the four failure
channels in `docs/asm-conventions.md` (§1.3) are unaffected.

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

The `numeri` block (§5.4, §10.1) is on the **interface** side of this line: floating-point semantics change what a call computes, so a dependent must rebuild when they change. Numeric policy is interface, not implementation detail.

Keying a build cache on the interface hash lets an attacker change only an implementation and ship it under an unchanged key — a poisoned shared or remote cache serves it to everyone. Named as an invariant because it is subtle enough to be got wrong by implementation judgment.

---

# 10. The `ego` file

*ego* (ἐγώ, "I") — the module declaring itself. One format, per module, from leaf to root package. It is a C header and a Nix flake combined: the module's **interface** and its **dependency closure**, in one declarative, total, non-Turing-complete file, evaluable without compiling the implementation.

## 10.1 Format

```exsecutor
// ego.exsc — generated by `exsc ego --emitte`, verified in CI. Never hand-edited.

ego norma.textus {
    versio    "0.4.1"
    licentia  "LGPL-3.0-or-later"

    fontes {
        norma.nucleus  sha256-1a2b3c…
        unicode.data   sha256-9f8e7d…      // required wherever text is used
        lexicon.norma  sha256-3e5a91…      // the morpheme table
    }

    hospites [ x86_64-linux, aarch64-linux, riscv64-linux, wasm32-wasi, none-eabi ]
    acceleratores [ ]                       // §5.5 -- no device code in this module

    potestates { }                          // observes nothing

    numeri {                                // §5.4 -- interface-affecting
        rotundatio    ad_parem
        reassociatio  vetita
        contractio    explicita
        subnormales   conservata
    }

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
- **Interface identity is separate from implementation identity.** Changing a body without changing a signature leaves the `ego` hash unchanged, so dependents do not rebuild. Combined with dictionary-passing generics (§7.1), this actually works — **but only because of §4.4's ceiling**, and it did not before. `prototypes/gendict/` showed that without it, a new or edited implementation in an unrelated module could invalidate an already-compiled generic's soundness while changing no signature, no body of that generic, and no `ego` hash: the rebuild trigger never fires and soundness is gone anyway. The ceiling is what puts the fact a generic relies on inside the trait's *interface*, where the hash can see it. See §9.6 for the hash that must *not* be reused.
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

**The minimum build invocation is `exsc aedifica --hospes TRIPLE SOURCE -o OUT`.**
Pinned because it was not written down anywhere and two things already needed
it: `tools/publish-gate.sh` was invoking a bare `exsc SOURCE OUT`, which §9.5
makes an error outright — *"`exsc` with no `--hospes` is an error"* — and a
driver cannot be implemented against an unspecified spelling. A subcommand is
required; there is no bare form.

**`SOURCE` may be repeated.** The files named form one compilation unit — one
module in §10's sense: one `ego`, one interface hash, one artifact. Their
order on the command line is the order of their items and is part of the
input, so §9.3's byte-identity holds for the same command and need not survive
a reordering. This is the package model of Go and Java. It is chosen now
because the hello world is three functions in three files
(`examples/saluta.exsc`, `imprime.exsc`, `initium.exsc`), and the alternative
— importing across modules — waits on the `ego` reader, the closure resolver
and `EXS-E0105`'s whole-closure check, none of which a first program should
wait on. Imports are unaffected: a module is still the unit they name.

**`OUT` is text, and `exsc` never assembles it.** §18.1 says both backends
emit text — fasmg source for the reference backend, C for the reach backend —
and `-o OUT` names that text. Turning it into a binary is the build's step:
for the reference backend, `fasmg OUT BIN` with `vendor/fasmg-x86/` on the
include path, the one tool §18.1 puts in the closure. The emitted text is
self-contained — the runtime prelude is emitted into it, not found on a path.
`exsc` does not run the assembler and cannot: `execve` is not on the syscall
allowlist (§9.3, `CLAUDE.md`), and locating an assembler by `PATH` is ambient
state of exactly the kind the contract forbids. A build that wants one command
has `buildExsecutorPackage` (§16, Stage 3); a two-step build belongs there,
not inside the compiler.

`exsc novum` scaffolds a working project with a valid `ego.exsc` in one command — first running program under 60 seconds or onboarding has failed.

Scratch work without ambient authority: `exsc curre --potestates omnes scratch.exsc`. Grants typed on the command line — explicit, not ambient, one flag of friction.

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
| `EXS-E0201` | unexpected token |
| `EXS-E0202` | unterminated construct |
| `EXS-E0203` | unexpected end of input |
| `EXS-E0210` | malformed literal |
| `EXS-E0220` | reserved keyword used as identifier |
| `EXS-E0311` | integer index applied to `textus` |
| `EXS-E0321` | `:nativus` in a `@transitus` type |
| `EXS-E0322` | implicit padding in a `@transitus` type |
| `EXS-E0332` | branded offset applied to the wrong buffer |
| `EXS-E0341` | reduction accumulator read inside its own body |
| `EXS-E0342` | `rumpe` inside an iteration carrying a `contrahe` |
| `EXS-E0421` | undeclared capability (atom or row) |
| `EXS-E0500` | module-level mutable state |
| `EXS-E0501` | capability stored in module-level state |
| `EXS-E0510` | capability escapes a declared bound (trait ceiling or `dyn`) |
| `EXS-E0520` | non-atomic `refero` crossing `externus` |
| `EXS-E0601` | public name does not decompose into the morpheme table |
| `EXS-E0602` | suffix contract disagrees with declared type |
| `EXS-E0603` | prefix signature law violated |
| `EXS-E0610` | composition depth exceeded (max two affixes) |
| `EXS-E0701` | target cannot honour the declared `numeri` |
| `EXS-E0801` | allocation after `initium` under `profilum certum` |
| `EXS-E0802` | recursion detected under `profilum certum` |
| `EXS-E0811` | loop without a statically known bound under `profilum certum` |
| `EXS-E0812` | generic does not monomorphize within the closure under `profilum certum` |
| `EXS-E0813` | capturing closure under `profilum certum` |
| `EXS-E0821` | capability forbidden by `profilum certum` |
| `EXS-E0831` | `numeri` not fully declared under `profilum certum` |
| `EXS-E0841` | `certus` module depends on a non-`certus` module |

The `02xx` range is deliberately coarse. Code granularity and message quality
are independent axes: a consumer branches on *what class of thing broke* — which
is what an editor needs to decide whether to auto-close a delimiter or stay
quiet mid-typing — while *which* construct broke belongs in the span and the
structured fix payload §8.3 already requires. `EXS-E0220` earns a code of its
own on frequency: §8.4's keywords are Latin words that read like plausible
identifiers, so reserved-word-as-identifier is the predictable error, and its
fix is mechanically derivable.

`EXS-E0510`'s **text** broadened when §4.4 did; the code did not move. §8.3 is
explicit that "codes are permanent; text is not", and the two cases are one
violation — a capability escaping a declared bound — differing only in where
the bound was written. The `dyn` case turns out to be the special one.

`EXS-E0341` and `EXS-E0342` close a gap §5.4 opened. That section states two
rules — a running accumulator is not readable inside the reduction body, and
`rumpe` is forbidden inside an iteration carrying a `contrahe` — and neither
had a code to be reported with. Both are `03xx` because they are semantic
rules about a §5.x construct, alongside `EXS-E0311`'s `textus` index and
`EXS-E0332`'s branded offsets. Neither carries a machine-applicable fix: the
edit in both cases is a restructure that changes what the program computes.

`08xx` is the `certus` profile (`docs/design/profile-certus.md`, ADR 0010).
The grouping is the profile's own section structure, not an invention:
`080x` memory (its rules 3–4), `081x` control flow (6, 7, 9), `082x`
capabilities (10–13), `083x` arithmetic (14), `084x` interface closure (19).
Every one of these is *only* a diagnostic under `profilum certum` — the same
program outside the profile is well-formed, which is why they are a range of
their own rather than additions to `04xx` or `05xx`.

ADR 0010 called this amendment "a prerequisite, not a follow-up" and it went
unwritten for some time; the profile checker could not have been built without
it, because CLAUDE.md forbids inventing a code and §13 is the only source.

`EXS-E0811` is narrower than it looks now that §8.5 exists. A `dum` with no
`terminus` is a *parse-level* fact, so the common case is caught before this
code is reached; `EXS-E0811` is for a `terminus` whose bound is not statically
known. A safety rule that reduces to a missing keyword is much cheaper than one
requiring analysis, which is the point §9.2 makes about `certus` rule 6.

**Only `EXS-E0831` carries a machine-applicable fix.** §8.3 scopes fixes to
cases where "the required edit is mechanically derivable," and for seven of
these eight it is not — moving an allocation, unwinding recursion, or dropping
a capability all change what the program does. Undeclared `numeri` is the
exception: §5.4 defines the defaults (`ad_parem`, `vetita`, `explicita`,
`conservata`) and rule 14 requires only that they be written down, so the fix
is exactly those four lines.

`EXS-E0701` opens a `07xx` range for **target** failures — the compilation is
well-formed and the *target* cannot deliver it. §5.5 has required "a target
that cannot honour the declared `numeri` fails the build" since it was written,
and §9.2 extended that to backends, but neither had a code to fail with.
Found by writing ADR 0012 against the amended §9.2, which is the first time
anything tried to use it. CLAUDE.md is explicit that a new code needs a §13
amendment *first*; this one was owed.

Codes are permanent and never renumbered (§8.3), so under-committing is
recoverable and over-committing is not. `0204`-`0209`, `0211`-`0219` and
`0221`-`0299` are free for what parsing actually turns out to need.

---

# 14. Conformance suite

Ships with v1. Each entry must **fail to compile**, except entries 16 and 17, which must produce identical bytes.

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
18. Source with a UTF-8 BOM → `EXS-E0101`
19. Mixed-script identifier, single module → `EXS-E0104`
20. CRLF line ending → `EXS-E0106`
21. `@transitus` bit widths not summing to a whole byte → `EXS-E0322`
22. `u4:maior` — byte order on a sub-byte field → `EXS-E0201`
23. `DeModFrame` encode/decode → byte-identical to all 246 vectors of `vendor/hydramesh-wire/golden_vectors.json`
24. Implementation whose mark exceeds the trait's declared ceiling, reached only through a generic → `EXS-E0510`

Entries 18-20 close a gap: §8.1 defines six source-policy codes and only three
of them (`E0102`, `E0103`, `E0105`) had an entry, while `E0101`, `E0104` and
`E0106` are exactly what a Stage 1 lexer implements first.

Entries 21-23 cover §5.2's bit-width rules. **23 is different in kind from
every other entry here**: the rest are cases this project wrote for itself,
while 23 is an external certificate this project must satisfy. Under its own
theorem, matching all 246 vectors is equivalent to agreeing with the reference
implementation on every one of 2^108 frames — so it is the one conformance
entry whose passing means something to somebody else. They are appended
rather than interleaved because existing entries are referenced by number
elsewhere in the tree, and renumbering a referenced list is the same mistake as
renumbering a code.

---

# 15. Open problems

Worst first, for items 1-8. **New items append rather than insert**, because
these are cited by number across the ADRs and design documents, and renumbering
a referenced list is the same mistake as renumbering an error code (§8.3).

1. **Closure capture in capability rows** (§4.2). **Found, fixed, re-measured.** The probe accepted a genuine violation; §4.2 was amended so rows travel with function-typed values rather than being resolved by name at a call site; the same file is now rejected as `EXS-E0421` and the correctly-declared variant is still accepted. No longer blocks Stage 1. What remains `[UNTESTED]` is soundness — nine probe cases are not a proof, and none of this is in a compiler. Note also that the six attacks the original prototype was said to be validated against were never enumerated anywhere; the rebuilt probe **chose** six, and says so.
2. **Lexicon derivation test** (§16). Cannot be retired by more engineering — needs human subjects. No longer gates whether §3 is load-bearing (ADR 0005); §3 is retained regardless. What remains open is the *size of its cost*, which is unmeasured.
3. **Reference cycles** (§6.7). No answer. Accepted cost, with a DoS exposure to document. **Still open for the full language.** The `certus` safety-critical profile (`docs/design/profile-certus.md`, ADR 0010) dissolves it by forbidding reference counting outright — arena-only, sized at `initium` — so cycles are structurally impossible there. That is a restriction, not a solution, and does not close this item.
4. **Root coinage governance** (§3.8). Resolved as design by §3.9 — exhaustion test, a five-rung coinage ladder, review, permanent registration against the content-addressed morpheme table, and a loan register kept as a running measurement of whether §3 scales. `[UNTESTED]`: no root has been coined through it, and `norma.algebra` (which needs *lane*, *stride*, *pivot*, *eigenvalue*) is its first real exercise.
5. **Generics × capability rows × dictionary layout** (§7.1). **Prototyped, and it found a soundness hole.** `prototypes/gendict/` demonstrated that §4.4's exceeds-check, stated only for `dyn` construction, left an implementation reached solely through a generic checked against nothing — and that a generic could launder a value past a `dyn` bound that would have caught it directly. §4.4 now makes a trait's declared rows a ceiling on *every* implementation, which closes both and is what makes §10.2's rebuild claim true rather than hopeful. Witness-table layout turned out to be stable; the row is bounded by the interface, so layout never depended on it. Still `[OPEN]`: no syntax for a row on a **type parameter** — `poscit sicut T` parses and is inert. `[UNTESTED]` — ten Python cases are not a proof and none of this is in a compiler.
6. **Generator model coverage** (§9.4). "No build scripts" may not survive real FFI binding generation.
7. **Generated-C debug info** (§9.2). If stepping through Exsecutor is unusable, QBE moves earlier.
8. **Ecosystem bootstrapping.** Unaddressed by anything in this document, and the actual reason languages die.
9. **Phrase grammar.** `[UNTESTED]` §8.6 now states it as normative — items, statements, the expression precedence table, types, `poscit` attachment, the lambda, `sub`'s two forms, `interfacies … in …` and the `sicut` cast, the `ego` entry point, every peek and every synchronisation set. That closes what this item originally recorded: there *is* a grammar, and it is the one the CST is built against. It does not close the item. Nothing has parsed anything — §9.1's CST does not exist, so the LL(1) claim and the recovery design are unexercised. §8.6's own list is still open: numeric literals and the `HASH` token, sum types and constructor patterns, brand syntax, generic implementation heads, the operators §8.4 lacks, and `sub` inside loop bodies. It also found three contradictions it did not paper over, all since fixed: §4.2's and §5.1's illustrative blocks and `examples/saluta.exsc` omitted the `;` §8.6 requires; §8.4 still marked the comparison words `[OPEN]` and lacked their tier-2 entries; and §4.4 wrote `dyn Trait poscit P` bare, which is not a form that parses. The canonical program gaining a semicolon is the useful one — `tests/unit/lexer_tokens.asm` pins its token count precisely so a change there is a deliberate edit, and the assert caught it. Position in this list is chronological, per the note above.

---

# 16. Roadmap

**Stage 0 — validation.** Four of five kill criteria retired with evidence: CVE gate passed (§2), ARC measured (§6.2), compile speed measured (§9.2), capability rows prototyped (§4.2).

Remaining, before Stage 1:
- **Closure capture in the prototype.** Done, and it did its job: it returned a negative, §4.2 was redesigned so rows travel with values, and the probe now rejects the violation while still accepting the correctly-declared variant. This is what a kill criterion looks like when it fires and the design survives.
- **The derivation test.** Print the affix table and twenty roots; give twenty derivation tasks; score against recall accuracy on an equivalent English API. Days, no compiler, no engineering. **Cheapest high-value experiment in the project.**
  - **No longer a kill criterion.** v0.3 originally read: *"if derivation accuracy does not clearly beat English recall, §3 is decorative — and everything else in this spec survives unchanged with English roots in the same derivational frame."* That branch has been closed by decision — §3 is retained whatever the number says, because the lexicon is an identity commitment rather than a hypothesis (ADR 0005). The test is still worth running as **calibration**: it measures what §3 costs, which affixes and roots produce errors, and therefore which of `EXS-E0601`–`EXS-E0610` need the best diagnostics and the widest `exsc emenda` coverage. The English control is kept because it makes that cost measurable rather than anecdotal.

**Stage 1 — frontend (3–4 months).** Lexer with the full §8.1 policy. Lossless CST. Typed AST. Diagnostics with spans and stable codes.
*Kill:* diagnostics quality is not retrofittable. Bad here → stop and fix.

**Stage 2 — types and capabilities (4–6 months).** Type checker. Capability rows with substitution and inference. Capability-bearing types and `dyn` bounds. Text types and brands. `@transitus` enforcement. Lexicon checking.
*Kill:* if `sub` resolution needs a search algorithm, redesign.

**Stage 3 — backends, runtime, Nix (3–4 months).** SSA lowering via Braun. The **fasmg reference backend** (§9.2), then C emission. Cross-compile to RISC-V and embedded Linux. `buildExsecutorPackage`. `proba-reproducibilitatem` green in CI, and the two backends agreeing on every conformance module.
*Kill:* undebuggable generated C → move to QBE.

> **The reference backend does not have to wait for Stage 1.** §9.2's clean SSA
> boundary means it is written against the IR contract
> (`docs/design/ssa-ir.md`), not against a frontend — so it can be built and
> tested against hand-written IR while the lexer and CST are still being
> written, the way LLVM backends are tested against `.ll` files no frontend
> produced.
>
> There is a reason to do exactly that. §9.2's own kill criterion —
> *undebuggable generated C* — **cannot fire until some C has been generated**,
> so as written it is a criterion that decides whether QBE moves earlier and is
> only evaluable after two stages have been built assuming it did not. The same
> holds for §5.4: its guarantees are untestable through C, so nothing verifies
> them until a reference backend exists. Both are arguments for pulling the
> reference backend forward rather than for the ordering above.
>
> `[OPEN]` — not rescheduled here. Moving it is a decision about where effort
> goes, and the estimate that a naive backend is cheap has not been measured.

**Stage 4 — tooling (4–8 months).** LSP, formatter, package manager.
*Kill:* if any is intractable because of a language decision, the decision is wrong. Change it.

**Stage 5 — users, then optimization.** QBE or Cranelift. Self-hosting. LLVM for release.

**Estimate: 14–22 months** to something usable, assuming this is not the only project competing for attention. Treat as optimistic: three earlier estimates in this project were revised after measurement, all in the direction of more surprises rather than fewer.

---

# 17. What would make this fail

- The derivation test comes back negative and §3 costs more than it returns. Since ADR 0005 retains §3 regardless, this failure mode is now **accepted rather than mitigated** — the documented fallback of English roots in the same derivational frame has been closed by choice. The risk below is the same risk, undiluted.
- Closure capture cannot be checked soundly, and the audit view — the strongest artifact here — becomes theatre. This fired once: §4.2's original rule missed an escaped closure, demonstrated by a running probe, and the rule was changed so rows travel with values (§4.2). The risk is **reduced, not retired** — nine probe cases are not a soundness argument, and the interaction with dictionary-passing generics (§15 #5) is now harder.
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
- **Both of §9.2's backends emit text, not machine code** — C for the reach backend, fasmg source for the reference one, and neither is machine encoding. Among the easier things to do in assembly. The costly parts of the pipeline are the CST (§9.1) and the Unicode layer (§8), not codegen. The reference backend also emits into the *same* assembler the compiler is written in, so it inherits the vendored macro package rather than needing a second emitter.

## 18.2 Accepted costs

- **§12's demo is dropped.** `exsc aedifica --hospes riscv64-linux` on a Mac is impossible when the compiler is x86-64 machine code. `buildPlatform` is pinned to `x86_64-linux`. Cross-compilation to RISC-V is unaffected — that is `targetPlatform` — but the demo §12 identified as the adoption hook is gone until an aarch64 host exists, and §17 lists never getting users as the top failure mode.
- `[OPEN]` **The LSP is deferred.** §12 calls it non-negotiable and that judgement stands unchanged; incremental reparse over a red-green tree in assembly is not Stage 1 work. This is an amendment, not an omission.
- **aarch64 and riscv64 hosts are full rewrites** of the architecture-specific tree. §5.3's minimum ABI coverage is about what `exsc` can target and is unaffected; this is about where `exsc` can run.
- **Debugging degrades.** §12 already flags DWARF as the C backend's weak point; hand-written assembly does not improve it. Mitigated by stage dumps at every pipeline boundary rather than by a debugger.
- **§16's revise-on-measurement loop gets expensive**, in direct tension with this document's method (see the evidence-base note above: prose designs are hypotheses until code runs). The Python probe layer is the mitigation — design questions get answered where iteration is cheap, and only settled answers are written in assembly.
