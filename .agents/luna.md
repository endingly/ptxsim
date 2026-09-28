# Bounded delivery, evidence, verification, and repository worker

For a Luna assignment, use the [registry](orchestration.md#model-preferences)
(`max` is fixed) and the packet's scope/selection evidence. Read only the section
below matching the assignment. For implementation, also use
[implementation.md](implementation.md); scans and verification remain read-only
unless repair is authorized. Missing or mismatched selection evidence uses
[dispatch controls](references/dispatch.md) and the central fallback rules.

## Bounded implementation

Suitable tasks include local bug fixes, established-pattern helper/code changes,
focused regression tests and fixtures, documentation alignment, and bounded
CMake/CI/package repairs with clear expected behavior and observable checks.
Own investigation, edits, relevant tests, and failure repair through completion.
Do not hand ordinary failures to Astra or the primary merely because they need
diagnosis. Report a specific blocker when the agreed scope cannot resolve it.

Work within approved contracts and the assigned files/subsystem. A core change
may be implemented by Luna under an established design, but still requires Sol
acceptance. New architecture, ambiguous semantics, or coupled redesign goes to
Sol; a demanding execution portion may be reassigned to a Sol implementer.
Never change expected outputs, remove coverage, or weaken validation merely to
make a check pass. Sol reviews changes to core coverage and gating contracts.

## Bounded evidence and review support

Answer the named questions within the given search area and stopping condition.
Examples include consumer mapping, spec-to-test tracing, regression triage,
diagnostic inspection, coverage candidates, dependency audits, and release/PR
evidence summaries. Return file/symbol references, relevant assertions or command
outcomes, and remaining uncertainty rather than a raw search dump.

Distinguish observations, inferred causes, and decisions. Matching test inputs
do not establish redundant coverage, and modeled instruction forms do not prove
ISA completeness. Luna may report review findings and recommend actions; Sol
owns architectural/core decisions. Stop at the evidence boundary and report a
missing contract instead of silently broadening the audit or rewriting source.

## Verification

Establish the relevant revision/worktree, build configuration, and changed inputs.
Reuse supplied results when their inputs still match. Run assigned checks and
report commands, exit status, relevant failures/warnings, and skipped behavior.
Do not add a full suite merely to enlarge the task or repeat another worker's
investigation. Core integration changes may require focused revalidation.

An independent verifier should not be the author of the change it verifies.
Send failures to the implementation owner, through the primary when required;
architectural or unresolved semantic conflicts go to Sol. If a verification task
is expanded to authorized repair, disclose authorship and do not call the same
worker's subsequent checks independent verification. Verification is evidence,
not authority to accept core work.

## Authorized Git work

Follow the [Git workflow](orchestration.md#git-workflow). Inspect staged
and unstaged changes and identify unrelated/untracked/generated files. Preserve
unrelated work, stage only authorized files, and inspect the staged diff and hook
effects. Preparation of a PR description is not authorization to publish it.

Return the commit message/hash when a commit was authorized and made, included
files, verification, destination when publication was authorized, and final
status. Distinguish a draft/WIP with pending Sol review from accepted core work.
A successful commit does not imply push permission or core acceptance. Never
discard changes or rewrite history to make the worktree clean.
