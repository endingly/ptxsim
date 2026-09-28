# Orchestration policy

## Reading scope

The coordinator reads ownership/routing, task decomposition, the model registry,
and task packets before delegation; parallel work adds the integration section.
Workers read their assigned role and the selected model's registry row. Runtime
selection details are in [dispatch controls](references/dispatch.md); consult them
when configuring or checking a dispatch, not for every repository question.
Core decisions/reviews use [sol.md](sol.md); Git writes/publication use the Git
workflow below only when authorized. These are task-specific references, not a
checklist to read every file. None of these reading rules waive the Sol gate.

## Ownership and routing

The primary agent coordinates requirements, task decomposition, integration, and
communication. Sol owns architecture control and core acceptance review,
regardless of which model hosts the primary session. A primary confirmed to run
Sol at the required effort may perform those duties directly; any other primary
routes them to a compliant Sol agent. The [distinct-review rule](sol.md#core-acceptance) still applies.
Coordination does not confer authority to override a Sol architectural decision
or core verdict. User instructions and system/developer rules remain superior.

Sol settles module boundaries, public contracts, complex ISA/semantic disputes,
and conflicting core review findings. Implementation owners make ordinary
choices inside those boundaries and own diagnosis, edits, tests, and repair.
Luna owns clear, bounded delivery as well as evidence, verification, and authorized
Git batches. Sol implementation workers own demanding or coupled delivery.
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
| Local bug fix, settled-pattern code/test additions, documentation, bounded CI/CMake/packaging repair | Luna | Own diagnosis through focused tests and repair; retain Sol review when core contracts are affected. |
| Coupled changes, generator/IR work, ownership/state-machine debugging, ambiguous multi-step diagnosis | Sol implementation worker | Use [implementation.md](implementation.md); architecture changes go to the Sol authority. |
| Small isolated helper, test fixture, mechanical migration, localized fix | Luna | The selected worker owns the complete packet, not merely edits that another worker must finish. |
| Architecture proposal, core semantic dispute, core acceptance | Sol authority/reviewer | Follow [sol.md](sol.md); this responsibility is not replaceable by another model. |
| Hard unresolved investigation, adversarial second opinion, high-risk specialist implementation | Astra when justified | Return evidence/proposals within assigned scope; Sol decides architecture and core acceptance. |
| Verification batches, artifact inspection, authorized Git/PR preparation | Luna | Read-only unless edits or Git writes/publication are expressly authorized. |

Route by uncertainty, coupling, contract risk, and verifiability, not file count.
A small semantic change can require Sol review; a larger mechanical migration
can remain a bounded Luna task under an established contract. Ordinary
failures stay with the implementation owner. Escalate a specific missing decision
with evidence and attempted approaches, then return execution to the owner.
Architecture and unresolved core semantics go to Sol, not whichever model is
primary. Do not enlarge a bounded scan into an unbounded audit or duplicate a
worker's useful investigation merely because the primary knows the repository.

## Model preferences

This table is the sole project model registry. The listed models may be requested
as subagents when the runtime supports them. Role documents define behavior,
not separate model defaults. Retired model generations are not eligible as hidden
fallbacks, compatibility aliases, or inherited worker defaults.

| Role | Preferred model | Reasoning effort | Instructions |
| --- | --- | --- | --- |
| Architecture control and core acceptance | `gpt-6-sol` | `high` only (fixed) | [Sol](sol.md) |
| Demanding implementation and deep debugging | `gpt-6-sol` | `high` only (fixed) | [Implementation](implementation.md) |
| Bounded delivery, evidence, verification, Git | `gpt-6-luna` | `max` only (fixed) | [Luna](luna.md) |
| Optional specialist, second opinion, difficult implementation | `gpt-6-astra` | `low` by default; a justified higher setting is an exception | This policy and [implementation.md](implementation.md) when editing |

Terra is retired from model routing; its complete-delivery responsibilities now
live in [implementation.md](implementation.md) and are assigned by the task table.
Do not manufacture a next-generation Terra identifier. The subagent allowlist is
exhaustive: only the three models named above may be selected, including for
narrow tasks and availability fallbacks. Luna owns narrow implementation tasks;
no retired model may be reintroduced through a role alias or implicit default.

### Reasoning-effort constraints

Treat the selected model and reasoning effort as one dispatch contract. Sol is
fixed at `high` in every role, including implementation, architecture, review,
verification, and execution fallbacks. Luna is fixed at `max` even for simple
scans, Git work, and mechanical edits. These are exact settings, not ceilings or
recommendations: do not lower them for routine work or raise Sol for a dispute.
Task complexity changes scope, decomposition, or the model assignment, not a
fixed effort. Retries, resume operations, and model substitutions follow the
same registry; a new model uses its own effort rather than the previous worker's.

Astra normally uses `low`. A higher supported effort needs a specific task-based
reason recorded in the packet and dispatch record before use. Being a specialist,
handling a fallback, or encountering an ordinary failing test is not by itself
that reason. An Astra exception does not change Sol/Luna's fixed settings or
transfer architecture control or core acceptance away from Sol.

Before dispatch or resume, use [dispatch controls](references/dispatch.md) for
supported parameters, context inheritance, and selection evidence. Reuse a valid
selection record rather than making every worker rediscover the same tool schema.
A required pair that cannot be selected is unavailable; use the rules below.

## Availability and fallback

For execution, use a supported listed model/effort pair appropriate to the task.
Demanding Luna work goes to a Sol implementation worker. If Luna / `max` is
unavailable, execution may go to Sol / `high`; Astra / `low` may take a documented
specialist or availability fallback when justified. A higher Astra effort still
needs the exception reason above. If a required effort is unavailable, do not
use the same model at a different effort as a workaround. A substitution never
transfers architecture control or core acceptance away from Sol. Astra is not
the silent execution default. Resume interrupted work at a compliant pair or
reassign its remaining bounded scope rather than automatically absorbing it into
the primary. Report unavailable pairs, chosen substitutions, and any remaining
capability or verification gaps.

The Sol authority has no cross-model or cross-effort fallback. If Sol / `high`
cannot be used under the actual tools and permissions, continue authorized
evidence work, non-core delivery, and implementation inside already approved contracts where
feasible. Proposals/prototypes may be prepared as unapproved work. Leave new
architecture decisions and core acceptance pending Sol review; do not claim core
completion, approve/merge affected work, or close it as accepted. Another model's
review can supply evidence but cannot satisfy that gate. Gate blockage does not
require stopping independent safe work or repeatedly asking the user.

## Architecture control and core acceptance

Sol owns architecture decisions and core acceptance, independently of the primary.
Use [the core-review skill](skills/ptx-core-review/SKILL.md) for that workflow and
[sol.md](sol.md#core-boundary) for the core boundary and review contract. Ordinary
choices inside approved contracts need no new design handoff. A non-core task may
close after proportionate verification; core work requires the actual-diff Sol
review. A skill selection or passing CI cannot replace it.

## Task packets and scope

Before delegation, read the selected worker instructions for the assigned role.
Reuse packet fields and valid evidence already supplied; request only a missing
field that blocks the assigned work. Provide the objective,
acceptance criteria, relevant files/contracts and decisions, exact editable and
shared-state scope, core/non-core classification, owner, requested model/effort,
required validation, search boundary/stopping condition, and concise return format.
Take effort from the registry; include a specific reason for any Astra exception.
Specify whether edits, Git operations, or external publication are authorized.
A task packet is not permission to widen scope or weaken the Sol gate.

Workers resolve routine implementation choices within agreed contracts. Route
scope problems through the primary; route architectural/core judgment to Sol.
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
  not replace Sol core review. Report commands, outcomes, skipped/unverified
  behavior, and relevant repository state without dumping full logs.
- The primary coordinates integration and preserves Sol's reviewed boundaries.
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
Sol review, but must say so; approval/merge/core completion requires the Sol gate.
Neither a technical verdict nor Git ownership grants publication permission.
