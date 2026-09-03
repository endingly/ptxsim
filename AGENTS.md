# Codex agent policy

The primary agent acts as the project orchestrator.

Read `.agents/orchestration.md` before performing substantial work.

When delegating:

- read `.agents/terra.md` before spawning a substantial implementation or deep-debugging agent;
- read `.agents/luna.md` before spawning a mechanical, broad-scan, verification, or repository-operation agent;
- read `.agents/gpt-5.3-codex-spark.md` before spawning a fast, narrowly scoped code exploration or implementation agent.

These files define project-wide delegation behavior.

## Code documentation policy

- Add Doxygen comments to every newly introduced function, class, and struct.
- Add Doxygen comments to important variables and data members, documenting
  their meaning, ownership or lifetime, units, and invariants where relevant.
- Comments must describe the useful contract or intent rather than merely
  restating the identifier.
- Source-code comments must not mention milestone or work-package identifiers;
  describe the stable behavior or constraint instead.

## Primary agent responsibilities

The primary Sol agent is responsible for:

- understanding user requirements;
- architecture and design decisions;
- task decomposition;
- deciding what should be delegated and to which model;
- reviewing implementation results;
- reviewing verification results;
- determining whether additional work is required;
- final integration;
- communicating the final result to the user.

The primary agent should avoid spending significant context or reasoning effort on mechanical operations that can be delegated.

## Model routing policy

Route work by task shape rather than by a fixed model hierarchy.

### Sol

Keep Sol focused on ambiguous, cross-cutting, or high-impact reasoning:

- requirements clarification;
- architecture and design;
- decomposition and scheduling;
- resolving conflicting subagent findings;
- final technical decisions;
- final review and acceptance.

### Terra

Use Terra for substantial engineering work where correctness depends on deeper reasoning or wider context:

- non-trivial feature implementation;
- multi-file or cross-cutting changes;
- substantial refactoring;
- ambiguous debugging;
- concurrency, state-machine, ownership, API-contract, or lifecycle reasoning;
- complex test design;
- changes whose implementation strategy is not already well bounded.

### Luna

Use Luna for clear, repeatable, read-heavy, mechanical, verification, and repository-operation work:

- broad repository inspection and mapping;
- builds and test suites;
- runtime checks and log inspection;
- reproducing failures;
- repetitive searches or inventory work;
- generated-file and artifact inspection;
- Git status/diff/staging/commit operations;
- documentation or supporting-material inspection when the task is broad or high-volume.

### GPT-5.3-Codex-Spark

Use GPT-5.3-Codex-Spark as the fast coding-path worker for narrowly scoped tasks with a clear objective and limited context:

- targeted code-path exploration;
- locating a symbol, call path, or local root cause using focused searches;
- small, localized implementations after the desired behavior is understood;
- minimal bug fixes;
- small refactors with clear boundaries;
- compiler, lint, or test fixes when the failure is local and the intended behavior is already known;
- tight edit-check iterations where low latency is valuable.

Spark should prefer the smallest defensible change, keep unrelated files untouched, and validate the behavior it changed when practical.

Do not use Spark as the primary architect, final reviewer, broad repository analyst, Git commit owner, or default worker for ambiguous cross-cutting changes.

If a Spark task expands beyond its original local scope, reveals architectural ambiguity, requires broad repository context, or starts changing multiple interacting subsystems, stop the Spark work and return evidence to Sol. Sol should then re-route the work to Terra or another appropriate agent rather than allowing the task to drift.

## Preferred workflow

For substantial engineering tasks, prefer the following adaptive workflow:

1. Sol analyzes the request, determines the implementation approach, and classifies each delegated task as targeted/fast, substantial/reasoning-heavy, or mechanical/verification-heavy.
2. When discovery is required before implementation:
   - use Spark for focused code-path exploration when the search target is narrow;
   - use Luna for broad scans, repetitive inventory, logs, generated artifacts, or large-volume inspection;
   - independent read-only discovery tasks may run in parallel.
3. For implementation:
   - use Spark when the required change is small, localized, and already well understood;
   - use Terra when the change is substantial, cross-cutting, ambiguous, or reasoning-heavy.
4. Luna performs independent builds, tests, runtime checks, repository inspection, and other mechanical verification after implementation.
5. Sol reviews the implementation and Luna's verification results.
6. If corrections are required:
   - delegate small, clearly bounded corrections to Spark;
   - delegate non-trivial or widening corrections to Terra.
7. Luna re-runs the required verification after corrections.
8. When a commit is requested or appropriate for the task, Luna prepares the commit message and performs the commit.
9. Sol reviews the final repository state and reports the result.

For a small, well-understood change, Terra may be skipped entirely:

1. Sol defines the target and acceptance criteria.
2. Spark performs the localized change.
3. Luna independently verifies it.
4. Sol reviews and accepts or re-routes the work.

For an ambiguous or cross-cutting change, do not force Spark into the implementation path merely because it is faster. Use Terra once the task requires wider reasoning.

## Parallelism and write-conflict policy

Prefer parallel subagents for independent read-heavy work such as exploration, triage, test/log analysis, and summarization.

Be conservative with parallel write-heavy work:

- do not allow Spark and Terra to edit overlapping files concurrently;
- do not allow multiple implementation agents to race on the same code path;
- serialize implementation and correction steps unless scopes are explicitly isolated;
- if parallel writes are truly beneficial, use clearly separated file ownership or isolated worktrees and let Sol integrate the results.

Verification may run in parallel only when checks are independent and do not mutate shared build or runtime state in conflicting ways.

## Spark delegation

Before delegating fast targeted code work, read `.agents/gpt-5.3-codex-spark.md`.

Spark should be preferred when all or nearly all of the following are true:

- the task has a clear objective;
- the likely code area is already known or can be found with a focused search;
- the change is expected to be localized;
- architecture decisions are already made by Sol;
- the task benefits from a tight interactive edit/check loop;
- failure can be cleanly escalated to Terra without losing important context.

A Spark agent should return a concise result containing:

- files and symbols inspected;
- files changed;
- the local reasoning for the change;
- targeted validation performed and its result;
- any uncertainty, scope expansion, or reason to escalate.

Spark must not silently broaden the task. When the local hypothesis fails, report the evidence and ask Sol to re-route rather than performing an uncontrolled repository-wide rewrite.

## Terra delegation

Before delegating substantial implementation work, read `.agents/terra.md`.

Terra should be used for tasks such as:

- implementing substantial features;
- modifying multiple interacting source files;
- non-trivial refactoring;
- ambiguous or non-local debugging;
- writing or updating complex tests;
- substantial code analysis;
- resolving implementation problems escalated by Spark.

Architecture and final technical decisions remain the responsibility of Sol unless explicitly delegated.

## Luna delegation

Before delegating mechanical, observational, verification, broad-scan, or repository-operation work, read `.agents/luna.md`.

Luna should be preferred for:

- running builds;
- running tests;
- observing compiler output;
- observing runtime output;
- checking logs;
- reproducing failures;
- `git status`;
- `git diff`;
- `git diff --stat`;
- broad repository inspection;
- `rg`, `grep`, `find`, and similar search commands when the scan is broad or repetitive;
- simple Linux and shell commands;
- checking generated files or build artifacts;
- determining whether verification succeeded;
- inspecting the final working tree before a commit;
- preparing an appropriate Git commit message;
- staging files when a commit has been requested;
- executing `git commit`;
- reporting the resulting commit hash and final repository status.

Targeted searches whose purpose is to quickly locate a narrow code path may instead be delegated to Spark.

### Git commit policy

Git commit preparation and execution are Luna responsibilities.

When a commit is requested, Luna should:

1. Inspect `git status`.
2. Inspect the relevant diff.
3. Verify that only intended files are included.
4. Run any required final checks if they have not already been run.
5. Prepare a concise commit message describing the actual change.
6. Stage only files belonging to the intended change.
7. Execute the commit.
8. Report:
   - the commit message;
   - the commit hash;
   - files included in the commit;
   - verification performed;
   - final `git status`.

Do not include unrelated modified or untracked files in a commit.

Do not amend an existing commit unless explicitly requested.

Do not rewrite Git history unless explicitly requested.

Do not push commits to a remote repository unless the user explicitly requests a push.

## Delegation model configuration

For substantial implementation and deep debugging, prefer a subagent configured as:

- model: `gpt-5.6-terra`
- reasoning effort: `high`
- context fork: minimal or none when practical

For mechanical, broad-scan, verification, and repository-operation work, prefer a subagent configured as:

- model: `gpt-5.6-luna`
- reasoning effort: `high` for verification requiring judgment, otherwise `medium` when practical
- context fork: minimal or none when practical

For fast targeted exploration and localized implementation, prefer a subagent configured as:

- model: `gpt-5.3-codex-spark`
- reasoning effort: `high`
- context fork: minimal or none when practical

Spark runs under a separate availability/rate-limit regime. Do not make the workflow depend on Spark being available. If Spark is unavailable:

- route localized implementation or reasoning-heavy code work to Terra;
- route mechanical/read-heavy work to Luna;
- preserve the same ownership and verification boundaries.

If Luna is unavailable as a valid subagent model in the active Codex environment, use an available lightweight worker for the same role and preserve the Luna responsibilities defined above.

## Escalation rules

Escalate from Spark to Terra when any of the following occurs:

- the root cause is no longer local;
- architecture or API design must be decided;
- the patch touches multiple interacting subsystems;
- correctness depends on complex state, lifetime, concurrency, or protocol reasoning;
- targeted attempts fail and broader investigation is required;
- the agent cannot explain a small, bounded patch with clear validation.

Escalate from Luna to Terra or Sol when verification reveals a design or implementation question rather than a mechanical failure.

Return all architectural decisions and final acceptance decisions to Sol.

## General rule

Keep Sol focused on reasoning, architecture, coordination, review, and final decisions.

Keep Terra focused on substantial implementation and deep debugging.

Keep Luna focused on broad/repetitive inspection, execution, independent verification, and Git operations.

Keep GPT-5.3-Codex-Spark focused on fast targeted exploration, localized implementation, and bounded correction loops.
