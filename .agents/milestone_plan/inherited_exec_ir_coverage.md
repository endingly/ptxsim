# Inherited executable IR coverage and delivery evidence

This ledger distinguishes full representation and binding from implemented
execution. It accompanies the [adopted architecture](../arch/inherited_exec_ir.md)
and [delivery plan](inherited_exec_ir_execution_plan.md). Independent Max and
Authority Sol review final acceptance; this implementation ledger does not grant it.

## Complete model, native representation and binding

| Obligation | Delivered boundary and evidence |
| --- | --- |
| Released frontend | C++ source tag `v0.8.0`, commit `14879089d2a0f8c5fcb962c621c43b6dc3de193c`, and exact hashed Python wheel. Source and wheel hashes are recorded in the delivery plan and enforced by the port/package metadata. Debug and Release production installations were verified without frontend test dependencies. |
| Full projection | 137 opcodes, 5,453 semantic forms and 7,457 layouts. All 48 specification operand kinds normalize to 41 resolved operand categories; all 74 value-kind mappings are checked. [Snapshot tests](../../python/tests/test_snapshot.py) check exact counts, dense identities, layout-local field paths and missing-map failure. Capability profiles do not filter this projection. |
| Inherited native IR | Every form has a direct final class, fixed readonly dense Kind/Opcode, typed owned fields and layouts, constrained typed queries and closed base construction. All generated form sources compile; positive and negative public-header probes verify tag/type closure. No executor resides in the IR. |
| Full contracts | Native typed static arrays/getters cover 5,453 forms, 7,457 layouts and 31,154 operand-binding joins. [Native contract tests](../../submod/exec_ir/test/test_native_contracts.cpp) query all forms/layouts/bindings and signed bounds. [Generator tests](../../python/tests/test_native_contracts.py) cover scoped references, typed absence, signed/unsigned representation and indirect-resource admission. |
| Full cold binding | All 203 generated bridge translation units compile and link. Exact known-kind dynamic-class checks precede field access; cold module, scope, symbol and cache checks precede publication. Actual source reviews and full integration tests establish populated owned leaves/tables, rather than merely naming each leaf type. Missing binding is an incomplete implementation, never execution unsupported. |
| Owned identity and metadata | Registers retain actual component slots, declaration widths and lanes. Canonical functions, declaration-specific parameters, resources/storage, prototypes, target sets and sparse call tables have separate owned IDs. Full ABI, aliases, order/duplicates, Constant128, sparse typed relocations, UUIDs and opaque static objects survive frontend destruction. Bodyless declarations and empty definitions remain distinct. |
| Immutable Program | Creation deep-copies final instructions and nested metadata into an independent const snapshot. Program copies share that const storage. Lifecycle tests retain and mutate caller aliases, destroy source inputs, then fetch/print/validate through Program copies. Container and ABI joins are checked without eager ordinary operand lookups. |

The early leaf and record crosswalks are shape inventories. They are not standalone
proof that binders populate every field. The accepted full cold-binder/Program
source review, compiled and linked all-kind bridges, ownership regressions and
current full integration suites jointly provide that evidence. The descriptor
link/runtime report is
`/tmp/ptxsim-native-descriptor-gate-20261110/native-static-descriptor-gate-summary.md`;
accepted slice records and exact input scopes are indexed in the delivery plan.

Only diagnostic/source spelling and individually approved target-legality
predicates are deliberately omitted from native runtime records. Python retains
the complete frozen facts. Native domains/defaults, selected controls,
CC/completion/WGMMA/matrix/tensor contracts, effective types/widths, field links,
parameter direction/entry role, sink/token/shape constraints, modern vector
semantics, MMIO membership and texture/surface indirect admission remain owned.
Surface's present empty gate still admits indirect access; texture's absent gate
does not. No frontend object, borrowed frontend span, JSON blob or monostate
placeholder substitutes for execution-related data.

## Execution and preserved behavior

The packaged [execution profile](../../python/src/ptxsim_codegen/resources/execution.yaml)
registers 83 layout rules for **67 implemented forms**, using six handler families.
The dense dispatch table still contains all 5,453 forms; all other entries report
unsupported at actual issue. Unknown identities/selectors, overlapping rules and
missing typed-handler symbols have generation or real link negative tests.
Profiles can restrict or select existing semantics; widening them cannot create
new handler capabilities. Ordinary numeric values, runtime IDs or resource lookups
are not capability selectors.

Each issue decodes Kind once and makes one indirect typed-group call, with lane
loops inside the group. Structural checks precede static capability admission;
unsupported precedes successor errors, predicate reads, ordinary operand reads
and mutation. Owning deferred successor errors preserve ProgramError identity.
Supported instructions retain lazy predication, lane faults, prepare-all-before-
commit and existing branch/exit/barrier behavior. Direct and Simulator regressions
cover terminal instructions, false guards, unavailable resources and exact error
ordering. Unexecuted unsupported forms do not reject a whole Program.

The only user-authorized retirement is `fma.rn.oob.sat.f16` and its `f16x2`
counterpart, which released frontend 0.8 rejects and cannot represent together.
Both original complete kernels remain exact source-rejection tests. The other
68 numerical FMA cases and all 16 native FMA identities remain covered. No legacy
parser, frontend patch or simulator-only extension was added. Existing arithmetic,
MOV, memory, predicate, control-flow, synchronization, ABI and storage obligations
remain; full representation does not imply simulation of newly represented forms.

## Packaging, build closure and verification limits

Project Python implementation, generated resources, tests and test helpers live
under root `python/`; two CI-only scripts remain in `.github/scripts/` by explicit
user exception. Submodules contain C++ and CMake, without Python/template residue.
Python requires 3.14; setuptools 80 and wheel are build dependencies, with an actual
80.0.0 floor build. Runtime metadata declares only the exact frontend wheel;
its production dependencies supply PyYAML/Jinja2/jsonschema. This project has no
direct Jinja2 dependency and unittest needs no pytest dependency.

Normal sdist-to-wheel build, dependency-resolved installation, provenance,
`pip check`, 24 installed runtime modules, both YAML resources and three installed
CLIs passed. Full 66 Python tests plus the later complementary 13 capability tests
cover **68 distinct tests**, not a fresh full 68-test run. Their original README/test
artifact scope is preserved; installed production bytes and metadata match the
accepted runtime. The 751 IR/lowering outputs are byte-identical to S2/S3.
Execution-profile input closure passed all 16 checks: engine-only regeneration,
unchanged IR/binding outputs and a genuine next-build no-op.

The five actual compiler/configuration rows and exact report paths appear in the
delivery plan. Local preset runs use `out/build/<preset>` and
`out/install/<preset>`; the earlier successful GCC Debug gate retains its original
private output directory. The exact frontend Debug/Release prefix is reused on the
same host/libstdc++; successful links/runs verify that ABI combination. Project
ASan+UBSan instrumentation does not imply instrumented external frontend archives.
Remote GitHub CI was not executed. Clang emitted bounded switch/style warnings;
read-only diagnosis found explicit unsupported/error fallbacks and unambiguous
control flow, as recorded in the plan. No warning-free claim is made.

All changes belong to the isolated branch/worktree. The original dirty main
worktree is preserved, and nothing has been pushed. Final independent actual-diff
and Authority whole-goal acceptance remain separate from implementation reporting.
