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
| S1 | Physical Python package consolidation and CMake/CI/docs paths, retaining the exact current frontend C++/Python pin and dependency behavior; source tests, isolated wheel and sdist-built wheel resource loading, CMake input/rebuild checks | Accepted and committed `17375149` |
| S2 | Advance both frontend pins to 0.8.0; remove direct Jinja2 and separate production/build dependencies; one full projection, owned leaves and final inherited classes, closed tags and immutable program storage; exact 137/5,453/7,457 parity, all classes compiled through narrow per-opcode and bounded form/source shards, provenance/distribution/ownership checks | Accepted and committed `d1168b6`, coupled with S3 |
| S3 | Full generated frontend binding and switch to new public IR; every leaf/layout/contract covered, real resolved-module lifetime/resource/storage/relocation tests and meaningful full-binding evidence | Accepted and committed `d1168b6`, coupled with S2 |
| S4 | Capability YAML and dense typed-group dispatch, all previously supported execution behaviors preserved, temporary bridge removed; negative rule/link tests and issue-ordering/per-lane regressions | Runtime/resource/closure verified; final independent acceptance pending |
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

## Current implementation boundary

S2 and S3 are delivered as one coupled buildable slice: changing to the 0.8.0
Python APIs makes the legacy generator/binder path incompatible. One full
projection, owned inherited IR and complete frontend binding switch together;
a minimal internal engine entry adapter may preserve existing behavior until
S4 removes it. No compatibility manifest, old form alias layer or dual Python
frontend installation is introduced.

The refined Authority Sol contracts are mandatory:

- Every final form is a direct `Instruction` subclass. Base construction and
  copy/move are private to generated final friends, preventing arbitrary
  user-derived copies from forging Kind/Opcode. Generated typed queries use a
  concept over exact generated forms and one Kind comparison, without RTTI.
- `ExecutableProgram::create(const ProgramDefinition&)` freezes a fresh deep
  value snapshot. Internal generated typed-copy thunks construct actual const
  final objects, copying all nested containers and metadata tables. Private
  const storage exposes only const children; program copies share it. Caller
  instruction/container/metadata aliases remain legally mutable and cannot
  change the frozen snapshot. No public virtual clone is required.
- Register every canonical callable function, including prototypes, externs and
  aliases. A function's body is optional; declaration is distinct from a zero
  length definition. Fetching a declaration reports a structured nonexecutable
  function error. Entry launch requires a real entry definition. Declaration
  and definition parameter scopes remain distinct.
- Storage/resource, parameter, prototype, call-target-set, branch-target-set
  and call-table identities are separate strong domains. Indirect callees
  retain register/metadata/table alternatives; sparse call-table slots retain
  order, byte offsets, duplicate targets and ABI. Full signatures preserve
  roles, pointer properties and array extents, independently of launch packing.
- Owned declarations retain scalar/opaque element types, vector width, optional
  extents/alignment, external/definition, parameterized count, managed/unified
  UUIDs and opaque static objects. Sparse initialization retains Constant128 or
  typed storage/resource/function relocation targets, member/address kind,
  addend bits and mask. Runtime allocation views are derived only when supported;
  an unknown allocation is a runtime capability error, not a missing binder.
- Bind every actual nested C++ alternative, including predicate constants and
  special registers, all seven MOV sources, indirect callee and CP-async
  controls, call parameter/literal arguments, address base/index/offset and
  unified/parameter roles, texture/surface direct versus indirect resources,
  fabric counters/endpoints, stack function scope and signed TCGEN offsets.
  Immediate leaves retain type/use bits, negativity and integer source bits.
- The immutable projection separates semantic IDs, version-specific C++ access,
  owned fields/layouts, modifier domains and complete typed contracts. Layout
  fields are keyed by form/layout/field, so reused names with different storage
  are never flattened together. Static contracts may use owned constexpr tables
  shared by Kind. All emitters consume this projection, and optional/None means
  a real absence rather than a missing implementation.

## Current projection and binding refinements

Atomic selected address qualifiers are explicit instance controls outside the
74 ordinary resolved ValueKinds. The projection records their access and typed
native domain for all 99 relevant forms. Sixty-one instance controls have
non-injective frontend C++ value mappings. Their explicit equivalence/context
records keep canonical parameter/shared spaces together with the selected
parameter or atomic qualifier; a generic converter must not choose the first
source token. Native structural checks and printing must use the complete
combination. Backend vocabularies exactly cover frontend domain vocabularies;
missing/extra atoms and accidental owned enumerator collisions fail generation.

Authority classified `FunctionRef.address_availability` and
`SymbolRef.address_availability` as frontend target-legality-only metadata and
approved their deliberate omission from runtime values. Actual function/space,
parameter role, alignment, managed/unified and callable identity facts remain.
Opaque scope binding must first compare the exact source declaration scope,
kind, member and entry role before normalizing to a ResourceId and owned owner.
Stack/local allocation scope must be present and exactly equal to the current
function declaration scope, with matching Entry/Device kind and register owner,
before conversion to canonical FunctionId. Canonical aliases cannot excuse an
incorrect declaration scope. Required tests include these malformed-cache cases
and normal source-lifetime independence.

## Verification record

- S2/S3 model-stage evidence: the full immutable projection ran with
  **137 opcodes / 5,453 forms / 7,457 layouts / 48 specification operand kinds**.
  Seven focused projection tests passed, including layout-local overloaded
  names, immutable ownership, semantic/C++ name separation, selected atomic
  qualifiers, 61 source-equivalence contexts, and missing/extra/colliding
  backend-domain rejection. Early independent max review of the four model
  files found no additional blocker; its scope was model code only, not the
  complete coupled slice or actual binder behavior.
- The new production-only 0.8.0 overlay built and installed Debug and Release
  successfully in `/tmp/ptxsim-inherited-v080-build/vcpkg_installed` (session
  32444, 23 minutes). ABI
  `40895797ec15081dec8838fd9d6153981aae51a3bd9bcbd2d044cb28cfbd5afe`
  used the exact source archive SHA512 recorded in the architecture. The
  buildtree environment contained frontend 0.8.0 and its production
  dependencies, no pytest/unittest-parallel, and passed `pip check`. A separate
  package-only CMake probe found exact version 0.8.0, `resolved_ir`, and both
  configuration archives. Logs: `/tmp/ptxsim-inherited-v080-build/logs/`.
  This is dependency/package evidence, not a new root build.
- Build-floor evidence: Python 3.14.4, setuptools **80.0.0**, wheel 0.48.0
  successfully built a read-only source snapshot with `pip wheel
  --no-build-isolation --no-deps`. The tested pyproject SHA256 was
  `a30a7607f13340160adb9d367f46d0bc7b92f9b0d337af07e7f780836d131ca0`;
  its metadata retained the exact 0.8.0 wheel hash and Python >=3.14.
  `/tmp/ptxsim-buildfloor-bkwxq3/verification-summary.txt` records the inputs.
  This proves the declared backend floor, not final resources/CLI installation.
- Early inherited-IR syntax evidence: 5,453 final classes were emitted across
  342 public headers and 202 constructor/domain sources. A frozen independent
  copy passed **202/202** GCC 15.2 C++23 `-fsyntax-only` checks at parallelism
  four in 44.412 seconds, without errors or warnings. All 553 input files were
  regular files and retained their hashes. Frozen manifest SHA256
  `4d209218a5aa8239792a76c0c1136857871c1e2856ca8952a7eebe026298efe6`
  and artifact-plan SHA256
  `92e08514b0e891f2a0bb5029aa78f1a9034f6b6fae3d8fdb40381011e6258c71`
  are recorded under `/tmp/ptxsim-inherited-ir-syntax-20261110/`. This does not
  prove linkage, native static contract completeness, Program freeze/binding,
  root integration or runtime behavior; those remain in progress.
- S1 committed as `17375149f81baf1f14a799cd3c61ba37b53f43eb`, after
  independent max review and Authority acceptance of all 84 paths at
  `64adb19db344a201f6d1ce43c1b73e950e46fb844e892899c1c15392792fa43d`.
  Git independently matched the staged F/D manifest fingerprint; the worktree
  was clean. S1's validated legacy environments remain baseline evidence.
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
  does not establish exposed support). The legacy baseline had 70 accepted
  FMA spellings and 16 old IR forms; the explicit user retirement below removes
  only the two combinations rejected by frontend 0.8. Preserve the other
  arithmetic/MOV/memory/barrier, predication, prepare/commit, entry-argument
  and storage pipeline coverage.

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

## Coupled S2/S3 verification record

S2/S3 was independently accepted and committed as `d1168b6`. Its temporary
engine registry preserved the existing families until the S4 replacement. The following later evidence supersedes the limitations of the
early syntax-only inventory above only within each stated check's scope.

- The exact 0.8.0 production port built and installed both Debug and Release in
  `/tmp/ptxsim-inherited-v080-build/vcpkg_installed`. ABI
  `40895797ec15081dec8838fd9d6153981aae51a3bd9bcbd2d044cb28cfbd5afe`
  and the source SHA512 above matched. Its isolated production environment
  contains no pytest or unittest-parallel; `pip check` and the exported CMake
  version/target probe passed. This does not substitute for root integration.
- Python 3.14.4 with the declared minimum `setuptools==80.0.0` built the wheel
  successfully. The bounded backend-floor result is recorded in
  `/tmp/ptxsim-buildfloor-bkwxq3/verification-summary.txt`; normal dependency
  installation and runtime resource validation are separate checks.
- The frozen actual IR/Program gate compiled 207 translation units, linked all
  real objects without stubs or undefined-symbol suppression, and passed six
  lifetime/operand tests. It checked all three then-existing 5,453-slot dense
  tables and unchanged hashes of all 559 frozen inputs. Evidence:
  `/tmp/ptxsim-actual-ir-program-gate-20261110/actual-ir-program-gate-summary.md`.
- The subsequent descriptor gate compiled 208 translation units and linked
  real native getters; eight tests passed. It checked 5,453 getters, 7,457
  layouts, 31,154 scoped binding joins and four complete dense tables. The
  signed range tests accept -8/-1/0/7 and reject -9/8. Evidence:
  `/tmp/ptxsim-native-descriptor-gate-20261110/native-static-descriptor-gate-summary.md`.
- The full cold bridge linked 412 real objects with the exact frontend archives.
  After repairing two invalid new fixtures, ten tests passed. Independent
  review then identified external natural/explicit alignment canonicalization
  and indexed-special-register domain checks; their repairs passed all fourteen
  tests with the original failing and successful evidence retained separately.
  Evidence: `/tmp/ptxsim-binding-compile-gate-20261110/repair/summary.md`.
- The initial frozen root production build completed all 544 Ninja steps and
  nine native archives with tests disabled. A separate cleanup comparison ran
  all six actual generator CLIs and proved byte equality of 839 artifacts.
  That comparison predates the later intrinsic/predicate/offset repairs and
  must not be cited as verification of those changes. Evidence roots:
  `/tmp/ptxsim-root-nontest-fullbuild-20261110/` and
  `/tmp/ptxsim-codegen-equivalence-20261110/`.
- The later production repair is frozen by
  `/tmp/ptxsim-production-repair-manifest.json`, SHA256
  `c31cfa596f4e26318e318821dc0ada3232fa7e48954051716752de602055f641`.
  Its 189 current inputs and 18 deleted legacy paths were verified before and
  after copying. All nine production targets completed the 544-step build
  after a fresh `BUILD_TESTING=ON` configuration in
  `/tmp/ptxsim-root-repair-integrated-20261110/`. Independent source reviews
  resolved the scoped entry-ABI join, predicate-output validation, closed
  intrinsic admission and shared checked signed-offset findings; these source
  conclusions do not replace the pending integrated runtime regressions.
- A normal installed wheel from that frozen production snapshot passed all
  55 Python tests in 60.433 seconds, with zero failures, errors or skips.
  Production/test source hashes and installed-package provenance were checked.
  Evidence: `/tmp/ptxsim-current-python-suite-verification.md`. A wrapper's
  initial inclusion of generated bytecode in its input count was diagnosed
  separately; no source drift or test rerun occurred.
- The strengthened seventeen-test regression input set is frozen separately
  under the repaired root gate's `regression-inputs-v2`. It independently
  checks signed-offset memory locations, matching intrinsic types, scoped ABI
  joins and legal source inversion. Its real seventeen-case run passed sixteen
  tests; the only failure was a new test incorrectly treating a vector entry's
  known eight-byte packing as unavailable. The V3 correction retains that
  source as a positive pack/run test and separately tests a valid native body
  that explicitly provides no supported packed-byte view. The original failed
  V2 evidence remains separate; the corrected cases passed in the subsequent
  integrated runs described below.
- A subsequent narrow `exec_ir.cpp` repair validates all five parameter roles,
  two resource roles and three opaque kinds before interpreting derived views.
  Unknown enum values are structural metadata errors even when packing is
  unavailable. Independent source review resolved this finding; the source hash
  is `02c3ab472091fae13145bf7aaf15411707b4aca79290426e6bbdaa2edbd7cef6`.
  Its three new metadata tests and restored expected/fetch guards are recorded
  in `/tmp/ptxsim-metadata-domain-repair-packet.json`. The c31 production build
  above predates this patch; the subsequent combined incremental build and
  runtime runs below cover it. The original source/object/archive are preserved
  separately by the verifier before updating its configured temporary tree.
- The remaining integrated test client migration is a validation candidate,
  recorded in `/tmp/ptxsim-integrated-test-candidate-manifest.json`. All ninety
  engine cases remain; after the user's explicit retirement below, FMA retains
  68 numeric execution cases, both retired source spellings as rejection tests,
  and all sixteen native semantic identities. Sparse storage/relocation binding
  assertions now inspect complete owned metadata, with allocation restrictions
  retained at runtime. Exact printer expectations were refreshed against real
  output without dropping the numeric/PTX obligations. Consumers, compiler
  matrix and sanitizers remain later gates; no complete-goal acceptance is
  claimed.

The first complete GCC Debug test run reached all 681 CTest cases: 635 passed,
46 failed, and none were skipped or left unrun. All twenty-one scoped
metadata/issue/launch regressions passed in that run. The full log and parsed
result are under `/tmp/ptxsim-root-repair-integrated-20261110/`, as
`fullsuite-ctest-v2.log` and `fullsuite-ctest-v2-result.json`. The earlier build's
single old variant-equality client error was repaired by a constrained native
mixed-form comparison; every arithmetic input and expected result remains.

The next candidate repairs physical immediate use-bit/provenance consistency,
formal/definition scope caches, sparse initializer cell shape, and unsigned
32-bit numeric base decoding, plus the independent exact printer expectations.
MOV shape validation is separated from implementation admission using the
owned layout-local compatibility facts. Focused source and generation checks
are recorded in `/tmp/ptxsim-post681-combined-repair-packet.json`; its
incremental default build completed successfully. The subsequent unfiltered
run reached all 689 cases: 687 passed, two failed, and none were
skipped or left unrun. Evidence is `post681-final-fullsuite-ctest.log` in the
same repaired root gate. The retired FMA source-rejection regression passed.
No fresh full-suite success is claimed.

The user resolved the pinned-frontend conflict on 2026-11-10: “明确移除那两种新
frontend 不再支持的组合”. Only `fma.rn.oob.sat.f16` and
`fma.rn.oob.sat.f16x2` are retired from PTX/native execution. Their original
public-ABI kernel spellings remain explicit parse-success/resolve-rejection
regressions, with the exact 0.8 modifier-combination diagnostic. The other 68
numeric execution cases keep their original inputs and expected bits; all 16
native form identities remain covered. No frontend patch, legacy parser or
native extension restores the removed controls. Generic low-level arithmetic
primitives are unchanged. This is an explicit user scope exception, not an
implementation fallback or permission to remove any other old case. The
parameterized execution suite loses two cases and gains one rejection test
that checks both strings, so this file's CTest count changes by minus one.


The final two failures are isolated in
`/tmp/ptxsim-final-two-repairs-packet.json`. The storage cold binder now checks
that nonempty scalar cells require explicit initialization, and that constants
fit the physical scalar container with a zero high word through 64 bits. This
preserves sparse layouts, relocations and the existing B128 positive cases.
The MOV helper test now respects its real compatibility reference, which spans
all three layouts, and separately checks an owned copy with a restricted
layout link. Both changed translation units passed syntax checks. The same configured
build directory then completed its incremental default/all build successfully
(session 18685). All 171 tests in the affected lowering and engine executables
passed: 54 lowering and 117 engine, with zero failures, skips or unrun tests
(session 63247, 1.56 seconds). Both previously failing cases passed. Evidence:
`final-two-repairs-affected-ctest-result.json` and its selection ledger/log in
the repaired root gate. Its current 189-production-input fingerprint is
`41711db9b61b9e641738985f63a9a6716b42bb7afb1fb28f2e4c8cfdb8717dfa`.
The preceding complete 689-case run remains a 687/2 result. Together these
checks cover the final changes and the unaffected earlier results; they are
not a fresh passing run of all 689 cases.

The fresh normal installed wheel after the MOV generator repair passed all 55
Python tests in 72.390 seconds with zero failures, errors or skips. Installed
source provenance and all 23 Python production inputs matched. Evidence:
`/tmp/ptxsim-post681-python-suite-verification.md`. The final two repairs change
only native C++ and one C++ test; they do not invalidate that Python input set.


A separate distribution gate built an sdist and then a wheel from that sdist,
installed the wheel with normal dependency resolution into an independent
runtime environment, and verified package provenance plus `pip check`. The
32-file source snapshot fingerprint is
`e37f84dd469d0f87ea1c2e0fd56a7a521e11a111151d1044ad49743cc9d8017f`;
the sdist SHA256 is
`aba1075476e9b196991c9d35ad23e9adcd8beac61ebb1434b19c1b194c991579`,
and the resulting wheel SHA256 is
`07366d0c046f16e7b824cccbfbbd9811a678dbc832e9e862413ef50a5c57b814`.
All 23 runtime modules and `backend.yaml` retained their exact source bytes.
The wheel excludes tests, caches and retired generator modules; the sdist
legitimately includes six source test files. All three installed CLI commands
completed successfully: IR produced 343 headers and 203 sources, lowering two
headers and 203 sources, and engine one header and 87 sources. The engine table
contains all 5,453 distinct dense entries. Evidence:
`/tmp/ptxsim-s2-distribution-20261110.Wba24P/summary.json`.

The coupled S2/S3 candidate received final independent actual-diff and Authority
acceptance and was committed as `d1168b6`; the accepted 111-path raw manifest
fingerprint is `f792a5391e7984e6e3d5f5b2cea10882b981cfab446a45e1a9fe005912e90b35`. The official execution capability YAML and deferred-successor ordering are
verified below in S4, including GCC Debug consumer/install integration.
Remaining compiler/configuration and sanitizer rows are S5. The bounded GCC Debug, Python, distribution and earlier
ownership/binding checks above do not establish those later outcomes.

## S4 verified candidate, acceptance pending

The packaged `execution.yaml` replaces the temporary form registry with 83
layout rules covering the same 67 implemented forms, including all 16 FMA
identities. The full 137/5,453/7,457 projection and cold binding are unchanged.
Rules resolve exact semantic form/layout/control identities and closed
layout-local operand categories, intrinsic identities, qualifiers and true
optional presence. Unknown identities, values, selector paths and overlaps
fail generation. Ordinary values, register widths/contents and resource IDs
are excluded. Profiles choose or restrict a handler and cannot add semantics
by widening values. Known-handler bounds remain before continuation/predicate/lane
activity; custom relaxed-memory and XOR-SETP profiles have focused generation
regressions. Transfer-size and sink checks stay local shape preconditions.
Omitted MOV forms still receive immutable shape validation before unsupported
rejection.
The dense table retains one Kind decode and one indirect typed-group call;
all admission stays inline before participating-lane loops.

The owning successor context and separate structured program/execution error
variants implement the adopted ordering above. Simulator no longer performs
an opcode classification or propagates fallthrough errors before admission.
The old optional-PC call remains available without overload ambiguity.
A paired link fixture shares one generated object between a positive executable
that supplies the exact typed handler and a negative executable that omits only
that definition. Actual runtime, positive/negative links, installed resources
and input closure now pass within the scopes below. This candidate still
requires final independent Max review and Authority acceptance; its
implementation writer does not grant that acceptance.

Owner checks for this candidate used private output directories. The existing
33 engine-generator tests passed; a later whole Python run passed all 64 cases
in 65.890 seconds after the boolean and predicate-constant selector corrections.
The final focused capability suite passed all 13 cases in 12.366 seconds,
including conditional-intrinsic overlap, shared layout-local C++ spelling and
the two wider-profile handler-bound regressions requested in review.
The current whole-suite inventory is 68 tests; no new whole-suite run is
claimed after those focused additions. The initial 92-TU syntax batch passed
89 and exposed only new selector/test-client mistakes. Narrow retries fixed those inputs: unchanged
generated files were compared byte-for-byte rather than recompiled. All 87
generated sources, changed native sources and test translation units then
passed syntax checks within their recorded input scopes. These early checks
establish compilation only; the later independent gates below establish actual
runtime, links, installed resources and closure. Evidence is under `/tmp/ptxsim-s4-*` and is indexed in
`/tmp/ptxsim-s4-implementation-progress.md`.

Independent verification completed against the frozen candidate and narrowly
repaired fixtures:

- The first unfiltered GCC Debug run reached all 700 CTest cases: 697 passed
  and three failed, with none skipped. Both external consumers stopped during
  frontend resolution because their entry-body .param had no PTX version;
  this precedes ptxsim lowering or execution. The required-handler positive
  link also exposed its omitted real issue_validation.cpp dependency.
  Those failed binaries, objects, sources and logs remain separate evidence.
- The consumer keeps every original instruction, its 2x3 scratch array, three
  parameter roles/extents, 12-byte entry packing and exact fused-result bits.
  It now supplies .version 8.0 to select the expected modern BodyLocal role
  and prints frontend resolution errors. Pinned upstream production requires
  this context to distinguish legacy entry inputs from body-local parameters;
  no target/SM value or new execution semantics was introduced. The link
  fixture adds the real issue_validation.cpp to the object library shared by
  positive and negative controls; only the typed handler definition differs.
- The subsequent actual default/all build and unfiltered GCC Debug CTest pass.
  All 700 tests passed, with zero failures or skips, in 345.52 seconds
  (session 99327). Both actual build-tree and separately installed consumers,
  the existing missing-Add contract, and the required-handler positive plus
  exact-symbol negative contract passed. This is the repaired full-suite
  result, not a private diagnostic or a filtered replacement.
- Final evidence is in
  /tmp/ptxsim-root-repair-integrated-20261110/s4-final-posthash.json
  (SHA256 f97ff6280e6fc49fbbb92a7ac39ca09ae0153d83eb7a5928bca578d398336c4a).
  Its checks all pass, verification_complete is true, nine primary archives
  and nine selected objects match the pre-fixture-repair versions, and runtime
  plus CI differences against the live source are zero. Companion records are
  s4-final-source-manifest.json, s4-final-codegen-manifest.json and
  s4-consumer-overlay-ctest-result.json in that gate directory. Later docs and
  the two added Python test cases are accounted for separately.
- The normal PEP 517 sdist-built wheel installed with dependency resolution
  into an independent Python 3.14.4 environment. The pinned frontend URL/hash,
  installed provenance and pip check passed. All 24 runtime modules, both YAML
  resources and pyproject metadata match the final runtime inputs; no tests,
  caches or retired modules enter the wheel. All three installed CLI commands
  passed, retaining 5,453 dispatch entries, 83 layout rules and 67 implemented
  forms. All 751 generated IR/lowering files are byte-identical to S2/S3.
- Independent Python testing passed the original frozen full 66 cases in
  69.662 seconds, then the updated capability suite's 13 cases in 10.097
  seconds, with zero failures, errors or skips. Together they cover the 68
  distinct current cases; this is not a fresh complete 68-case run. Evidence:
  /tmp/ptxsim-s4-python-dist-20261110-xwyes7n3/summary.json. The original sdist
  SHA256 is 995444d01f42542c971bbea350de47331760bbeb720fa4a32da1b20a263d0f39;
  its wheel SHA256 is
  c09cc1cffb2a618cc04dc6e4b215bc22dda98ae9f7b1ffbcc34abc2599dbc2e7.
  These artifacts retain their original README/test snapshot scope; subsequent
  docs/test-only changes do not alter production modules, resources or metadata.
  The sdist includes seven test modules; the separate CMake-only test helper
  remains in the repository and is not an installed runtime dependency.
- The execution YAML trigger/no-op gate passes all 16 checks. Only engine
  generation ran; IR and lowering were not invoked and their output bytes
  stayed unchanged. The same next build reported no work, and the YAML bytes
  and original access/modification times were restored. Record:
  s4-execution-yaml-trigger/trigger-noop-report.json under the repaired gate.

The user's final layout exception keeps scripts used exclusively by CI in
.github/scripts/. The attempted CI move was fully withdrawn: both scripts retain
their original bytes/modes and the action/CONTRIBUTING references match HEAD.
The current audit finds 32 project Python source/test/helper files under python/
and only the two permitted CI scripts outside it. Submodules have no Python,
Jinja or legacy Python/template/cache directory residue. The README states this
boundary; no historical review body was rewritten and no new package dependency
or schema was added.

Authority fixed the final S5 gate scope in
`/tmp/ptxsim-s5-authority-gate-decision.md`: GCC and Clang Debug/Release plus
GCC Debug with combined ASan+UBSan, each default/all build and unfiltered CTest
including build-tree and installed consumers. The exact existing frontend
Debug/Release prefix may be reused on this host with libstdc++; actual links
and runs must prove compatibility, without claiming that external frontend
archives are sanitizer-instrumented. The new input-closure check only needs
execution YAML to trigger engine generation while IR/binding remain unchanged,
followed by a true next-build no-op; that new check now passes as recorded
above. Earlier S1 closure evidence is retained.

Per the user's later path convention, subsequent S5 builds/tests use the actual
worktree presets at out/build/<preset> and out/install/<preset>, without copying
the source/build for each configuration into /tmp. The already passing S4
temporary gate is preserved as evidence and is not moved or rebuilt merely to
change its path. Source hashes are frozen and checked before/after subsequent
preset runs. The verified GCC Debug row may be reused; the remaining compiler
and sanitizer rows are not yet claimed passed. S5 and whole-goal acceptance
remain pending.

## Review artifact mapping

If a canonical archived review is created for this plan, use plan version 3,
milestone 1, with stages identifying the slice (for example
`v3-m1-s0-review.md`). Follow `.agents/review_policy.md` for archival provenance
and immutable historical bodies. A review message tied to the stable diff
fingerprint may provide a slice review record without creating duplicate
archives. This plan records decisions and verification; it does not grant its
implementation writer acceptance authority.
