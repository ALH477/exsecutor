# Architecture Decision Records

An ADR here is a short, dated record of one binding decision and the reasons for
it — not a design spec and not a how-to. Files are numbered sequentially and
immutably (`0001-`, `0002-`, …) followed by a kebab-case slug of the decision;
the number is never reused, renumbered, or reassigned, even once the decision
it names is out of date. When a decision changes, write a **new** ADR that
supersedes the old one — mark the old file's Status as `Superseded by 000N` and
the new file's as `Supersedes 000M` — rather than editing the original's
Decision or Consequences in place. The point of the record is to show what was
believed and why at the time it was written; editing it after the fact to stay
current would destroy the thing it exists to preserve.
