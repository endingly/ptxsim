# Orchestration policy

## Reading scope

The coordinator reads ownership/routing, task decomposition, the model registry,
and task packets before delegation; parallel work adds the integration section.
Workers read their assigned role and the selected model's registry row. Runtime
selection details are in [dispatch controls](references/dispatch.md); consult them
when configuring or checking a dispatch, not for every repository question.
Core decisions/reviews use [sol.md](sol.md); Git writes/publication use the Git
workflow below only when authorized. These are task-specific references, not a
checklist to read every file. None of these reading rules waive the Authority
Sol gate.

## Ownership and routing

The primary agent coordinates requirements, task decomposition, integration, and
communication. Authority Sol (`gpt-6.1-sol` / `high`) owns architecture control
and core acceptance review, regardless of which model hosts the primary session.
A primary assigned the Authority Sol role and confirmed to run that pair may
perform those duties directly; any other primary routes them to a compliant
Authority Sol agent. Model/effort selection alone does not confer authority. The
[distinct-review rule](sol.md#core-acceptance) still applies. Coordination does
not confer authority to override an Authority Sol architectural decision or core
verdict. User instructions and system/developer rules remain superior.

Authority Sol settles module boundaries, public contracts, complex ISA/semantic
disputes, and conflicting core review findings. Implementation owners make
ordinary choices inside those boundaries and own diagnosis, edits, tests, and
repair. Implementation Sol (`gpt-6.1-sol` / `medium` by default) owns complete
cross-file or coupled delivery without architecture or core acceptance authority. Luna owns
simple, single-file implementation tasks as well as evidence, verification, and
authorized Git batches.
Astra is available for difficult investigations and second opinions, not as the
automatic owner of implementation, architecture, or acceptance.

Delegate useful complete tasks, not individual commands to satisfy a role label.
Prefer independent read-only work and disjoint implementation for substantial
changes. A primary may complete genuinely trivial work directly when handoff
cost exceeds its benefit. Lack of parallel work alone does not make Astra the
execution default. Follow actual delegation, tool, and permission constraints;
report a constraint rather than bypassing it. There is no agent-count quota,
model-percentage quota, or mandatory sequence for non-core tasks.

## Task decomposition

Separate evidence, implementation, verification, and decisions before broad work.
Use the least costly suitable supported model/effort pair permitted by the
registry for the bounded task; do not lower a fixed effort to optimize cost or
infer cost/suitability from a role label alone. The routing below is project
policy, not a claim that models have exclusive capabilities.

| Task shape | Normal owner | Scope and escalation |
| --- | --- | --- |
| Consumer mapping, bounded code/spec scans, issue triage, evidence summaries | Luna | Answer named questions with file/symbol evidence and a stopping condition. |
| Simple local fix, helper, test fixture, or documentation edit completed within one file | Luna | Own diagnosis, edit, focused checks, and repair for the full bounded task; retain Authority Sol review when core contracts are affected. |
| Cross-file code/test or documentation changes, coupled generator/IR work, ownership/state-machine debugging, ambiguous multi-step diagnosis | Implementation Sol | Own the whole implementation packet, including tests and repair; architecture changes go to Authority Sol. |
| Architecture proposal, core semantic dispute, core acceptance | Authority Sol | Follow [sol.md](sol.md); this responsibility is not replaceable by another model. |
| Hard unresolved investigation, adversarial second opinion, high-risk specialist implementation | Astra when justified | Return evidence/proposals within assigned scope; Authority Sol decides architecture and core acceptance. |
| Verification batches, artifact inspection, authorized Git/PR preparation | Luna | Read-only unless edits or Git writes/publication are expressly authorized. |

Route implementation by task completeness, edit scope, uncertainty, coupling,
contract risk, and verifiability. Luna's single-file limit applies to writing an
implementation, not read-only evidence, cross-file verification, or authorized
Git batches. If code and tests, coupled policy documents, or source and
generated artifacts must change together, assign the whole delivery to
Implementation Sol; do not split routine edits or repairs merely to fit Luna's
limit. A one-file semantic change can still require Authority Sol review.
Ordinary failures stay with the implementation owner. Escalate a specific
missing decision with evidence and attempted approaches, then return execution
to the owner. Architecture and unresolved core semantics go to Authority Sol,
not whichever model is primary. Do not enlarge a bounded scan into an unbounded
audit or duplicate a worker's useful investigation merely because the primary
knows the repository.

## Model preferences

This table is the sole project model registry. The listed models may be requested
as subagents when the runtime supports them. Role documents define behavior,
not separate model defaults. Retired model generations are not eligible as hidden
fallbacks, compatibility aliases, or inherited worker defaults.

| Role | Preferred model | Reasoning effort | Instructions |
| --- | --- | --- | --- |
| Architecture control and core acceptance | `gpt-6.1-sol` | `high` only (fixed) | [Sol](sol.md) |
| Cross-file implementation and deep debugging | `gpt-6.1-sol` | `medium` by default; a justified `high` setting is an exception | [Implementation](implementation.md) |
| Simple single-file implementation, evidence, verification, Git | `gpt-6-luna` | `max` only (fixed) | [Luna](luna.md) |
| Optional specialist, second opinion, difficult implementation | `gpt-6-astra` | `low` by default; a justified higher setting is an exception | This policy and [implementation.md](implementation.md) when editing |

Terra and Spark are retired from model routing; its complete-delivery responsibilities now
live in [implementation.md](implementation.md) and are assigned by the task table.
Do not manufacture a next-generation Terra or Spark identifier. The subagent allowlist is
exhaustive: only the three distinct models serving the four roles above may be
selected, including for narrow tasks and availability fallbacks. Luna owns simple single-file
implementation tasks; no retired model may be reintroduced through a role alias
or implicit default.

### Reasoning-effort constraints

Treat the selected model, reasoning effort, and assigned role as one dispatch
contract. Authority Sol is fixed at `high`. Luna is fixed at `max` even for
simple scans, Git work, and mechanical edits. These fixed settings are exact,
not ceilings or recommendations; do not alter them for routine work or disputes.
Implementation Sol defaults to `medium`. A specific hard diagnosis or complex
state problem may justify `high`, with the task-based reason recorded in both
the packet and dispatch record before dispatch. An ordinary failing test does
not justify this upgrade. The exception changes effort only: Implementation
Sol remains an implementation owner without architecture or core acceptance
authority, even though its model/effort pair then matches Authority Sol's.

Task complexity changes scope, decomposition, or the model assignment; it does
not change a fixed effort. Retries, resume operations, and model substitutions
follow the same registry and require the same exception evidence where applicable;
a new model uses its own effort rather than the previous worker's.

Astra normally uses `low`. A higher supported effort needs a specific task-based
reason recorded in the packet and dispatch record before use. Being a specialist,
handling a fallback, or encountering an ordinary failing test is not by itself
that reason. An Astra exception does not change the fixed settings of Authority
Sol or Luna, the Implementation Sol default/exception rules, or role authority.
Neither an effort exception nor selecting `gpt-6.1-sol` / `high` transfers
architecture control or core acceptance to an implementation worker.

Before dispatch or resume, use [dispatch controls](references/dispatch.md) for
supported parameters, context inheritance, and selection evidence. Reuse a valid
selection record rather than making every worker rediscover the same tool schema.
A required pair that cannot be selected is unavailable; use the rules below.

## Availability and fallback

For execution, use a supported listed model/effort pair appropriate to the task.
Work beyond Luna's simple single-file implementation scope goes to Implementation
Sol. The execution fallback order is Luna / `max`, then Implementation Sol /
`medium`, then Astra / `low`. If `gpt-6-luna` / `max` is unavailable, its
execution may go to Implementation Sol (`gpt-6.1-sol` / `medium`). If the
required Implementation Sol pair is unavailable, its execution may go to
Astra / `low` with the fallback reason recorded. An implementation `high`
exception or higher Astra effort still requires its own task-based reason;
unavailability alone does not justify an effort change. If a required effort
is unavailable, do not use the same model at a different effort as a workaround.
A substitution never transfers architecture control or core acceptance away
from Authority Sol. Astra is not the silent execution default. Resume interrupted
work at a compliant pair or reassign its remaining bounded scope rather than
automatically absorbing it into the primary. Report unavailable pairs, chosen
substitutions, and any remaining capability or verification gaps.

Authority Sol has no cross-model or cross-effort fallback. If `gpt-6.1-sol` /
`high` cannot be used under the actual tools and permissions, continue
authorized evidence work, non-core delivery, and implementation inside already
approved contracts where feasible. Proposals/prototypes may be prepared as
unapproved work. Leave new architecture decisions and core acceptance pending
Authority Sol review; do not claim core completion, approve/merge affected work,
or close it as accepted. Another model's review can supply evidence but cannot
satisfy that gate. Gate blockage does not require stopping independent safe work
or repeatedly asking the user.

## Architecture control and core acceptance

Authority Sol owns architecture decisions and core acceptance, independently of
the primary. Use [the core-review skill](skills/ptx-core-review/SKILL.md) for
that workflow and [sol.md](sol.md#core-boundary) for the core boundary and review
contract. Ordinary choices inside approved contracts need no new design handoff.
A non-core task may close after proportionate verification; core work requires
actual-diff review by
Authority Sol. A skill selection or passing CI cannot replace it.

## Task packets and scope

Before delegation, read the selected worker instructions for the assigned role.
Reuse packet fields and valid evidence already supplied; request only a missing
field that blocks the assigned work. Provide the objective,
acceptance criteria, relevant files/contracts and decisions, exact editable and
shared-state scope, core/non-core classification, owner, requested model/effort,
required validation, search boundary/stopping condition, and concise return format.
Take effort from the registry; include a specific reason for any Implementation
Sol or Astra effort exception, recorded before dispatch.
Specify whether edits, Git operations, or external publication are authorized.
A task packet is not permission to widen scope or weaken the Authority Sol gate.

Workers resolve routine implementation choices within agreed contracts. Route
scope problems through the primary; route architectural/core judgment to
Authority Sol.
State the decision needed, evidence, attempted approaches, and available options.
A coordinator relays the decision and returns execution to the owner where
feasible; escalation is not automatically a question for the user.

## Parallel work, verification, and integration

- Assign one writer per file or interacting subsystem at a time. Use disjoint
  ownership or isolated worktrees for concurrent implementation; separate
  worktrees do not eliminate semantic conflicts in shared contracts.
- Serialize builds/tests that share mutable outputs. Reuse valid evidence tied
  to the same inputs and configuration; do not repeat a full suite just because
  work moved between agents. Run focused integration checks when inputs changed.
- Avoid duplicating worker investigation or edits. Review concise evidence and
  key diffs; investigate gaps, conflicting results, or correctness concerns.
- The implementation owner repairs failures caused by its change and reruns the
  affected checks. Independent verification is proportionate to risk and does
  not replace Authority Sol core review. Report commands, outcomes,
  skipped/unverified behavior, and relevant repository state without dumping full
  logs.
- The primary coordinates integration and preserves Authority Sol's reviewed boundaries.
  A core verdict applies to the reviewed result, not an unreviewed merged variant.

## Git workflow

When authorized, prefer one bounded Luna task for status/diff inspection,
staging, commit, and any separately authorized publication. Do not delegate
individual commands for their own sake. The primary may do genuinely trivial
Git work or handle a documented tool/availability constraint. An implementation
assignment alone does not authorize staging, commit, or push.

The Git owner checks staged/unstaged status and the intended diff, preserves
unrelated/untracked work, reuses valid verification evidence, stages only the
authorized files, inspects the staged diff and hook effects, and reports the
commit hash and final status. Never amend, discard changes, or rewrite history
without authorization. A successful commit is not core acceptance.

Publication requires explicit user authorization; a request to commit alone
does not authorize push. Existing authorization applies to the same unfinished
task, not a new task. An authorized draft/WIP publication may carry a pending
Authority Sol review, but must say so; approval/merge/core completion requires
the Authority Sol gate.
Neither a technical verdict nor Git ownership grants publication permission.

## Policy provenance

This policy adapts the [ptx_frontend agent rules at revision
5a8d1b0](https://github.com/endingly/ptx_frontend/tree/5a8d1b0f03acb311248d98583a21bf6e2fe78775/.agents)
to ptxsim contracts. The registry above is the local routing authority.
