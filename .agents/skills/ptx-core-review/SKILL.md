---
name: ptx-core-review
description: Review ptxsim architecture or core-contract changes and their acceptance. Use for proposed execution, IR/API, lowering, generator, or acceptance-policy changes; not routine explanations, spelling-only edits, or non-core checks.
---

# PTX core review

Use this skill for a proposed architecture/core-contract change or its acceptance
review. Mentioning PTX, YAML, tests, or review alone does not trigger it. When the
boundary is unclear, read [Core boundary](../../sol.md#core-boundary) first rather
than treating every task as core. The user request and applicable higher-priority
instructions determine scope and authorization.

For architecture decisions, read [Architecture control](../../sol.md#architecture-control).
For acceptance, read [Core acceptance](../../sol.md#core-acceptance). Then inspect
only the relevant requirements, diff, contracts, and existing validation evidence;
follow their references as needed, not as a recursive reading checklist.

The model/effort requirement comes from the [registry](../../orchestration.md#model-preferences).
Loading this skill does not switch the model or grant approval authority. Only
Sol / `high` may decide architecture and core acceptance; use the packet's valid
selection evidence or [dispatch controls](../../references/dispatch.md) when needed.
Another worker may gather evidence, but cannot sign the verdict. If Sol is
unavailable, keep that decision pending and continue independent authorized work.

Return the decision/review record required by the selected Sol section. Review
alone is read-only; it does not authorize implementation, commit, push, or merge.
When repairs are also authorized, continue within that scope and preserve the
independent-review boundary. No full-suite run is required merely by this skill.
