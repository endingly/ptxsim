# m5-main Review Provenance and Closure Record

> **Original artifact unavailable.** The original untracked
> `m5-main-review.md` was deleted and cannot be restored byte-for-byte. This
> record deliberately does not recreate or infer its prose.
> **Known baseline and purpose:** the MAIN P0/P1/P2 checklist was reviewed
> against `main@13bd652377b2256da05b1b8b4d106c365b9488b6` and drove the
> `fix/m5-main-review` remediation sequence. The mapping below is preserved
> from the project plan's known commit/test records, not from a reconstruction
> of the unavailable review.
> **Audit chain:** [m4 historical review](m4-review.md) → this provenance
> record → [m5 remediation review](m5-fix-review.md) →
> [current project plan](../../.agents/project_plan.md).

## Known MAIN issue-to-fix mapping

| Issue | Fix commit | Regression target / check |
|---|---|---|
| MAIN-P0-004 | `173a951` | `test_scalar.cpp`, `test_special.cpp` |
| MAIN-P0-001 | `04c67c1` | `test_scalar.cpp` |
| MAIN-P0-002 | `6adc5f7` | `test_special.cpp` |
| MAIN-P0-003 | `6be18ec` | `test_special.cpp` |
| MAIN-P1-001 | `e48fba2` | `test_special.cpp` |
| MAIN-P1-002 | `93147b9` | `test_conversion.cpp` |
| MAIN-P1-003 | `1aa80f9` | `test_conversion.cpp` |
| MAIN-P1-004 | `7dd0e71` | `test_conversion.cpp` |
| MAIN-P1-005 | `97eab10` | `test_bit.cpp` |
| MAIN-P1-006 | `ad44346` | `test_special.cpp` |
| MAIN-P1-007 | `3b50b45` | `test_scalar.cpp` |
| MAIN-P1-008 | `de1ab28` | `test_bit.cpp`, `test_scalar.cpp` |
| MAIN-P1-009 | `35873ec` | `test_tensor.cpp` |
| MAIN-P1-010 | `2c59b86` | `test_packed.cpp` |
| MAIN-P2-001 | `d2b2ad9` | `test_scalar.cpp` |
| MAIN-P2-002 | `d3850cf` | five workflow presets; manifest feature dry-run/config with tests enabled and frontend omitted |
| MAIN-P2-003 | `20e0e37` + `def4b8a` | `frontend-lowering` feature install; automatic snapshot integrity check + documented manual regeneration/byte comparison |
| MAIN-P2-004 | external / pending | GitHub `main` branch protection and hosted CI checks |
| MAIN-P2-005 | `57f3593` | `ptxsim_arith_public_header_check` |
| MAIN-P2-006 | `24dd230` | plan/document consistency searches |

`MAIN-P2-004` remains an external governance action. Subsequent local
remediations (`05b0af2`, `419e2e6`, and `def4b8a`) and the current document
governance follow-up are recorded in the project plan; hosted CI and branch
protection remain acceptance gates.
