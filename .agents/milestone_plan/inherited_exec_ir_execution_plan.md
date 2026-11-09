# Inherited executable IR delivery plan

> **Status:** design approved; delivery and core acceptance pending.
> **Authority:** [Inherited executable IR architecture](../arch/inherited_exec_ir.md).
> **Baseline:** `origin/main` `775f004af2c6861a053e47d686137fe08d8d29b0`.
> **Branch/worktree:** `refactor/inherited-exec-ir`, `/root/code/ptxsim-ir-inheritance`.

## Ownership and acceptance

The primary coordinates integration. Authority Sol (`gpt-6.1-sol`, `high`)
owns architectural decisions and core acceptance. Implementation Sol
(`gpt-6.1-sol`, `high`) owns cross-file changes: the effort is explicitly
user-directed and confers no core acceptance authority. Independent slice
review uses another `gpt-6.1-sol`, `max`, as explicitly requested by the user;
Authority Sol still gives the core verdict. Bounded scans, verification and
Git use `gpt-6-luna`, `max`: the requested Luna 6.1 pair was unavailable and the
registry permits this fallback and it has been disclosed to the user. Requested
model/effort pairs
are supported by tool parameters; effective runtime pair reporting is not
exposed. One implementation writer owns coupled files. Git stages and commits
only an accepted slice, continuously; push is not authorized.

The original `/root/code/ptxsim` has unrelated/local policy and old migration
changes and remains untouched. Its local `ea0dc33` policy commit is not part of
the fetched baseline. Current user-provided agent policy applies. The nine
policy files from `ea0dc33` are imported byte-for-byte with per-file `git show`
extraction as an explicitly user-authorized S0 policy update and reviewed with
this slice. This does not import `ea0dc33` into branch ancestry; no original
dirty business migration changes are copied.

This plan is the current gate for representation, binding, capability and
Python migration. Historical module-plan implementation statuses describe the
previous design. Completion requires all slices, not merely a working subset
or an unsupported fallback for an unbound operand.

## Baseline inventory and contract ledger

An independent normalized-model scan of the exact 0.8.0 wheel observed
`ptx-instr/v1`, 137 opcodes, 5,453 forms, and 7,457 layouts. The installed
wheel metadata reports the architecture's exact release URL and SHA256.
This was an inventory observation in the existing probe environment, not a
verification of the new worktree's eventual editable install or wheel.

Three distinct inventories must remain separate:

| Layer and reproducible scan | Represented kinds | Enum domain size |
| --- | --- | --- |
| Normalized specification: distinct `operand.kind` across every instruction/variant/layout operand | 48 `OperandKind` values | 71 |
| Normalized specification: distinct `modifier.kind` excluding absent modifiers | 28 `ModifierKind` values | 36 |
| Resolved model: distinct `field.value_kind` in layout operand fields after `from_instruction_spec()` | 41 operand storage categories | 74 total `ResolvedValueKind` values |
| Resolved model: distinct modifier-field value kinds | 28 modifier storage categories | Same 74-value enum |

The resolved fields represent 69 distinct value kinds in total. Several of the
48 specification operand kinds normalize into the same resolved storage
category; 41 is not a smaller supported-operand selection. All 48 specification
operand kinds, their alternatives and specialized contracts need complete
owned mappings and binders. The final projection-derived ledger must check
both specification kinds and resolved categories and fail missing coverage.

| Contract family | Required execution-owned representation/binding |
| --- | --- |
| Registers/predicates/sinks/vectors | Slot, width/type, complement/sink, ordered owned elements |
| Immediate/reg-or-immediate/conversion/movement | Owned bits/value type and exact selected alternative |
| Addresses/labels/symbols/functions | Bound spaces, offsets, relocations, function-local PCs and function IDs |
| Calls/parameters/branch sets | Ordered argument/return bindings, parameter IDs and owned target sets |
| Tensor/matrix/shared descriptors/TCGEN | Typed addresses, coordinates, selectors, layouts and control domains |
| Texture/surface/fabric/resources | Typed owned handle or declared resource identity and selected topology |
| Stack/local allocation/mbarrier/shuffle | Function-bound tokens/results, predicate alternatives and completion identities |
| Dynamic/fixed modifiers | All domains and values, with static constants and selected dynamic controls |
| Form/layout contracts | Optional/list structure, CC/completion/WGMMA and specialized execution metadata |

Binding must preserve register component declaration-width/lane views;
stack/local-allocation function scopes; typed opaque resource scope/kind/entry
roles; storage relocation symbol/member/address-kind/addend bits/byte masks;
static opaque objects, managed/unified UUIDs and arrays; branch/call target
sets, canonical functions, prototypes, ABI and call-table slots; and immediate
use-width bits plus signed/integer-source metadata. These are execution
contracts even before their semantics are implemented. Diagnostic source
ranges/spellings may be discarded. A real PTX opaque resource remains a typed
resource and is distinct from an opaque frontend payload.

Observed non-default contract counts are 24 condition-code effects, 2,701
completion kinds and 2,151 WGMMA protocol actions. Other present form metadata
includes matrix (3,298), fabric (44), surface (340), stack (6), texture (192),
video (23), tensor access modes (356), tensor destinations (180), tensor
reduction operations (128), and TCGEN fence directions (2). Counts overlap and
are inventory checks, not claims of semantic executor coverage.

## Delivery slices

| Slice | Scope and required evidence | Status |
| --- | --- | --- |
| S0 | Adopted architecture, current plan, exact source/artifact inventory, explicit old-contract supersession, role/effort/provenance record; diff and local-link checks | Accepted and committed `8d7b6a2` |
| S1 | Physical Python package consolidation and CMake/CI/docs paths, retaining the exact current frontend C++/Python pin and dependency behavior; source tests, isolated wheel and sdist-built wheel resource loading, CMake input/rebuild checks | Verified; slice acceptance pending |
| S2 | Advance both frontend pins to 0.8.0; remove direct Jinja2 and separate production/build dependencies; one full projection, owned leaves and final inherited classes, closed tags and immutable program storage; exact 137/5,453/7,457 parity, all classes compiled through narrow per-opcode and bounded form/source shards, provenance/distribution/ownership checks | Pending |
| S3 | Full generated frontend binding and switch to new public IR; every leaf/layout/contract covered, real resolved-module lifetime/resource/storage/relocation tests and meaningful full-binding evidence | Pending |
| S4 | Capability YAML and dense typed-group dispatch, all previously supported execution behaviors preserved, temporary bridge removed; negative rule/link tests and issue-ordering/per-lane regressions | Pending |
| S5 | Coverage ledger, consumers and installed package, complete CTest and GCC/Clang Debug/Release plus sanitizer gates; actual-diff core review and final documentation | Pending |

Each stable diff receives independent max-effort review and Authority Sol
acceptance before the Git owner commits and the next slice begins. S2 may add a
candidate target alongside the old public implementation. S2/S3 may form one
coupled commit when a buildable boundary requires it. A minimal temporary engine
entry adapter may bridge S3 to S4, must preserve behavior and is removed by S4.
No slice is described as complete while required targets are broken.

Authority Sol approved S1 as a physical move with the old exact frontend pin
and direct Jinja2 declaration preserved temporarily. S2 advances frontend C++
and Python together, removes direct Jinja2 and installs only the matching
frontend source's production dependencies in the overlay. This sequencing
avoids a broken intermediate generator because frontend 0.8.0 removed APIs
used by the old generator. S1 does not claim the requested release/dependency
target is complete. No compatibility manifest or form-alias layer is added.

## Verification record

- S0 committed as `8d7b6a29365cd666c8fb73638fb91bebf9c3c144`. Independent
  `gpt-6.1-sol/max` and Authority `gpt-6.1-sol/high` accepted the actual staged
  15-file fingerprint
  `c7fb938c7b3dc7b950d5cd66cfc2620256e26dc81094c94d81381618630b881c`.
  Git normalized CRLF to LF in four historical documents; per-file comparison
  proved no other byte differences. Policy-byte, local-link and diff checks
  passed; no C++ tests were needed for that documentation-only slice.
- Bounded baseline scan found an existing Simulator ordering gap: `Simulator::step`
  asks for fallthrough before the engine selects unsupported. Engine-only tests
  do not prove simulator ordering. S4 must reject unsupported before ordinary
  reads, state changes and terminal/invalid-successor fallthrough errors, with
  end-to-end regression tests; supported instruction ordering remains intact.
- Existing generated execution covers mov/add/sub/mul/fma/setp/ld/st/bar/bra/exit.
  SETP currently exposes u32/s32, eq/lt/ge and optional and (helper or/xor code
  does not establish exposed support). FMA has 70 canonical PTX spellings and
  16 old IR forms. Preserve these tests and arithmetic/MOV/memory/barrier,
  predication, prepare/commit, entry-argument and storage pipeline coverage.

- Baseline worktree began clean at the exact fetched main SHA above. Apart from
  the user-authorized nine policy files extracted byte-for-byte from `ea0dc33`,
  no original working-tree content was copied or restored; the old dirty
  migration and unrelated work remain untouched. No cherry-pick or ancestry
  import was performed.
- Frontend inventory command: existing exact-wheel Python 3.14 environment,
  `get_packaged_spec_database()` and `from_instruction_spec()` across all
  instructions; observed counts and contract ledger above.
- S1 source verification: the consolidated `python/tests` suite passed all 43
  existing tests in a fresh Python 3.14 environment. Its frontend came from a
  clean Git archive of exact `1a32fac33be61c0f540caaecf2ce14373c281b45`;
  the new editable project metadata points to this worktree's `python` project.
- S1 generated-artifact comparison: all three generators' headers and sources
  matched the pre-move `8d7b6a2` snapshot byte-for-byte after replacing only the
  generated warning's module path. The 43 test methods and existing PTX cases
  remain present; no semantic execution behavior was changed in this slice.
- S1 distribution verification: isolated setuptools/wheel builds produced a
  wheel and source distribution, then a second wheel built from that sdist.
  Both wheels contain all eight backend/template resources and exclude tests.
  The source distribution contains the same resources and retains four source
  test files. Separate fresh environments installed each wheel with normal dependency
  resolution. Both frontend `direct_url.json` records identify the declared
  GitHub URL and exact requested/actual old commit; `pip check` passed. From
  outside the checkout, installed-package paths, resource loading and all
  three console generators passed. Later wheel refreshes after resource EOL
  normalization preserve that verified dependency environment.
- S1 independent C++ verification used GCC 15.2 Debug/x64-linux, Python
  3.14.4 and the isolated prefix
  `/tmp/ptxsim-inherited-s1-build/vcpkg_installed`. Vcpkg completed successfully
  (session 41594), restoring eight ABI-exact packages from read-only cache and
  building `ptx-frontend 0.0.1b0#6` from exact REF
  `1a32fac33be61c0f540caaecf2ce14373c281b45`. Its ABI was
  `e494efa3713cb775afe3cc39e74d28f36e18377eb571a3d8109ca17b7c0475f1`;
  downloaded source SHA512
  `416e8922f10daf750b02ca5f3a80bb55d158512c41a31f9fd097d1be02c65e9d77a34bdcc9937f8069d6ce98cc05354156a4fb169a6ad9e603c91c6cde2327e3`
  matched the port. No 0.8.0 prefix/binary was reused for this slice.
- Fresh Ninja configuration in `/tmp/ptxsim-inherited-s1-build/s1-gcc-debug`
  passed with `BUILD_TESTING=ON`, `PTXSIM_BUILD_CONSUMER_TESTS=ON`, the isolated
  installed directory, manifest install disabled, and the new editable Python
  interpreter. All three codegen targets passed. Building
  `test_ptxsim_exec_ir`, `test_ptxsim_exec_ir_lowering`,
  `test_ptxsim_inst_execute_engine` and `test_ptxsim_simulator` with parallelism
  four passed all 72 build steps; `ptxsim_arith_validation` also built for
  install completeness.
- Focused CTest for those four affected executables and
  `InstExecuteEngineSemanticsLinkContract` passed **386/386**. Separate
  `^ptxsim_arith_(build-tree|installed)_consumer$` gates passed **2/2**, covering
  build-tree and installed-package consumers. These are the affected checks,
  not a claim that the full CI matrix ran.
- Incremental checks touched engine `__init__.py`, an engine template, the IR
  generator source, and binding generator source. Each invoked only its owning
  generator; all six generated artifact hashes stayed unchanged. A subsequent
  build at the same touched template mtime invoked no generator and reported
  no work; original source mtimes were restored. Shared marker/publication
  files use exact dependency paths, while recursive globs cover only each
  generator's actual import/template closure.
- C++ evidence logs are under `/tmp/ptxsim-inherited-s1-build/logs/`:
  `vcpkg-install.log`, `cmake-configure.log`, `build-codegen.log`,
  `build-tests.log`, `build-consumer-prereq.log`, `ctest-focused.log`,
  `ctest-consumers.log`, `incremental-summary.log`, and `noop-gate-summary.log`.
  Package validation used `/tmp/ptxsim-inherited-s1-venv` for source tests and
  builds, plus distinct `-wheel-env` and `-sdist-env` environments for normal
  dependency installation and installed-resource/CLI checks.
- S1 changes physical package ownership and build inputs only. The legacy
  frontend pin and direct Jinja2 declaration are explicitly transitional;
  release 0.8.0/full representation/full binding/execution migration remain
  S2-S4 obligations. Release, Clang, ASan/UBSan and the full compiler matrix
  remain S5 gates and have not been run for S1.

## Review artifact mapping

If a canonical archived review is created for this plan, use plan version 3,
milestone 1, with stages identifying the slice (for example
`v3-m1-s0-review.md`). Follow `.agents/review_policy.md` for archival provenance
and immutable historical bodies. A review message tied to the stable diff
fingerprint may provide a slice review record without creating duplicate
archives. This plan records decisions and verification; it does not grant its
implementation writer acceptance authority.
