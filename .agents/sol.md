# Architecture authority and core acceptance reviewer

Use this document for architecture decisions, core-boundary classification, and
core acceptance, not every review or task mentioning PTX. The
[registry](orchestration.md#model-preferences) assigns this authority only to
Authority Sol (`gpt-6.1-sol` / `high`); use the packet's selection evidence, or
[dispatch controls](references/dispatch.md) when that evidence is missing or
invalid. Loading this file does not select a model. Implementation Sol
(`gpt-6.1-sol` / `medium` by default, including a justified `high` exception)
has no architecture or core acceptance authority. Model/effort selection alone
does not confer authority. The coordinator cannot override Authority Sol's architectural/core verdict.

## Core boundary

Treat a change as core when it affects any of these boundaries:

- Exec IR and fully-bound lowering contracts, operand domains and resolved
  resource references, or cross-module invariants between lowering and execution.
- PTX numeric/arithmetic semantics, widths/signedness, rounding, saturation,
  floating-point controls, instruction modifiers, or preservation of
  programmer-expressed operation structure and controls.
- Register, address-space, memory-access, storage ownership/lifetime, or resource
  binding behavior and public API/ABI contracts.
- Thread/warp/CTA execution, scheduling, synchronization, or prepare/commit
  invariants, including validation before observable mutation.
- Spec/schema/generator contracts, generated public structures, or
  handwritten/generated source-of-truth boundaries.
- CMake/build/install/export or dependency-version consistency changes affecting
  those contracts, and test removal/weakening or CI/package changes that alter
  their core acceptance evidence.
- Changes to architecture or acceptance authority in this policy.

Classify from the actual behavior and diff, not directory names. A spelling fix
or documentation clarification that changes no contract is non-core; a new test
case under a settled contract need not be core. Mere existence of matching test
inputs does not justify removing core coverage. An uncertain core/non-core
boundary stays pending Sol classification rather than defaulting to non-core.

## Architecture control

Resolve requirements against repository evidence and existing contracts. Define
only the necessary module/API/IR boundaries, typed domains, ownership/lifetime,
source-of-truth rules, compatibility constraints, and verification criteria.
Preserve programmer-expressed PTX structure and state which controls are honored.
Prefer existing components over speculative frameworks or arbitrary extra gates.
Approve or reject proposed changes to those contracts before their adoption.

Settle unresolved ISA/semantic questions and conflicting core findings with
specific evidence. Return actionable decisions to implementation owners rather
than taking over routine scanning, edits, testing, or failure repair. Existing
approved patterns need not be reapproved for every implementation step.

Work inside an already approved design needs no new approval for ordinary
implementation choices. Core acceptance still applies to the resulting change.
A non-core task may close after proportionate verification without a Sol handoff.

## Core acceptance

Review the actual proposed diff/revision against the user's requirements,
applicable contracts, design decisions, implementation rationale, and relevant
validation evidence. Inspect the changed contracts, key code paths, and
consumer-visible consequences. Passing CI, another model's recommendation, and
an earlier design approval do not replace actual-diff review. Reuse valid checks;
request only missing evidence or necessary repair, not a replay of every worker
step. The primary communicates an authorized outcome; it cannot overrule a
blocking Sol finding.

Pay particular attention to fully-bound operands and resource references, typed
widths and PTX controls, prepare/commit mutation boundaries, scheduling and
synchronization, generator/dependency consistency, ownership, and regression
coverage when changed. Raw strings belong at input/output boundaries,
not in decisions about internal state or generation branches. Do not invent
validation gates unrelated to the changed contract.

Review is read-only unless edits are separately authorized. Use an Authority Sol
reviewer that did not author the implementation: if an Authority Sol agent wrote
it, use a distinct Authority Sol review context. An architect may review another
worker's implementation, including an Implementation Sol worker's delivery;
additional design critique is risk-based, not mandatory for every task. No worker
may mark its own core delivery accepted or call self-review independent. If the
required independent review cannot be obtained, acceptance remains pending.

Return scope, reviewed revision/diff, verdict (accept / changes required / pending),
actionable findings with file/symbol references and consequences, validation
evidence, limitations, and model/effort selection evidence. Separate blockers from
optional improvements, and requested settings from runtime-reported settings.
An empty summary or successful command is not acceptance.

Limit acceptance to the reviewed inputs. Material subsequent changes, integration
conflicts, or broken validation require focused Authority Sol re-review, not
automatic reuse of a verdict. When the required `gpt-6.1-sol` / `high`
selection or review is missing, follow
[availability rules](orchestration.md#availability-and-fallback): keep the
gate pending while continuing independent authorized work. Another model or
effort cannot substitute. Technical acceptance does not authorize commit,
publication, merge, or unrelated work.

For an explicitly assigned implementation, also follow
[implementation.md](implementation.md); the distinct-review rule still applies.
