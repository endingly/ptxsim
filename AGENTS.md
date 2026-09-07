# Codex agent policy

The primary agent owns the requested outcome, architecture decisions, final
review, and communication. It may implement and verify work directly.

## Working rules

- Read [.agents/orchestration.md](.agents/orchestration.md) before substantial
  work. It is the single authority for project routing and model preferences.
- Follow the user's current request and applicable system/developer rules.
  Repository policies and skills do not expand task authorization.
- Continue already-authorized work through implementation and verification.
  Resolve routine choices from repository evidence; ask only when missing
  information materially changes scope, correctness, or an irreversible action.
- An audit or explanation request calls for findings, not automatic edits.
  Implement when the user requests or approves changes.
- Preserve unrelated work and the user's staging choices. Stage task files
  only when a commit is authorized; include all changes only when requested.
- Commit and push are separate actions. Push requires an explicit request;
  do not amend, rewrite history, or discard changes without authorization.
- Use existing components before adding abstractions. Keep changes scoped to
  the requested behavior.

## Code documentation and templates

- Add Doxygen comments to every newly introduced C++ function, class, and struct.
- Document important variables and data members: meaning, ownership/lifetime,
  units, and invariants where relevant. Use docstrings for Python APIs.
- Explain useful contracts rather than restating identifiers.
- Source comments must not mention milestone or work-package identifiers.
- Constrain template parameters with meaningful concepts/requires clauses;
  prefer overloads when the supported type set is small and fixed.

## Documentation authority

- [.agents/arch/](.agents/arch/) contains adopted architecture decisions.
- [.agents/milestone_plan/](.agents/milestone_plan/) contains active plans and
  implementation status; read only those relevant to the task.
- [.agents/project_plan.md](.agents/project_plan.md) and archived review bodies
  are historical evidence, not current work instructions.
- Read [.agents/review_policy.md](.agents/review_policy.md) before creating,
  importing, renaming, or superseding a review artifact.

## Verification and reporting

- Run checks appropriate to the changed behavior and required project gates.
  Once they pass, expand or repeat them only for new changes, failures, or
  unresolved concerns. Documentation-only work normally needs diff/link checks.
- Report what changed, verification performed, and material limitations.
  Distinguish an observation from an inference and unrun checks from passing ones.
- Prefer concise, connected prose; use lists for genuinely parallel information.
  Do not narrate every command or repeat the plan in each update.
