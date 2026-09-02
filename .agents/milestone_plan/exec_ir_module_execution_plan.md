# PTXSim `exec_ir` Module Execution Plan

> **Status:** WP0 generator contract probe and executor Gate A complete; WP1 ready for implementation
> **Working branch:** `feat/exec-ir-wp0`
> **Frontend baseline:** `ptx_frontend` commit `3458bc53eacbc051d3ba4e2685c59aced4bf50af` / PTX ISA 9.3
> **Language/build:** C++23 / CMake / Python code generation
> **Primary objective:** define a compact, frontend-independent execution IR without copying PTX instruction facts or generated frontend sources.

---

## 1. Boundary decision

`exec_ir` owns normalized facts required to execute the supported PTX subset
after frontend validation and one immutable dense instruction sequence. It does
not own PTX parsing, source syntax, target availability checks, runtime
topology, mutable execution state, storage, scheduling, or instruction
execution.

```text
                         build time only
ptx_frontend wheel ───────────────────────────────┐
  ptx_spec YAML + normalized InstructionSpec      │
                                                  v
ptxsim support manifest ───────────────> ptxsim exec_ir emitter
                                                  │
                                                  v
                                      generated build-tree proof header (WP0)
                                      generated exec_ir C++ types (WP1)

                         runtime/library targets
common ────────────────────────────────> exec_ir::InstructionStream

ptx_frontend::resolved_ir ──> optional frontend adapter ──> exec_ir

InstructionStream + runtime + executor ──> future simulator
```

Required dependency rules:

```text
exec_ir -> common
exec_ir -X-> ptx_frontend
exec_ir -X-> arith
exec_ir -X-> memory
exec_ir -X-> execution_model
exec_ir -X-> runtime

frontend adapter -> ptx_frontend::resolved_ir + exec_ir
executor -> exec_ir + arith + runtime
future simulator -> exec_ir + executor + runtime
```

The wheel is a host/build dependency. It must not appear in installed `exec_ir` headers or in an `exec_ir` link interface.

---

## 2. `ptx_frontend` reuse audit

Pinned generator-package dependency:

```text
ptx_frontend @ git+https://github.com/endingly/ptx_frontend.git@3458bc53eacbc051d3ba4e2685c59aced4bf50af#subdirectory=python
```

The wheel provides:

- canonical `ptx-instr/v1` YAML and its schema;
- schema validation and normalization;
- `CodegenDatabase` / `InstructionSpec` / variant, modifier, and operand models;
- a frontend-specific C++ generator;
- `PyYAML>=6.0,<7` and `jsonschema>=4.26,<5` dependencies.

The existing `ptx_frontend_generate()` function must not be used for `exec_ir` generation. Its backend YAML changes value spellings, but its emitters and output topology are fixed to frontend `resolved_ir`, descriptor, resolver, and checker artifacts. Generated headers also include frontend C++ headers and use frontend storage types.

There is also a release-version mismatch at this baseline:

```text
CMake package version: 0.0.1
wheel version:         0.0.1b0
```

The installed `codegen` component requires exact equality and therefore rejects this wheel. `exec_ir` generation will invoke a ptxsim-owned script in the project-selected Python environment instead. The generator package metadata fixes the VCS source revision; `requirements.txt` is only the local editable environment entry point, and neither fact is repeated in the manifest or CMake. CMake must not install Python packages or access the network.

The current Python modules are top-level `code_gen` and `ir`, not a stable `ptx_frontend.*` facade. All imports from them must remain isolated in one ptxsim script. A later stable frontend API should require changing that script only.

---

## 3. Single source of truth

Do not copy any of the following into ptxsim YAML:

- PTX syntax;
- operand layouts;
- modifier defaults or legal values;
- target availability;
- address/type constraints;
- frontend variant definitions.

Those facts remain owned by the wheel's canonical `ptx_spec` and normalized model.

The ptxsim manifest owns only execution support and semantic mapping:

```yaml
schema: ptxsim-exec-ir/v1
ptx_spec_schema: ptx-instr/v1

instructions:
  - opcode: add
    operation: add
    forms:
      - variant: add_integer_no_sat
        layouts: [default]
        modifier_values:
          type: [u32]
```

`forms`, `layouts`, and `modifier_values` select a subset of facts already defined by the frontend; they do not redefine legality. Unknown opcodes, variants, layouts, modifier/value selectors, duplicate mappings, and an unexpected frontend spec schema are generation errors.

Do not add a generic templating language, plugin API, inheritance hierarchy, or per-field C++ snippets. Add manifest fields only when a supported operation cannot be represented without them.

The Python emitter owns one closed mapping from normalized frontend modifier/operand `kind` values to handwritten ptxsim primitives. It must reject an unmapped kind; it must not silently fall back to a generic operand.

---

## 4. Generated and handwritten boundary

### Generated C++

Generate only repetitive structural declarations and dispatch glue:

- one plain record per supported opcode;
- one `Op` enumerator per opcode, never per data type, modifier, or frontend
  variant;
- a bounded `std::variant` over the generated operation records;
- deterministic `Operation -> Op` identity;
- a static, closed first-level dispatch list consumed by executor code;
- optional compile-time metadata that is directly consumed by the adapter or executor.

Generated files live outside the source tree and are never committed. The public generated header is copied from the build tree into the install tree as part of the `ptxsim::exec_ir` package; private generated artifacts, if any, are not installed.

### Handwritten C++

Handwrite stable semantic primitives and behavior:

- `RegisterRef` using `common::RegisterSlot`;
- predicate reference plus negation;
- typed immediate storage using `common::RawValue`;
- the minimal runtime scalar/type descriptor required by the first operation;
- structural validation returning `std::expected`;
- frontend-to-exec conversion;
- execution behavior and all interaction with `arith`, `runtime`, or memory.

Generated code must not contain:

- `ptx_frontend` types, includes, strings, source ranges, or symbol IDs;
- memory handles or topology IDs;
- arithmetic implementations;
- PC mutation, storage writes, barriers, or async behavior;
- a module/program image, loader, call stack, or source map.

These exclusions keep regeneration mechanical and keep execution policy reviewable in handwritten code.

### Gate A C++ shape

The executor probes require the equivalent of this deliberately narrow model:

```cpp
struct RegisterRef {
  common::RegisterSlot slot;
};

struct PredicateRef {
  common::RegisterSlot slot;
  bool negated = false;
};

using ScalarSource = std::variant<RegisterRef, common::RawValue>;

enum class Op {
  mov,
  add,
  bra,
  exit,
};

enum class ScalarType {
  b32,
  u32,
};

struct Move {
  ScalarType type;
  RegisterRef source;
  RegisterRef destination;
};

struct Add {
  ScalarType type;
  RegisterRef destination;
  ScalarSource left;
  ScalarSource right;
};

struct Branch {
  common::ProgramCounter target;
};

struct Exit {};

using Operation = std::variant<Move, Add, Branch, Exit>;

[[nodiscard]] Op op(const Operation&) noexcept;

struct Instruction {
  std::optional<PredicateRef> predicate;
  Operation operation;
};

class InstructionStream {
 public:
  // Validated once, then immutable for Simulator lifetime.
  [[nodiscard]] static auto create(std::vector<Instruction> instructions)
      -> std::expected<InstructionStream, StreamError>;

  [[nodiscard]] auto fetch(common::ProgramCounter pc) const
      -> std::expected<std::reference_wrapper<const Instruction>, StreamError>;
  [[nodiscard]] auto fallthrough(common::ProgramCounter pc) const
      -> std::expected<common::ProgramCounter, StreamError>;

 private:
  std::vector<Instruction> instructions_;
};
```

`Op` contains only opcode identity. Data type, execution-relevant modifiers,
and operand form are fields of that opcode's record and participate only in
secondary dispatch. Do not generate cross-product enumerators such as
`add_u32`, `add_s32_sat`, or `mov_b32`.

The operation records, `Operation`, `Op`, `op()`, and closed first-level
dispatch list are generated. Operand primitives, scalar/control primitives,
the `Instruction` envelope, `InstructionStream`, validation, and execution are
handwritten. `Op` is derived from `Operation`; it is not stored as a second tag
that could disagree with the variant alternative.

The initial move remains register-to-register and supports only `b32`; the
initial add supports only wrapping `u32` with register/immediate sources.
Those restrictions are validated when the stream is created rather than
encoded into top-level operation names.

### Gate A consumer audit

The executor WP1-WP3 probes establish the following contract:

- operands own either a `common::RegisterSlot` reference or, where the
  operation permits it, a `common::RawValue` immediate;
- predicate reference and negation belong to the common instruction envelope,
  because move, add, branch, and exit use the same operand-gating rule;
- `Thread` remains the sole authoritative owner of its current PC;
- `InstructionStream` defines PC as a dense vector index, fetches by the
  scheduler-selected `WarpIssueGroup::pc`, and derives fallthrough as the next
  validated index; only the direct branch target is stored in `Branch`;
- Simulator owns the immutable stream for its lifetime, performs fetch, and
  passes the already-fetched instruction plus derived fallthrough to executor;
- first-level dispatch uses only the pure `Op`; each opcode handler may perform
  a second dispatch over normalized data type, form, or modifier controls;
- dispatch is closed and static, not an opcode string, dynamic handler
  registry, factory, or virtual interface;
- the operation records are generated structure, while operand primitives,
  the instruction envelope, validation, and execution remain handwritten;
- executor receives one already-fetched `Instruction`; it does not own the
  stream, label lookup, fetch policy, or reconvergence policy;
- `FunctionId`, `LaunchRuntime`, `arith::context`, Warp/Thread objects, register
  views, prepared writes, and prepared control effects remain executor inputs
  or transient state and never enter `exec_ir`.

Predicated-off `Exit` proves that Simulator must provide a valid successor even
when an active `Exit` does not consume it. A frontend adapter must resolve a
direct label to a dense `ProgramCounter` before constructing `Branch`.
Function ranges, symbol/source metadata, and call layout remain deferred; the
initial stream is not a return to the retired multi-module `ProgramImage`.

---

## 5. Initial repository shape

Create files only as their work package begins:

```text
submod/exec_ir/
├── CMakeLists.txt
├── include/
│   ├── exec_ir.hpp
│   ├── instruction_stream.hpp
│   ├── operand.hpp
│   └── validation.hpp
├── python/
│   ├── pyproject.toml
│   ├── setup.cfg
│   ├── codegen/
│   │   ├── __init__.py
│   │   ├── __main__.py
│   │   ├── generate.py
│   │   └── test_generate.py
│   └── instructions/
│       ├── __init__.py
│       └── exec_ir.yaml
└── test/
    └── test_exec_ir.cpp

cmake/
└── generate_exec_ir.cmake

requirements.txt
```

The WP0 generator is the independent `ptxsim-exec-ir-codegen` distribution. Its import package is `ptxsim_exec_ir_codegen`; explicit setuptools `package_dir` mappings preserve the physical `codegen/` and sibling `instructions/` directories. The bundled manifest is installed as package data and is the default `python -m ptxsim_exec_ir_codegen` input. One generator module remains enough until code generation develops reusable internal modules.

`submod/exec_ir/python/setup.cfg` is the single source of truth for the `ptx_frontend` VCS commit. `requirements.txt` installs the local editable `ptxsim-exec-ir-codegen` package, whose metadata resolves that pinned dependency. Its transitive Python dependencies may initially follow the bounds declared by the installed package; add a full hash lock only when reproducible/offline packaging requires it.

The handwritten `exec_ir.hpp` facade includes the generated public header through the same `ptxsim/exec_ir/...` path in build-tree and installed-package consumers. CMake must register both include roots and explicitly install the generated header; source-header installation alone is insufficient.

---

## 6. Work packages

### WP0 — Generator contract probe (implemented)

Goal: prove that the Python environment prepared from `requirements.txt` can supply normalized frontend facts without using the wheel's fixed C++ emitter.

Required behavior:

1. import the required `code_gen` model API from the selected Python environment;
2. locate the packaged `code_gen.resources/ptx_spec` with `importlib.resources`;
3. load it through `code_gen.database.load_codegen_database`;
4. verify `ptx-instr/v1`;
5. resolve the selected `add_integer_no_sat/default/u32` form;
6. emit a deterministic, minimal C++ proof header to the caller's explicit output path;
7. fail clearly when the required API or spec schema does not match.

The installed package exposes:

```text
python -m ptxsim_exec_ir_codegen --output <header>
ptxsim-exec-ir-codegen --output <header>
```

Both commands use the bundled manifest by default; `--manifest <path>` remains
available for explicit validation and negative tests.

Acceptance:

```text
same requirements environment, manifest, generator, and interpreter produce byte-identical output
unknown manifest selector fails before C++ compilation
no frontend checkout is required
no generated file is added to Git
```

If the wheel model cannot support this narrow probe without importing frontend emitter internals, stop and first add a stable normalized-spec API to `ptx_frontend`; do not copy its normalizer into ptxsim.

### WP1 — Core `exec_ir` envelope and proven operation set

Implement the handwritten operand/envelope/stream primitives and generate
exactly four pure opcode records proven by executor WP1-WP3: `mov`, `add`,
`bra`, and `exit`. Generalize the WP0 generator only enough to accept these
four manifest mappings, modifier-free operations, and a generated static
first-level dispatch list.
Packed types, additional move sources, `.uni`, other add variants, and every
other opcode remain unsupported.

Minimum invariants:

- registers use `common::RegisterSlot`, never source spellings;
- immediates own their bits after frontend destruction;
- predicate negation is explicit;
- PC is a checked dense instruction index; fallthrough is derived by the
  stream and direct-branch target PCs are explicit;
- `Op` contains no data type, modifier, or operand-form cross product;
- `mov` and `add` data types are validated as record fields;
- width/type combinations are validated before execution;
- invalid records return structured errors.

Do not add a complete PTX type universe or additional frontend forms merely
because they exist in the frontend database. If selecting register-only
`mov.b32` requires an operand-kind selector, add that one manifest field and
validate it against the normalized frontend model; do not add a generic
selection language.

### WP2 — CMake and CI generation

Add a build-tree-only custom command that:

- requires a selected Python interpreter;
- checks that the required `code_gen` imports are available before registering generation;
- lists the manifest, generator, and wheel model/resources as dependencies;
- exposes generated headers through `ptxsim::exec_ir`;
- installs the public generated header and verifies an installed-package consumer;
- never runs `pip` or downloads files during CMake configure/build.

CI prepares a virtual environment and runs:

```text
python -m pip install -r requirements.txt
cmake ... -DPython3_EXECUTABLE=<that environment>
```

The current repository has no ptxsim vcpkg portfile. If one is added, that portfile must install the local generator package into the selected Python environment before CMake configuration, allowing its pinned package metadata to resolve `ptx_frontend`; ordinary CMake remains offline and never invokes `pip` itself.

### WP3 — Optional frontend adapter

Add a separate target only after WP1/WP2 are stable:

```text
ptxsim_frontend_adapter
  -> ptxsim::exec_ir
  -> ptx_frontend::resolved_ir
```

For the first supported form, convert the exact frontend variant into the ptxsim record. The adapter must copy all required data and must not retain frontend references, `WithLocs`, source spellings, or source-owned storage.

Required tests:

- supported input converts exactly;
- unsupported opcode/variant returns a structured diagnostic;
- the resulting `exec_ir` remains valid after the frontend object is destroyed;
- malformed/incomplete mappings produce no partial output.

The adapter requires a compatible C++ frontend package. Fix or explicitly map the frontend CMake/wheel release version before using its installed `codegen` component.

### WP4 — Add operation families on demand

Add the next operation only when its executor semantics are ready. Each addition must include:

- manifest support mapping;
- generated structural record;
- handwritten adapter conversion;
- handwritten validation;
- one focused test through generation and conversion.

Do not generate the full PTX instruction universe up front. The generated variant remains bounded to operations the simulator can execute.

---

## 7. Deferred artifacts

The following are deliberately outside the initial `exec_ir` module:

- function PC ranges and control-flow tables;
- register-frame allocation plans;
- globals/constants/parameters initializers;
- source-location side tables;
- calls and call-stack metadata;
- executor, scheduler, and simulator composition.

Add each only when the first execution path needs it. Do not recreate the retired `exec_ir -> program -> state -> bootstrap -> lowering` chain.

---

## 8. First implementation checkpoint

WP0 adds the pinned Python requirement, the local generator package, one
minimal support manifest, and a deterministic Python check. It does not add
frontend C++ linkage, an `exec_ir` target, or execution behavior. Executor
WP1-WP3 and the post-Gate-A dispatch audit completed the consumer contract;
the revised WP1 may now implement only the immutable dense stream, instruction
envelope, pure four-op enum, per-op records, and static dispatch glue above.
