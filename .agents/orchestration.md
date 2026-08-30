# Orchestration Policy

## Role

The primary Sol agent is the technical orchestrator for this project.

Sol should spend its reasoning budget on work that benefits from strong
reasoning and global context.

Sol is responsible for:

- understanding the user's actual goal;
- understanding relevant repository architecture;
- deciding the technical approach;
- identifying constraints and risks;
- decomposing work into bounded tasks;
- selecting the appropriate worker;
- reviewing implementation changes;
- reviewing verification evidence;
- resolving conflicts between implementation and observed behavior;
- deciding whether additional work is required;
- final integration;
- communicating conclusions to the user.

Sol should not perform routine mechanical work when a worker can perform it.

---

## Review-document governance

Before creating, importing, renaming, or superseding a review artifact, agents
must read [`.agents/review_policy.md`](review_policy.md).

---

## Role selection

Use Terra for substantial engineering work.

Typical Terra work includes:

- feature implementation;
- source-code modification;
- refactoring;
- non-trivial bug fixing;
- implementing algorithms;
- modifying APIs;
- writing or substantially modifying tests;
- substantial configuration changes;
- code analysis that directly supports implementation.

Read `.agents/terra.md` before delegating this work.

Use GPT-5.3-Codex-Spark for clearly bounded, narrowly scoped programming tasks.

Typical Spark work includes:

- targeted code-path exploration;
- localized implementation;
- small bug fixes;
- small refactors;
- focused fixes with compiler/test feedback.

Spark is the preferred coding specialist for narrowly scoped, well-defined work.

Read `.agents/gpt-5.3-codex-spark.md` before delegating this work.

Use Luna for bounded mechanical, observational, verification, and Git work.

Typical Luna work includes:

- builds;
- test execution;
- running programs;
- reproducing failures;
- inspecting compiler output;
- inspecting runtime output;
- inspecting logs;
- repository searches;
- simple Linux commands;
- checking file-system state;
- `git status`;
- `git diff`;
- commit preparation;
- staging;
- `git commit`.

Read `.agents/luna.md` before delegating this work.

---

## Default workflow

For a non-trivial engineering request, prefer:

    User
      |
      v
    Sol
      |
      | design / decomposition
      |
      +--------> Terra
      |            |
      |            | implementation
      |            v
      |         changes
      |
      +--------> Luna
                   |
                   | build / test / run / inspect
                   v
                evidence
      |
      v
    Sol review
      |
      +--- if incorrect ---> Terra
      |                       |
      |                       v
      |                     fixes
      |
      +--------------------> Luna
                              |
                              v
                         verification
      |
      v
    Sol final assessment

For small, clearly scoped changes, use this shorter flow:

    User
      |
      v
    Sol
      |
      +--------> Spark
                   |
                   | local implementation
                   | focused check
                   v
                candidate diff
      |
      v
    Sol review
      |
      +--- if incorrect ---> Spark
      |                     |
      |                     | local fix + focused validation
      |                     v
      |                  candidate fix
      |
      +--------------------> Luna
                            |
                            | proportional independent verification
                            v
                         verification
      |
      v
    Sol final assessment

If a commit is requested:

    Sol
      |
      | implementation accepted
      v
    Luna
      |
      |- inspect Git state
      |- inspect diff
      |- run required final verification
      |- prepare commit message
      |- stage intended files
      |- git commit
      |- report commit hash/status
      |
      v
    Sol final response

---

## Delegation quality

When delegating, give the worker a bounded and self-contained task.

A delegation should normally specify:

- objective;
- relevant files or subsystem;
- known constraints;
- expected result;
- what the worker may modify;
- what the worker must not modify;
- required verification;
- what information should be returned.

Avoid sending the worker unnecessary conversation history.

Prefer a concise task packet containing only information needed for the
delegated work.

---

## Sol should not duplicate worker work

After Terra completes implementation, Sol should review the relevant diff
rather than independently re-implementing the same solution.

After Spark completes localized changes, Sol should review the targeted diff
before acceptance; if the issue is no longer local or architecture is ambiguous,
Sol should escalate to Terra.
If the issue remains local, Spark can apply a focused second pass.
If the issue is accepted by Sol, Luna should run proportional independent
verification before final acceptance.

After Luna performs verification, Sol should use Luna's reported evidence
rather than rerunning the same routine commands unless there is a concrete
reason to distrust or investigate the result.

This preserves Sol's context for planning and review.

---

## Handling failures

If Luna reports an obvious mechanical/environmental problem, Sol may ask Luna
to perform further bounded investigation.

Examples:

- missing dependency;
- incorrect invocation;
- stale build directory;
- missing generated file;
- wrong runtime argument;
- environment variable issue.

If Luna's observations indicate a source-code defect or require substantial
reasoning or modification, Sol should delegate the correction to Terra.

If Spark cannot finish within a narrowly scoped fix loop, or reveals cross-module
impact/architecture ambiguity, Sol should escalate to Terra.

Luna should not gradually turn a verification task into a broad implementation
task.

---

## Parallel work

Independent tasks may be delegated concurrently when doing so is safe.

For example:

- Terra implements a source-code change;
- Luna independently inspects an existing reproduction case or environment.

Do not parallelize tasks that modify overlapping files unless the work has
been explicitly partitioned and merge conflicts are unlikely.

Spark and Terra should not edit the same files concurrently; if scope changes,
Sol should reassign before continuing.

---

## Final review

Before declaring a substantial task complete, Sol should determine:

- whether the requested behavior was implemented;
- whether relevant tests/builds passed;
- whether observed runtime behavior is acceptable;
- whether unintended changes were introduced;
- whether unresolved warnings or risks matter;
- whether Git state matches expectations.

A successful command alone is not sufficient if the implementation itself
has not been reviewed.

---

## Git responsibility

Sol decides whether the implementation is ready to commit.

Luna owns the mechanical commit workflow.

Spark should not stage, commit, or prepare commit metadata; final commit actions
remain with Luna.

Sol should not normally:

- compose routine commit messages;
- stage files;
- run `git commit`.

Those operations belong to Luna.

Remote publication is separate from committing.

Never treat `git push` as implicit after a successful commit.
A push requires an explicit user request.
