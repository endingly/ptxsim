# Orchestration policy

## Ownership and routing

The primary agent owns requirements, architecture, task decomposition, final
integration, acceptance, and communication. Model preferences are centralized
below; worker documents define role-specific behavior, not competing defaults.

Ownership of the outcome does not mean personally executing every step. Terra
owns routine issue delivery from investigation through implementation, tests,
and failure repair. Luna owns bounded evidence collection, verification batches,
and authorized Git work. Sol provides risk-based independent review. The primary
focuses on architecture, complex semantic disputes, conflicting review findings,
final acceptance, and communication rather than routine execution.

The primary may complete genuinely trivial changes directly when a task packet
would cost more than the work, or handle a specific unresolved design concern.
Tightly coupled implementation belongs in one cohesive Terra task, not
automatically on the primary. No task must pass through a fixed agent sequence.

Delegate bounded work when it can progress independently alongside useful
primary-agent work, or when an independent review materially improves confidence.
Prefer parallel read-only discovery and isolated implementation tasks for
substantial work. Do not delegate a single routine command merely to satisfy a
role label. Cost reduction is also a routing consideration; lack of parallel work
alone does not justify moving a complete routine task onto Astra. Delegation
remains subject to the session's tool and permission rules, including any
requirement for useful independent work alongside the primary. If those rules
prevent the preferred routing, report the constraint rather than bypassing it.

## Task decomposition

For substantial work, separate evidence collection, implementation, verification,
and decision-making before starting a broad scan or change. Assign a complete
routine implementation task to Terra and separate bounded evidence or independent
verification to Luna where useful and permitted. Do not keep discovery and
checking on the primary merely because it already knows the repository.

- Give Luna explicit files or a bounded search area, questions, expected evidence,
  and a stopping condition. Suitable tasks include enumerating consumers of an
  IR field, mapping specified PTX instruction forms to tests, checking a defined
  set of links, and running an agreed verification batch after the affected
  files are stable.
- Escalate architecture changes, unresolved complex ISA/semantic questions, and
  conflicting review findings to the primary or an appropriate reviewer. Routine
  interpretation within established contracts remains with Terra. For
  example, Luna gathers candidate duplicate tests and their assertions; the
  primary decides whether coverage is genuinely redundant.
- Assign cohesive implementation, relevant tests, and failure repair to Terra.
  A normal test failure stays with its implementation owner; it is not an
  automatic primary-agent takeover. Reuse a worker's
  findings rather than rescanning the same area; investigate only unresolved
  evidence or review concerns.
- Batch related routine work into one useful task packet, including diagnosis,
  edits, and checks. Keep only genuinely trivial work local when delegation
  overhead exceeds its benefit.
  There is no agent-count quota or mandatory handoff sequence.

If a worker reaches its stated boundary without enough evidence, request the
missing evidence narrowly or reassign the judgment-heavy part. Do not expand a
bounded scan into an unbounded review by default.

## Model preferences

| Role | Preferred model | Reasoning effort | Instructions |
| --- | --- | --- | --- |
| Primary: architecture, escalations, final acceptance | `gpt-6-astra` | Preserve the session setting | This document |
| Optional independent technical review | `gpt-5.6-sol` | `high` | [Technical review](#independent-technical-review) |
| Routine issue delivery and deep debugging | `gpt-5.6-terra` | `high` | [Terra](terra.md) |
| Bounded evidence scans, independent verification, Git | `gpt-5.6-luna` | `xhigh` for judgment-heavy verification | [Luna](luna.md) |
| Optional narrow coding worker | `gpt-5.3-codex-spark` | `high` only for a specific need | [Spark](gpt-5.3-codex-spark.md) |

These are preferences, not assertions that every session offers these models.
Check the actual tool's supported model list before assigning an override.
Do not attempt an unavailable model or invent an alias. The selected primary
model is controlled by the host/user; editing this file does not switch it.

If Spark is unavailable, use Terra for a bounded implementation or Luna for
read-only discovery, or complete the task directly. If Terra/Luna is unavailable,
use a suitable supported worker or the primary agent. Agent unavailability must
not block otherwise feasible authorized work. Report the unavailable model or
tool constraint and chosen fallback; do not silently make Astra the execution
default. An interrupted worker should be resumed or its remaining bounded work
reassigned when supported, rather than automatically absorbed by the primary.

If Sol is unavailable, use another suitable supported reviewer when independent
review is warranted. Otherwise the primary agent reviews directly and reports
any material verification gap; do not claim an independent review occurred.

Use a self-contained task packet and the smallest context fork that supports an
explicit worker model and effort. For tools where a full-history fork inherits
the parent model, use no history or a supported limited fork for Terra/Luna/Sol;
do not drop the override merely to keep full history. If full-history inheritance
is genuinely required, record that exception and the inherited model rather than
claiming a different worker model. Follow the actual tool schema and do not change
effort solely because the primary model upgraded.

Record the requested model and effort in the delegation record. Distinguish them
from the effective model when runtime metadata exposes it; otherwise mark the
effective model as unverified. A role name or successful spawn is not proof of
billing identity. For usage audits, distinguish request counts, input/cached-input
tokens, output tokens, and cost over the same period. Do not invent telemetry or
enforce arbitrary model-percentage quotas. Host model configuration is separate
from this policy and must not be changed without user authorization.

## Independent technical review

Use Sol when a separate review of an architecture proposal or cross-module
change would materially improve confidence, including work designed or
implemented by the primary agent. This is optional, not a gate for every task.

Give the reviewer the requirements, relevant contracts, proposed design/diff,
and validation evidence. Prefer a reviewer that did not author the work.
Review is read-only unless changes are explicitly assigned; return actionable
findings with file/symbol evidence, their consequences, and remaining uncertainty.

Sol provides a technical assessment; Luna remains the bounded evidence,
verification, and Git worker, and Terra owns routine implementation delivery. The
primary agent resolves conflicting findings and retains final design and
acceptance responsibility.

## Task packets and scope

Before delegating, read the selected worker's instructions. Provide:

- the objective and acceptance criteria;
- relevant files, existing decisions, and ownership/lifetime constraints;
- exact editable scope and shared-state restrictions;
- required validation and expected evidence;
- the search boundary, stopping condition, and concise return format;
- whether Git operations or external publication are authorized.

Workers report scope expansion or architectural conflicts to the primary agent.
Terra resolves ordinary implementation choices within the agreed contracts.
Escalations identify the specific decision, evidence, attempted approaches, and
available options. The primary resolves that decision and returns execution to
the owner where feasible; escalation is not automatically a question for the user.

## Parallel work and integration

- Assign one writer to each file or interacting subsystem at a time.
- Use disjoint ownership or isolated worktrees for concurrent implementation.
- Builds/tests sharing mutable output directories must be serialized.
- Once delegated, avoid duplicating a worker's investigation or implementation.
  Review its evidence and relevant diff; investigate further when evidence is
  incomplete, conflicting, or reveals a correctness concern.
- Prefer completion or blocker reports over frequent status polling and
  step-by-step intervention. Review concise evidence and key diffs, not a second
  full investigation. Final acceptance does not require replaying worker checks.
- The primary owns final acceptance, including review of code it delegates.
  Use independent verification for substantial or risky work when useful;
  small changes do not require a separate agent solely to rerun a passing check.

## Verification

Select tests from the changed contracts and explicit project gates. Fix a failed
check at its cause, then rerun the affected checks. Do not repeat a full suite
merely because work passed between agents. Reports identify commands, outcomes,
unverified behavior, and relevant repository state without dumping full logs.

## Git workflow

When Git work is authorized and ready, prefer a single bounded Luna task for
status/diff checks, staging, commit, and any separately authorized publication.
Batch the operation rather than delegating individual commands. Use useful
parallel handoff/evidence review where the session requires it. The primary may
perform genuinely trivial Git work or handle a documented tool/availability
constraint. Implementation workers do not stage or commit unless that scope
was explicitly assigned.

The Git owner checks status and the intended diff, reuses valid verification
evidence, stages authorized files, checks the staged diff, creates a descriptive
commit, and reports its hash and final status. Inspect hook changes before
continuing. Never amend or rewrite history without authorization.

Publication requires explicit user authorization. A request to commit alone
does not authorize push. Existing authorization for the same unfinished task
remains valid; a prior completed task does not authorize publishing new work.
