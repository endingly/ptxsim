# Review-Document Governance

Read this policy before creating, importing, renaming, or superseding a review
artifact.

## Archive and naming

- The canonical archive is `docs/reviews/`, indexed by its maintained
  `README.md`.
- Name canonical records for the current project-plan milestone, never a
  branch or legacy sequence: `v<plan>-m<milestone>-<stage>-review[.rN].md`.
  Current examples are `v2-m1-main-review.md`, `v2-m1-fix-review.md`, and
  `v2-m1-fix-rereview.r1.md`.
- Record legacy `m4`/`m5` source names and unchanged branch names in archival
  metadata. Branch renames are not required; `fix/m5-main-review` is not
  V2-M5.

## Preservation and links

- Each archival banner states status, original filename/hash, reviewed
  branch/SHA and baseline, current milestone mapping, and
  supersedes/superseded-by links.
- When a source exists, preserve its report body byte-for-byte immediately
  after a banner separator, with no injected blank byte. The current
  `project_plan.md` owns live gate truth; archived bodies are snapshots and
  are not rewritten for later state.
- Never delete a tracked historical review to hide stale status. Move or rename
  it, add a Historical/Superseded banner, and repair canonical links.
- If an original is missing, do not fabricate it. Record transparent provenance
  and a recovery target; replace the record only when the matching source is
  recovered.

## Lifecycle and checks

- Transient uploaded duplicates may be deleted only after archived copy, hash,
  link checks, and the archival commit. Preserve unrelated user files.
- Review governance is its own logical commit. Docs-only changes require
  naming/link/hash/diff checks, not builds or workflows unless documentation
  affects them or the user asks.
