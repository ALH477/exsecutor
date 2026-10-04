Exsecutor Authority-Composition Experiment

You are working in the Exsecutor repository.

Repository thesis:

Locale and target are capabilities, never ambient state.

The deeper claim under test is:

Environmental authority can behave like an ordinary interface property through abstraction and separate compilation: implementation may be hidden, but authority requirements may not be.

Your task is to design and execute the smallest serious experiment that can falsify or support that claim.

Do not treat this as a general feature-development task. Do not broaden the language unnecessarily. Build only the minimum missing infrastructure required to run the experiment.

1. First, inspect before changing anything

Read the repository's README, specification, compiler sources, existing tests, publish-gate scripts, interface/ego machinery, capability-row implementation, generic implementation, closure handling, and syscall audit.

Identify:

what is already executable;

what is specified but unimplemented;

what is implemented only by a probe;

what prevents independently compiled libraries from being tested;

what currently defines interface identity;

how capability rows enter and propagate;

how closures represent captured authority;

how generic rows are represented;

how dictionary layout interacts with generic capability rows;

how generated binaries are audited.

Do not assume that the specification and compiler agree. Record discrepancies.

Before modifying code, write a short experiment design in the repository or in your final report.

2. Define the experiment

Construct a deliberately small five-layer dependency graph:

Application
    ↓
Framework
    ↓
Library
    ↓
Generic helper / utility
    ↓
Capability provider


The graph must exercise all of these authority paths where the language permits:

capability received as a parameter;

capability held in a field;

capability captured by a closure;

capability propagated through a generic abstraction / row variable.

The exact names and syntax should follow the repository's existing language conventions.

Use at least two distinct authority atoms where useful, e.g. a clock-like capability and a network/filesystem-like capability.

The important thing is not the specific capabilities. The important thing is that authority crosses several abstraction boundaries.

3. Establish a baseline

Compile the entire dependency graph using the normal reference compiler/toolchain.

Record:

source inputs;

declared capability rows at every public boundary;

interface/ego identities;

dependency identities;

generated artifacts;

compiler diagnostics;

runtime behavior;

syscall closure of the resulting binary;

declared authority envelope;

syscall-envelope audit result.

Preserve these artifacts so the experiment is reproducible.

4. Experiment A — implementation-only change

Change the implementation of a library while preserving its declared interface and capability requirements.

Examples:

replace one internal helper with another;

add/remove internal computation;

change internal control flow;

change implementation details behind a capability-bearing value.

Do not change:

exported types;

declared capability rows;

generic interface;

numeric policy;

dependencies visible in the interface.

Then rebuild.

Verify mechanically:

same interface identity
same authority interface
no unnecessary dependent rebuild


If artifacts are expected to remain byte-identical under the repository's reproducibility model, test that too. If byte identity is not promised at this layer, distinguish that from interface identity and dependent recompilation.

The key question:

Can implementation change without authority-interface change?

5. Experiment B — authority expansion

Now modify a library implementation so that it genuinely requires one additional capability atom.

For example:

before: {clock}
after:  {clock, rete}


Do not manually modify callers to hide the change.

Verify:

the library's public authority interface changes;

its interface/ego identity changes;

affected dependents are recognized as stale;

a caller that does not possess/declare the new authority cannot silently continue;

the new authority is visible at the appropriate abstraction boundary;

unrelated dependents are not unnecessarily invalidated if the dependency graph allows finer-grained identity.

The expected result is not merely "the compiler errors."

The expected result is:

authority expansion is observable in exactly the same sense as another interface/ABI change.

6. Experiment C — closure capture

Create a closure whose implementation captures a capability.

Verify that the capability row of the function-typed value exposes the captured authority.

Then attempt to construct the counterexample that previously motivated the closure rule:

closure captures capability
but function type omits capability


The compiler must reject it.

If it accepts it, treat that as a failed experiment, not something to paper over with a test adjustment.

Also test a closure whose implementation changes while its captured authority does not. Determine whether its interface identity remains stable.

7. Experiment D — generic propagation

Create a generic abstraction whose behavior requires an authority row.

The row must cross the generic boundary while remaining visible to the caller.

Test at least:

generic definition
    ↓
instantiation
    ↓
library interface
    ↓
application


Verify that specialization/dictionary machinery does not create an authority channel absent from the type/interface.

Specifically try to produce:

capability exists at runtime but disappears from the static interface because it was hidden in a generic parameter or dictionary.

That must fail.

If row polymorphism on type parameters is currently specified but semantically inert, implement only the minimum semantics needed for this experiment. Do not invent a larger generic system.

8. Experiment E — separate library artifacts

If independently compiled library artifacts are missing, implement the smallest artifact format/driver path required to perform the experiment.

The artifact must expose enough information for a downstream compilation to know:

public interface;

capability rows;

generic requirements;

dependency identities;

interface identity/hash;

any other information necessary to reject stale or under-authorized consumers.

The implementation body should not need to be present for a consumer to type-check against the interface.

Do not use the experiment as justification for exposing implementation details that the interface is supposed to hide.

9. Experiment F — syscall witness

For every meaningful final program produced by the experiment:

compute the binary's syscall closure;

compute the authority envelope implied by its declared capabilities;

verify:

syscall_closure(binary)
    ⊆
declared_authority_envelope


Do not attempt to turn this into exact reachability analysis.

Preserve the repository's existing over-approximation rules, including its treatment of broad/unsafe authority such as raw-pointer escape hatches.

The syscall audit is a witness that compilation did not escape the declared authority envelope. It is not the proof of interface provenance.

10. Test independent authorship

Where practical, arrange the experiment so that the layers behave like independently authored libraries.

At minimum, compile lower layers separately, publish their interfaces, and consume those interfaces without relying on their source bodies.

The point is to prevent the experiment from accidentally succeeding because the compiler has whole-program knowledge.

11. Measure the actual composition cost

Do not merely report pass/fail.

Record:

number of public capability atoms introduced at each layer;

number of capability atoms propagated through each boundary;

how many signatures changed after an implementation-only edit;

how many signatures changed after an authority change;

number of dependent rebuilds;

whether generic rows become visible/verbose at call sites;

whether closure rows remain manageable;

whether implementation details leak into public interfaces;

whether dictionary/generic machinery changes interface identity unnecessarily.

The key question is:

Does explicit authority remain cheaper than the guarantee it provides?

Do not answer this subjectively. Use the experiment's concrete changes.

12. Preserve the distinction between three invariants

Do not collapse these into one test.

Interface invariant

The public interface declares the authority required by the implementation.

Conceptually:

authority(implementation)
    ⊆
authority(permitted_by_interface)

Identity invariant

Implementation changes that preserve the interface do not change interface identity.

Conversely, authority-interface changes do change interface identity.

Binary invariant

The generated artifact does not exceed the authority envelope:

syscalls(binary)
    ⊆
authority(declared_program)


These are related but distinct claims.

13. Treat failures as research results

If the experiment fails, do not immediately weaken the test.

Classify the failure:

hidden authority;

unsound closure capture;

generic row erasure;

dictionary leakage;

interface-hash instability;

excessive propagation;

inability to separately compile;

unnecessary rebuild;

syscall audit mismatch;

implementation detail leaking into interface;

other.

Then determine whether the failure is:

an implementation bug;

an underspecified language rule;

an intentionally accepted limitation;

evidence against the broader thesis.

Do not silently choose category 1 merely because it is convenient.

14. Regression-test the result

Every invariant demonstrated by the experiment must become an automated regression test.

At minimum, preserve tests for:

implementation change with unchanged authority interface;

authority expansion changing interface identity;

unauthorized caller rejection;

closure-capture visibility;

generic row visibility;

separately compiled library consumption;

stale interface detection;

syscall-envelope inclusion.

The tests should fail if someone later reintroduces ambient authority through closures, generics, dictionaries, library artifacts, or compiler shortcuts.

15. Final report

Produce a concise report with these sections:

Hypothesis

State the exact authority/interface claim tested.

Existing support

What the repository already demonstrated before your changes.

Experimental additions

Only the infrastructure you had to add to make the experiment executable.

Results

For each experiment A–F:

expected behavior;

observed behavior;

pass/fail;

relevant artifact/interface identities.

Composition cost

Give concrete measurements from the five-layer graph.

Failures and limitations

Be explicit about anything that remains unproven.

Verdict

Choose one:

Supported: the experiment provides evidence that authority behaves compositionally as an interface property.

Partially supported: the core invariant works, but one or more composition boundaries remain unproven or costly.

Falsified: the experiment demonstrates a counterexample to the claimed model.

Do not call the thesis "proven." This is an empirical compiler experiment, not a formal proof.

Constraints

Prefer the smallest implementation that makes the experiment executable.

Do not redesign the language merely to make the experiment pass.

Do not weaken existing checks to accommodate new infrastructure.

Do not hide capability propagation in compiler magic that is absent from the interface model.

Do not treat syscall inclusion as exact reachability.

Do not claim separate compilation if the consumer still has access to the implementation body.

Do not declare success merely because the final program runs.

Keep all existing publish-gate tests passing.

Add regression tests for every newly demonstrated invariant.

Document every place where the implementation is weaker than the specification.

The ultimate question is:

When authority crosses several independently compiled abstraction boundaries, does it remain as mechanically visible and separately identifiable as an ordinary type/interface property?

That is the experiment. Everything else is infrastructure in service of answering it.
