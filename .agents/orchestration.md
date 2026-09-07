# Orchestration policy

## Ownership and routing

The primary agent owns requirements, architecture, task decomposition, final
integration, acceptance, and communication. Model preferences are centralized
below; worker documents define role-specific behavior, not competing defaults.

Small or tightly coupled tasks may be completed directly by the primary agent,
including exploration, implementation, checks, and authorized Git operations.
No task must pass through a fixed sequence of agents.

Delegate bounded work when it can progress independently alongside useful
primary-agent work, or when an independent review materially improves confidence.
Prefer parallel read-only discovery and isolated implementation tasks for
substantial work. Do not delegate a single routine command merely to satisfy a
role label. Delegation remains subject to the session's tool and permission rules.

## Model preferences

| Role | Preferred model | Reasoning effort | Instructions |
| --- | --- | --- | --- |
| Primary | `gpt-6-astra` | Preserve the session setting | This document |
| Optional independent technical review | `gpt-5.6-sol` | `high` | [Technical review](#independent-technical-review) |
| Substantial implementation/deep debugging | `gpt-5.6-terra` | `high` | [Terra](terra.md) |
| Broad scans, independent verification, Git | `gpt-5.6-luna` | `medium`; `high` for judgment-heavy verification | [Luna](luna.md) |
| Optional narrow coding worker | `gpt-5.3-codex-spark` | `medium`; `high` only for a specific need | [Spark](gpt-5.3-codex-spark.md) |

These are preferences, not assertions that every session offers these models.
Check the actual tool's supported model list before assigning an override.
Do not attempt an unavailable model or invent an alias. The selected primary
model is controlled by the host/user; editing this file does not switch it.

If Spark is unavailable, use Terra for a bounded implementation or Luna for
read-only discovery, or complete the task directly. If Terra/Luna is unavailable,
use a suitable supported worker or the primary agent. Agent unavailability must
not block otherwise feasible authorized work.

If Sol is unavailable, use another suitable supported reviewer when independent
review is warranted. Otherwise the primary agent reviews directly and reports
any material verification gap; do not claim an independent review occurred.

Use a minimal context fork with a self-contained task packet when practical.
Model overrides and effort values must follow the tool's actual schema; if a
full-history fork requires inheritance, omit overrides instead of sending an
invalid request. Do not change effort solely because the primary model upgraded.

## Independent technical review

Use Sol when a separate review of an architecture proposal or cross-module
change would materially improve confidence, including work designed or
implemented by the primary agent. This is optional, not a gate for every task.

Give the reviewer the requirements, relevant contracts, proposed design/diff,
and validation evidence. Prefer a reviewer that did not author the work.
Review is read-only unless changes are explicitly assigned; return actionable
findings with file/symbol evidence, their consequences, and remaining uncertainty.

Sol provides a technical assessment; Luna remains the preferred execution and
verification worker, and Terra the substantial implementation worker. The
primary agent resolves conflicting findings and retains final design and
acceptance responsibility.

## Task packets and scope

Before delegating, read the selected worker's instructions. Provide:

- the objective and acceptance criteria;
- relevant files, existing decisions, and ownership/lifetime constraints;
- exact editable scope and shared-state restrictions;
- required validation and expected evidence;
- whether Git operations or external publication are authorized.

Workers report scope expansion or architectural conflicts to the primary agent.
The primary resolves ordinary implementation choices itself or reassigns the
task; escalation to another agent is not automatically a question for the user.

## Parallel work and integration

- Assign one writer to each file or interacting subsystem at a time.
- Use disjoint ownership or isolated worktrees for concurrent implementation.
- Builds/tests sharing mutable output directories must be serialized.
- Once delegated, avoid duplicating a worker's investigation or implementation.
  Review its evidence and relevant diff; investigate further when evidence is
  incomplete, conflicting, or reveals a correctness concern.
- The primary owns final acceptance, including review of code it delegates.
  Use independent verification for substantial or risky work when useful;
  small changes do not require a separate agent solely to rerun a passing check.

## Verification

Select tests from the changed contracts and explicit project gates. Fix a failed
check at its cause, then rerun the affected checks. Do not repeat a full suite
merely because work passed between agents. Reports identify commands, outcomes,
unverified behavior, and relevant repository state without dumping full logs.

## Git workflow

Prefer Luna for a delegated commit/push task. The primary may perform it when
delegation offers no useful benefit or a worker is unavailable. Implementation
workers do not stage or commit unless that scope was explicitly assigned.

The Git owner checks status and the intended diff, reuses valid verification
evidence, stages authorized files, checks the staged diff, creates a descriptive
commit, and reports its hash and final status. Inspect hook changes before
continuing. Never amend or rewrite history without authorization.

Publication requires explicit user authorization. A request to commit alone
does not authorize push. Existing authorization for the same unfinished task
remains valid; a prior completed task does not authorize publishing new work.
