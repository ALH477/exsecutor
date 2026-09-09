# Phrase grammar — design plan

Status: `[OPEN]`. Design only; no parser exists and nothing below has run.
`spec §N` cites `docs/spec/exsecutor-spec-v0.4.md`. Settled surface (spec §8.4
tokens, spec §8.5 control flow) is taken as given. Every other item is one
recommendation with its reason, for the owner to accept into the spec or
reject. Nothing here is written into `docs/spec/`.

## 1. The constraint and what it forces

The parser is hand-written recursive descent in freestanding x86-64 assembly
over an arena, producing a lossless, error-tolerant red-green CST (spec §9.1).

1. **Every statement and item is decided by its leading token.** The
   expression statement is the only non-keyword-led one; it is the fallback.
2. **The parser always knows whether it reads a type or an expression.**
   Types occur only after `:`, `->`, `sicut`, inside `<…>`, and after `&`/`*`
   in those positions. No position needs type-vs-expression guessing.
3. **`<` `>` delimit generic arguments and nothing else.** Comparison is
   spelled with words — `lt`, `eq` are attested in spec §8.5's settled
   examples. The `a<b>(c)` ambiguity is deleted, not resolved. The lexer must
   never form `<<`, `>>`, `<=`, `>=`.
4. **Expressions use precedence climbing** — a loop over a fixed table with
   one-token peek per step; LL(1) in practice, natural in assembly.
5. **Context flags replace lookahead.** Where `{` could open a struct literal
   or a block, the expression parser is entered with a no-struct-literal bit
   (`ExprNS`), Rust-style.
6. **Fixed peeks are at most two tokens**, enumerated in section 4.
7. **Simple statements end in `;`** (section 2.1) — not on the task list, but
   forced, and the thing that makes recovery cheap.
8. **Recovery is by small synchronisation sets**: `;` and `}` at statement
   level; item-leading keywords, `@`, `publica` at module level; `,` and the
   closing bracket in lists. No production consumes a `}` it did not open.
   Missing tokens become zero-width `MISSING` tokens, skipped tokens go into
   `ERROR` nodes, whitespace/comments are leading trivia of the next token
   (rowan style). Codepoints spec §8.1 rejects are emitted as error tokens,
   so the tree stays lossless.

## 2. Decisions

### 2.1 Statement termination (prerequisite)

`;` terminates every simple statement (`firma`, `mutabilis`, `sub` statement
form, `redde`, `rumpe`, `perge`, expression/assignment) and every module item
not ending in `}`. Constructs ending in `}` take none.

Reason: spec §8.4 makes layout insignificant, so without `;` a statement
beginning with `(` `[` `-` `*` `&` is swallowed by the previous expression —
`foo()` newline `*p = v` becomes `foo() * p = v`, diagnosed as assignment to
a non-lvalue, exactly the failure spec §16 names as the kill criterion. `;` is
also the one token that recovers an expression statement. The greedy
alternative (optional `;`) is LL(1)-clean and rejected on diagnostics alone.
Cost: spec §4.2/§5.1's illustrative blocks omit `;`; spec §4.5's
`sub alloc = a;` — the only deliberately written statement — has one.

Struct, interface and `externus` bodies keep spec §5.2/§10.1's separator-free
form: after a complete type an identifier cannot continue it, so the next
field is decidable. Recovery syncs on the two-token peek `IDENT :` or `}`.

### 2.2 Lambda

Adopt SYNTAX-PROPOSAL's form, `functio ( Param* ) [-> Type] Block` in
expression position, no `poscit` on the literal (spec §4.1 rule 5; the row
still appears in the lambda's *type*, spec §4.2), parameter types required.

LL(1) judgement: safe. `functio`'s three roles never share a position — item
position is a declaration, type position a function type, expression position
a lambda; `(x: f32)` vs `(f32)` and `{` vs `poscit {` are fixed by which
grammar is active. At *statement* start `functio` is an expression statement,
except the two-token peek `functio IDENT`, parsed as a nested declaration and
diagnosed: nested named functions are not admitted, fix-it
`firma nomen = functio(…) … ;`. No `=> e` form: it needs a token spec §8.4
lacks, and LL(1) does not need it.

### 2.3 `sub`

    'sub' CapAtom '=' ExprNS ( ';' | Block )

One binding per `sub` (spec §4.5). `;` after the expression selects the
statement form (scope: rest of the enclosing block), `{` the block form.
Reason: one production, one-token decision, identical prefix so errors in the
expression recover identically. `ExprNS` because `sub alloc = a { … }` would
otherwise read `a { … }` as a struct literal. Loop-scope interaction stays
`[OPEN]` per spec §8.5.

### 2.4 Trait implementation and the `dyn` cast

Implementation reuses `interfacies`:

    'interfacies' Path [GenericParams] 'in' Type [DeclRow] '{' FunctionDecl* '}'

`interfacies Scriptura { … }` declares; `interfacies Scriptura in Scriptor
poscit rete, archivum { … }` implements; one token after the name decides
(`{`, `<`, `in`). Reason: zero new reserved words (spec §8.4: each spends a
root forever), `in` is already reserved, and the ego line
`publica interfacies legibilis` extends to `interfacies legibilis in lector`.
If reviewers want a distinct word, `implet` (indicative, "fulfils" — it
describes, like `poscit`) takes the same grammar. Needs spec §3.9 review and a
spec §8.4 note. The probe's English `impl`/`for` are out.

Cast: `Expr 'sicut' Type`, postfix, level 3 in section 2.6 —
`x sicut dyn Scriptura poscit {rete}`, `n sicut u64` (spec §5.2's explicit
widening). Reason: `sicut` is reserved and already means "as"; after an
operand it is the cast, inside a row a row item — position-disjoint. Needs a
spec §3.9/§8.4 note; English `as` is inadmissible.

`dyn` type: `'dyn' Path [GenericArgs] [TypeRow]`, row **braced** like a
function type's, because `dyn` types sit in comma-separated parameter lists
where a bare row is ambiguous. Spec §4.4's `poscit P` is schematic.

### 2.5 Module-level declarations and state

`Item ::= Annotation* ['publica'] ItemBody`. `mutabilis` at module level
**parses** and is rejected by the checker as `EXS-E0500`/`EXS-E0501` (spec
§4.1 rule 7): a parse error cannot say "no module-level mutable state" nor
tell the capability-typed case apart, and both codes already exist. Module
`firma` is admitted as a constant (semantics: owner's call). Imports: none in
the spec; dotted paths parse as `Path`, so no import statement is invented.

### 2.6 Expression grammar

| level | operators | assoc |
|---|---|---|
| 1 postfix | `.f` `(…)` `[…]` `?` `<…>` and `{…}` struct literal (both only after a path segment; literal not in ExprNS) | left |
| 2 prefix | `-` `&` `*` `!` | — |
| 3 cast | `sicut Type` | left |
| 4 multiplicative | `*` `*%` `*\|` `/` | left |
| 5 additive | `+` `+%` `+\|` `-` `-%` `-\|` | left |
| 6 range | `..` | none |
| 7 comparison | `lt` `le` `gt` `ge` `eq` `ne` | none |
| 8 conjunction | `et` | left |
| 9 disjunction | `vel` | left |

Assignment `=` is a statement (`Expr '=' Expr ';'`, lvalue check semantic),
never an operator: it stays out of conditions and out of the table. Word
operators are contextual: an identifier in *operator* position is an
operator, identifiers otherwise occur only in operand position, so nothing
collides and nothing is reserved. Logical not cannot be a word for that
reason — prefix position *is* operand position, `non(x)` would be a call —
hence `!`. The `-`/`*` families follow spec §8.4's `+` triple. `/`,
remainder, shifts: `[OPEN]` (bare `%` collides visually with the wrapping
suffix; shifts must not be `<<`/`>>` tokens). `a < b` earns "`<` opens
generic arguments; comparison is `lt`" with the edit attached (spec §8.3).

### 2.7 Type grammar

EBNF in section 3. Prefixes `&` `*`; suffixes `:maior`/`:minor`/`:nativus`
and `apud machina`; `functio(Types) -> Type [TypeRow]`; `dyn`; parenthesised
type; `TypeArg ::= Type | INT` decided by peeking `INT` (`acies<f32, 1024>`;
a const-parameter name parses as a path, resolved semantically). A function
or `dyn` type consumes `poscit` only when the next token is `{` (two-token
peek), so a declaration's bare row after a function-typed return type needs
no parentheses. Nested function types bind a row innermost; parenthesise to
override.

### 2.8 Where `poscit` attaches

| position | form | ends at |
|---|---|---|
| function declaration / signature / interface method | bare `DeclRow` after `-> Type` (or `)`) | first non-`,` token |
| implementation head | bare | `{` |
| function type, `dyn` type | braced `TypeRow` | `}` |
| lambda literal | none | — |

`RowItem ::= CapAtom | 'sicut' IDENT` in both forms; `poscit {}` in a type is
an explicit empty row; a bare `poscit` with no item is an error. Generics:
`sicut f` names a *parameter*, so `functio ap<T>(f: T) -> R poscit sicut f`
needs no new syntax; spec §7.1's dictionary-layout question is semantic and
untouched by the grammar.

### 2.9 The `ego` file

Same lexer, same `Type`/`Signature` productions, separate entry point
`EgoFile`, selected by the driver (`exsc ego …`) and confirmed by the first
token `ego`. Reason: ego signatures must be parsed by the code that parses
source or the two drift; the ego-only parts are keyword-led key/value blocks,
trivially LL(1). Hashes and target names are the hazard (H12).

## 3. EBNF sketch

    Module        ::= Item* EOF
    Item          ::= Annotation* ['publica'] ItemBody
    Annotation    ::= '@' IDENT ['(' [Expr (',' Expr)*] ')']
    ItemBody      ::= FunctionDecl | StructDecl | TypeDecl | InterfaceDecl
                    | PotestasDecl | ExternusBlock | BindingStmt
    FunctionDecl  ::= Signature Block
    Signature     ::= 'functio' IDENT [GenericParams] ParamList ['->' Type] [DeclRow]
    ParamList     ::= '(' [IDENT ':' Type (',' IDENT ':' Type)*] ')'
    GenericParams ::= '<' IDENT [':' Type] (',' IDENT [':' Type])* '>'
    DeclRow       ::= 'poscit' RowItem (',' RowItem)*
    TypeRow       ::= 'poscit' '{' [RowItem (',' RowItem)*] '}'
    RowItem       ::= IDENT | 'sicut' IDENT
    StructDecl    ::= 'structura' IDENT [GenericParams] '{' (IDENT ':' Type)* '}'
    TypeDecl      ::= 'typus' IDENT [GenericParams] ['=' Type] ';'      (* [OPEN] sum types *)
    InterfaceDecl ::= 'interfacies' Path [GenericParams]
                      ( '{' (Signature [Block])* '}' | 'in' Type [DeclRow] '{' FunctionDecl* '}' )
    PotestasDecl  ::= 'potestas' IDENT '=' '{' [IDENT (',' IDENT)*] '}'
    ExternusBlock ::= 'externus' '(' STRING ',' 'abi' ':' IDENT ')' '{' (['publica'] Signature)* '}'
    BindingStmt   ::= ('firma' | 'mutabilis') IDENT [':' Type] ['=' Expr] ';'

    Block         ::= '{' Stmt* '}'
    Stmt          ::= BindingStmt | SubStmt | 'redde' [Expr] ';' | 'rumpe' ';' | 'perge' ';'
                    | IfStmt | WhileStmt | ForStmt | MatchStmt | Block | Expr ['=' Expr] ';'
    SubStmt       ::= 'sub' IDENT '=' ExprNS ( ';' | Block )
    IfStmt        ::= 'si' ExprNS Block ('sin' ExprNS Block)* ['aliter' Block]
    WhileStmt     ::= 'dum' ExprNS 'terminus' ExprNS Block
    ForStmt       ::= ('per' | 'quisque') IDENT 'in' ExprNS ('contrahe' IDENT ':' ArithOp)*
                      ['forma' IDENT] Block
    MatchStmt     ::= 'discerne' ExprNS '{' ('casus' Pattern Block)* ['aliter' Block] '}'
    Pattern       ::= Literal | Path ['(' [Pattern (',' Pattern)*] ')']         (* [OPEN] *)

    Expr          ::= Or              (* ExprNS: identical, struct-literal suffix disabled *)
    Or            ::= And ('vel' And)*
    And           ::= Cmp ('et' Cmp)*
    Cmp           ::= Range [('lt'|'le'|'gt'|'ge'|'eq'|'ne') Range]
    Range         ::= Add ['..' Add]
    Add           ::= Mul (('+'|'+%'|'+|'|'-'|'-%'|'-|') Mul)*
    Mul           ::= Cast (('*'|'*%'|'*|'|'/') Cast)*
    Cast          ::= Unary ('sicut' Type)*
    Unary         ::= ('-' | '&' | '*' | '!') Unary | Postfix
    Postfix       ::= Primary Suffix*
    Suffix        ::= '.' IDENT | '(' [Expr (',' Expr)*] ')' | '[' Expr ']' | '?'
                    | GenericArgs | '{' [IDENT ':' Expr (',' IDENT ':' Expr)*] '}'
                      (* last two only directly after IDENT / '.' IDENT / GenericArgs *)
    Primary       ::= Literal | '(' Expr ')' | 'functio' ParamList ['->' Type] Block | IDENT
    ArithOp       ::= '+' | '+%' | '+|' | '-' | '-%' | '-|' | '*' | '*%' | '*|' | '/'

    Type          ::= ('&' | '*')* CoreType (':' IDENT | 'apud' 'machina')*
    CoreType      ::= Path [GenericArgs] | 'dyn' Path [GenericArgs] [TypeRow] | '(' Type ')'
                    | 'functio' '(' [Type (',' Type)*] ')' '->' Type [TypeRow]
    GenericArgs   ::= '<' (Type | INT) (',' (Type | INT))* '>'
    Path          ::= IDENT ('.' IDENT)*

    EgoFile       ::= 'ego' Path '{' EgoEntry* '}' EOF
    EgoEntry      ::= 'versio' STRING | 'licentia' STRING
                    | 'fontes' '{' (Path HASH)* '}'
                    | ('hospites' | 'acceleratores' | 'exitus') '[' [Target (',' Target)*] ']'
                    | 'potestates' '{' [IDENT (',' IDENT)*] '}'
                    | 'numeri' '{' (IDENT IDENT)* '}'
                    | 'publica' ( Signature | 'typus' IDENT | 'structura' IDENT
                                | 'interfacies' Path ['in' Type] )
    Target        ::= IDENT ('-' IDENT)*

CST node kinds are the nonterminals above plus `ERROR` and `MISSING`.

## 4. Hazards — every ambiguity and every peek

- **H1 `<` vs less-than.** Comparison is `lt`/`gt`; `<` after a path segment
  is always `GenericArgs`. Lexer emits two `>` for `>>`, never `<<` `<=` `>=`.
- **H2 statement start `(` `[` `-` `*` `&`.** Mandatory `;` (section 2.1).
  `*p = v;` is then unambiguous: statement start is operand position.
- **H3 `IDENT {` literal vs block.** `ExprNS` in `si`/`sin`, `dum` and
  `terminus`, `per`/`quisque` range, `discerne`, `sub` right-hand side. The
  literal suffix applies only after a path segment, so `f() { … }` errors.
- **H4 `functio` × 3.** Position-disjoint (section 2.2); statement-level
  two-token peek `functio IDENT` gives the nested-declaration diagnostic.
- **H5 one-token decisions.** `sub … ;` vs `sub … {`; `redde ;` vs
  `redde Expr ;`; `interfacies X {` vs `<` vs `in`.
- **H6 `poscit` bare vs braced; dangling row.** Types take a row only on the
  two-token peek `poscit {`; declarations take a bare row, so `poscit {` in a
  declaration is "expected capability" and a body is never mistaken for a
  row. Nested function types bind innermost; parenthesise to override.
- **H7 `:` byte order vs annotation.** After a `CoreType` inside `Type` it is
  byte order; after `IDENT` in a parameter, field, binding or generic
  parameter it is an annotation. `x: u32:maior` reads left to right. Byte-order
  words are contextual (`maior` `minor` `nativus`).
- **H8 `sicut` × 2.** After an operand: cast. Inside a row: row item.
- **H9 word operators as identifiers.** Operator position is never operand
  position, so `lt`/`et`/… stay contextual; prefix not is `!` (section 2.6).
- **H10 `in` in impl heads vs loops.** Both keyword-led; no shared position.
- **H11 `..` vs `.` vs floats.** Lexer munches `..` before `.`; floats must
  require a digit after `.` so `1..n` lexes `INT .. IDENT`. Depends on the
  `[OPEN]` numeric literal grammar (spec §8.4).
- **H12 ego hashes and target names.** `x86_64-linux` is
  `Target ::= IDENT ('-' IDENT)*` at grammar level. `sha256-1a2b…` needs a
  `HASH` token — `1a2b…` is neither identifier nor literal under any settled
  rule. `[OPEN]`.
- **H13 patterns before `{`.** A struct pattern `Nomen { … }` would recreate
  H3 in `casus` position; not admitted. `aliter` is the catch-all arm, so no
  wildcard token is needed at arm level.
- **H14 body recovery.** No field separators; sync on the two-token peek
  `IDENT :` or `}`.
- **H15 `externus("C", abi: …)`.** A named argument, given its own
  production; general named arguments are `[OPEN]` and would need the same
  two-token peek.
- **H16 `forma` reserved vs `discerne forma {` (spec §8.5).** The settled
  example uses a reserved word as an identifier. `forma` could be contextual —
  it only follows a loop head, where an identifier cannot continue the
  expression — but this is a spec inconsistency for the owner.

## 5. Unresolved

- Numeric literal grammar (spec §8.4 `[OPEN]`): blocks H11 and `HASH`.
  Suggestion only: the lexer munches `sha256-` + hex as one token.
- Sum types and the pattern grammar: `discerne` exhaustiveness presupposes an
  enum the spec never declares; suggested home is `typus` (keyword-led,
  LL(1)-harmless), not decided here.
- Generic implementation heads: where the impl's type variables are bound
  (`interfacies Legibilis<T> in acies<T, N>` leaves `N` free).
- Operator set beyond spec §8.4: `/`, remainder, shifts, `!`, `et`, `vel`,
  `le ge gt ne`, the `-`/`*` overflow families. Needs spec §8.4; the words may
  need spec §3.9.
- Brand syntax `positio<'t>` (spec §5.1): `'` is not a token in spec §8.4. A
  leading-`'` `TypeArg` would be LL(1)-harmless.
- `si`/`discerne` as expressions and tail expressions: not admitted; adding
  them later is keyword-led and LL(1)-safe but changes block semantics.
- Open-ended ranges, array literals, tuples, slices, imports, general named
  arguments, compound assignment, labelled `rumpe`/`perge`, a `sub` list
  form, `;` after `}`-ending expression statements (kept mandatory).
- `EXS-E0201` as the generic syntax code is inferred from spec §5.2's "does
  not parse, so it is EXS-E0201". No new code is proposed; a distinct code for
  `;`-insertion fix-its would be a spec §13 amendment.

## 6. Spec amendments this design implies (owner's job, not done here)

1. State the `;` rule in spec §8.4; add `;` to the `redde`/`firma` lines in
   spec §4.2 and §5.1's examples.
2. Spec §8.4 tier 2: `lt le gt ge eq ne et vel`, `maior minor nativus`, `abi`.
   Operator table: `!`, `-% -| *% *|`, `/`, `sicut` as cast.
3. Spec §4.4: braced row on `dyn` types; record `interfacies … in …` and the
   `sicut` cast, both flagged for spec §3.9 review.
4. Spec §8.5: resolve `forma` reserved vs `discerne forma` (H16).
5. Error registry: no change required.
