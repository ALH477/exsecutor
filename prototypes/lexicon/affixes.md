Exsecutor — Affix Reference Sheet
=================================

**You are about to name things in a language you have never seen.** This one
page, plus the companion sheet `roots-20.md`, is the complete and only
reference material for that task. Do not consult the spec, a dictionary, a
search engine, or a machine translator. Everything you need is below or on
the roots sheet.

Section numbers (`§3.4` etc.) refer to the Exsecutor language specification.
They are printed here so the source of each rule is traceable, not so you go
look anything up — you should not need to.

---

## The one rule

> Every name is built from **(prefix) + root + suffix**. At most **one**
> prefix. Exactly **one** suffix. The root comes from `roots-20.md`; the
> prefix and suffix come from the two tables below. Parts are joined by
> straight concatenation — nothing is dropped, doubled, or respelled at the
> joins. §3.1, §3.8

A name with no prefix is just root + suffix. A name with a prefix is
prefix + root + suffix — three pieces glued together as plain text.

---

## Stems: every root has two spellings

Latin roots on `roots-20.md` are given as a **present** stem and a **supine**
stem (e.g. `leg-` / `lect-`). Each suffix below says which stem it attaches
to. Use the wrong stem and the join is wrong even if every letter you wrote
is otherwise correct — e.g. the agent of `leg-` is `lector` (supine `lect-` +
`-or`), never `legor`. §3.3

The six Greek roots on `roots-20.md` are given as a **single** form. They are
not verbs and carry no present/supine split; treat the form given as the
whole root.

---

## Suffix table — exactly one, always last

Attaches directly to the stem the table names. Straight concatenation: write
the stem, then write the suffix, with nothing changed in between.

| suffix | attaches to | meaning | this name is declared as |
|---|---|---|---|
| `-e` | present stem | the action itself | `functio` — a function |
| `-or` | supine stem | the agent — one who/that does it | `structura` — a concrete value type |
| `-ibilis` | present stem | can-be-Xed | `interfacies` — an interface/contract |
| `-io` | supine stem | the action, reified as a thing | `structura` — a concrete value type |
| `-us` | supine stem | the result of doing it | `typus` — a type |
| `-orium` | supine stem | the instrument that does it | `structura` — a concrete value type |

§3.4. If a task tells you what *kind* of thing to name (a function, a type
that can be held, the result of an operation, an instrument…), that kind
tells you which row of this table to use, which tells you which stem to
pull from `roots-20.md`.

(The plain-English words after each em dash — "a function," "a concrete
value type," and so on — are this sheet's gloss, not spec prose. The four
Latin words themselves, `functio`/`structura`/`interfacies`/`typus`, are
§3.4's own "must declare" column, verbatim. `structura`'s gloss follows
§6.3's own characterization, "`structura` is a value type. Always.")

Worked pattern, straight from the spec, using `leg-` / `lect-` (read) —
memorize the *shape* of this, not the words:

```
lege        functio      (leg-   + -e)       the act of reading
lector      structura    (lect-  + -or)      one who reads
legibilis   interfacies  (leg-   + -ibilis)  can be read
lectio      structura    (lect-  + -io)      a read operation, reified
lectus      typus        (lect-  + -us)      the result of reading
lectorium   structura    (lect-  + -orium)   an instrument for reading
```
§3.7

---

## Prefix table — at most one, always first

| prefix | gloss | if the name is a `functio`, its signature must obey | on `structura` / `typus` / `interfacies` |
|---|---|---|---|
| `re-` | again | same signature as the bare form | positional/semantic only — no law to check |
| `de-` | reverse | the base's **return type** becomes the new **first parameter** | ″ |
| `ex-` | out of | **first parameter** is the source type | ″ |
| `in-` | into | **first parameter** is the destination type | ″ |
| `trans-` | across | parameter type and return type **differ** | ″ |
| `con-` | together | takes **two or more** of the base's operand type | ″ |
| `prae-` | before | positional only, no signature law | ″ |
| `sub-` | under | positional only, no signature law | ″ |

(§3.5 publishes `prae-` and `sub-` as one combined table row — same gloss
pair "before, under," same law "positional only" — since both behave
identically. They're split into two rows here only for readability; nothing
about either prefix is changed.)

§3.5. A prefix is optional. When a task describes a function's *parameters*
(what it takes, what it returns, how many of something), match that
description against the middle column to pick the prefix — the description
is telling you which law the name has to satisfy. When a task describes a
type, an interface, or an instrument, a prefix (if the meaning calls for
one at all) is decorative only — right-hand column.

---

## No assimilation — write it wrong, on purpose

Real Latin smooths the seam where a prefix meets a root: `con-` + `leg-`
would classically become *collega*; `trans-` + `scrib-` becomes *transcribe*;
`in-` + `leg-` becomes *illeg-*. **Exsecutor does not do this.** It
concatenates the pieces exactly as given, letter for letter:

| prefix + root | classical Latin (do NOT write this) | Exsecutor (write this) |
|---|---|---|
| `con-` + `leg-` | *collega* | **`conlege`** |
| `trans-` + `scrib-` | *transcribe* | **`transscribe`** |
| `in-` + `leg-` | *illeg-* | **`inlege`** |

§3.6. If a derivation you produce "looks wrong" or "looks like a typo"
because a letter seems doubled or a consonant seems unshifted — that is
usually a sign you did it **correctly**. Resist the urge to smooth it out.

---

## The two-affix ceiling

**A name has at most one prefix and exactly one suffix — two affixes,
total, and no more.** If the meaning you are given seems to call for two
prefixes at once (e.g. "again" *and* "together" in the same name), it
cannot be built as a single name under this rule. Pick the one qualifier
that matters most and drop the other, or say plainly that it does not fit
in one name. Do not stack a second prefix to try to fit everything in — a
name with two prefixes is invalid. §3.8

---

## Worked example: the language's own name

`exsecutor` — "one who carries out" — is built entirely from the material
above and from `roots-20.md`, at exactly the two-affix ceiling:

| piece | source | note |
|---|---|---|
| `ex-` | prefix table, above | "out of" |
| `secut-` | `roots-20.md` | supine stem of `sequ-`, "follow" |
| `-or` | suffix table, above | supine stem + agent → declares `structura` |

Concatenate without assimilating (the no-assimilation rule above): `ex` +
`secut` + `or` = **`exsecutor`**. One prefix, one suffix — two affixes,
exactly at the ceiling. Nothing here is a special case; it follows the same
two tables you have for every task on the sheet. (Spec front matter,
"Name"; `docs/decisions/0001-name-exsecutor.md`.)

---

## Procedure

For each task, in order:

1. Decide the declared kind you're naming — function, type, interface,
   instrument, result. This fixes your suffix (table above) and which stem
   it wants.
2. Find the root on `roots-20.md`. Pull the stem the suffix asked for.
3. If the meaning also calls for a prefix, match it against the prefix
   table's middle column (for a function) or use it positionally (for
   everything else). At most one.
4. Concatenate prefix (if any) + stem + suffix, letter for letter. Do not
   smooth, drop, or double anything at the seams.
5. Check: is this at or under two affixes total? If not, you have the wrong
   decomposition — go back to step 3.
