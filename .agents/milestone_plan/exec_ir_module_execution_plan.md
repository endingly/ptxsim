# PTXSim `exec_ir` Module Execution Plan

> **Status:** WP0 generator contract probe, WP1 executable program core, and
> the source-built frontend overlay port are implemented; WP2 waits for the
> frontend label-boundary contract
> **Architecture authority:**
> [`../arch/resolved_ir_execution_architecture.md`](../arch/resolved_ir_execution_architecture.md)
> **Primary objective:** lower checked frontend semantic IR into a ptxsim-owned,
> fully-bound, immutable executable program
> **Language/build:** C++23 / CMake / GoogleTest; Python only for the existing
> generator contract and any later generated static glue

---

## 1. Decision summary

`ptx_frontend::resolved_ir` remains the checked symbolic input. `exec_ir` owns
the representation that the simulator executes:

```text
ptx_frontend::resolved_ir::ResolvedModule
                 |
                 | mandatory lowering and support validation
                 v
          ptxsim::exec_ir::ExecutableProgram
                 |
                 v
              Simulator / executor
```

Lowering does not duplicate frontend parsing, modifier legality, type legality,
or target checking. It eliminates execution-time frontend identities:

```text
function SymbolId                 -> FunctionId
register SymbolId + member        -> RegisterSlot
label SymbolId                    -> function-local ProgramCounter
direct-call function SymbolId     -> FunctionId
immediate / execution controls    -> owned executable values
```

No executable instruction may retain a frontend `SymbolId`, spelling,
lexical-scope state, source range, or frontend-owned reference needed to run.
Frontend checker success proves PTX legality; lowering additionally rejects
frontend-legal forms that ptxsim does not implement.

The current executor probe remains authoritative evidence for issue,
predication, prepare/commit, lane-fault, branch, and exit behavior. WP1 now
provides the executable program; frontend lowering remains unimplemented.

## 2. Static program model

The semantic instruction address is a function-local `common` value. `common`
owns it so `execution_model` can later use it without depending on `exec_ir`:

```cpp
namespace ptxsim::common {
struct CodeLocation {
  FunctionId function;
  ProgramCounter pc;
};
}  // namespace ptxsim::common
```

`ProgramCounter` is a checked dense index inside its function. A flat storage
offset is derived layout information only:

```cpp
struct FunctionLayout {
  common::FunctionId id;
  std::size_t begin;
  std::uint32_t instruction_count;
  std::vector<common::RawWidth> register_widths;
};

class ExecutableProgram {
  std::vector<Instruction> instructions_;
  std::vector<FunctionLayout> functions_;
};
```

Fetch is conceptually:

```text
FunctionLayout.begin + location.pc -> flat instruction storage index
```

The initial program owns only the static register slot widths required by the
implemented operations. `RegisterSlot` is a function-local layout identity,
not a physical register or dynamic frame. Local and parameter layouts are
added with their first executable consumer. All such layouts are
frontend-independent values containing no memory handles or allocated storage;
Simulator/runtime converts them to existing memory allocation specs when it
creates an activation.

Branches store a function-local target PC. Direct calls store a `FunctionId`.
Return has no static target: its destination is dynamic call state. The initial
shape is compatible with that distinction without creating `Activation` or a
`CallStack` early.

## 3. Instruction and dispatch boundary

`Instruction` is a ptxsim-owned value envelope over only supported executable
operation records. Its top-level `Op` is pure opcode identity:

```text
mov | add | bra | exit | ...
```

Data type, modifier, and operand-form details live in their operation records.
The executor performs static first-level dispatch by `Op`, then only the
selected opcode handler performs a second dispatch by the needed form/type or
modifier. Do not encode their cross-product in `Op`; do not add a registry,
factory, or virtual handler hierarchy.

Operation records may be generated after the existing generator contract is
extended for a real supported form. Generated code is structural/dispatch glue;
it must not reimplement frontend legality. Operand primitives, `Instruction`,
`ExecutableProgram`, lowering, validation, and execution policy remain
handwritten.

## 4. Frontend lowering contract

Lowering is mandatory rather than an optional adapter. It consumes a checked
`ResolvedModule`, allocates ptxsim IDs/layouts, and returns either one complete
`ExecutableProgram` or a structured diagnostic; it produces no partial program.

The required order is:

1. allocate `FunctionId` values;
2. obtain each frontend function's instruction boundaries and label positions;
3. allocate static register, local, and parameter layouts per function;
4. bind operands, branch targets, and direct calls;
5. copy/normalize immediate and execution-control values;
6. reject unsupported executable forms; and
7. construct immutable function layouts and instruction storage.

The frontend must preserve each function-local label `SymbolId` with its
instruction-boundary offset in `[0, body.size()]`. Consecutive labels may share
a boundary. Ptxsim must not recover labels from source ranges, spellings, or a
retained AST.

## 5. Canonical executable printing

The printer describes the program actually executed, not original PTX source:

```text
@0  [func:0 pc:0]  mov.b32 reg:0, reg:1
@1  [func:0 pc:1]  bra pc:3
@2  [func:0 pc:2]  exit
@3  [func:0 pc:3]  add.u32 reg:2, reg:0, 1
```

`@N` is a derived flat storage offset; `[func:F pc:P]` is the semantic address.
Source text, comments, labels, and register spellings are intentionally not
reconstructed. The printer is part of the executable representation contract,
so lowering tests can use it for concise diagnostics and golden checks.

## 6. Current repository shape

The existing Python probe and the WP1 C++ core are isolated from each other:

```text
submod/exec_ir/
├── include/exec_ir.hpp
├── src/exec_ir.cpp
├── test/test_exec_ir.cpp
└── python/
    ├── pyproject.toml
    ├── setup.cfg
    ├── codegen/
    └── instructions/
```

The core `ptxsim::exec_ir` target exposes only fully-bound values/programs,
links only `ptxsim::common`, and accepts no frontend types. A separate
planned `ptxsim::exec_ir_lowering` component will be mandatory for converting
`ResolvedModule`; it will link `ptx_frontend::resolved_ir` and produce
`ExecutableProgram`. Consumers of an already-built program will need neither
frontend headers nor linkage. Keep CMake/Python generation out of
configure/build network paths.

`.ports/ptx-frontend` pins frontend `v0.0.1b0`, installs its declared Python
requirements in a vcpkg buildtree virtual environment, and generates the C++
artifacts from upstream sources. It contains no generated snapshot. The root
manifest will depend on this port only when the lowering target exists.

## 7. Work packages

### WP0 — Generator contract probe (implemented)

The local `ptxsim-exec-ir-codegen` package proves that the pinned Python
frontend model can be queried deterministically. It loads the packaged
`ptx-instr/v1` database, resolves `add_integer_no_sat/default/u32`, and emits a
byte-stable proof header to an explicit output path.

This remains useful evidence for later static record/dispatch generation. It
does not create a C++ `exec_ir` target, prove a runtime record shape, or make
the generator a second frontend checker.

### WP1 — Fully-bound executable program core (implemented)

Define the smallest core `ptxsim::exec_ir` target needed to own:

- `FunctionLayout`, immutable `ExecutableProgram`, checked function-local
  fetch by `common::CodeLocation`, and derived flat offsets;
- the static register slot widths required by the proven operations;
- the executable instruction envelope and only the `mov`, `add`, `bra`, and
  `exit` operation records already proven by executor probes; and
- canonical executable-program printing.

Use only fully-bound operands (`RegisterSlot`, local PC, `FunctionId`, and
owned raw values). Hand-constructed test programs are sufficient at this
stage; do not add simulator composition, source retention, calls, activations,
or a broad PTX opcode universe.

Acceptance:

- fetch distinguishes `{func:0, pc:0}` from `{func:1, pc:0}`;
- branch targets and fallthrough remain function-local;
- construction rejects a final instruction that may fall through, while an
  unpredicated terminal branch or exit needs no successor;
- invalid function IDs and local PCs return structured errors;
- flat offsets are derivable and never become Thread's authoritative PC; and
- canonical output is deterministic for the same executable program.

### WP2 — Resolved-IR lowering for the proven subset (blocked)

First obtain the frontend label-boundary contract described in section 4. Add
the separate, mandatory `ptxsim::exec_ir_lowering` component that consumes a
checked `ResolvedModule` and lowers exactly the WP1 supported subset.

Frontend `v0.0.1b0` and remote `dev` currently retain a branch target's label
`SymbolId`, but expose no mapping from that ID to an instruction boundary.
Do not start lowering by reconstructing this mapping from source ranges.

Tests cover function/register/label identity binding, owned immediates,
unsupported-but-frontend-legal forms, no partial output, and frontend lifetime
independence of the resulting executable program. The first supported set has
no call execution path; binding a direct call remains unimplemented until its
operation is selected for execution.

### WP3 — Static generated glue and build integration

Only if WP1/WP2 demonstrate repetitive supported-record structure, extend the
existing generator to emit the bounded operation declarations or static
first-level dispatch glue. Its manifest must select only ptxsim-supported
forms. Keep the handwritten executable program and lowering validation as the
authority.

Add CMake generation/installation checks only for files actually emitted.
CI may prepare the pinned Python environment before configuration; CMake must
not run `pip` or download dependencies.

### WP4 — Executor consumption

This is paired with executor WP4. Simulator constructs a
`common::CodeLocation` from the issued Thread/activation state, fetches from
`ExecutableProgram`, derives the same-function fallthrough, and calls the
existing executor lower entry.

Replace private probe instruction use only after equivalent `mov`, `add`,
`bra`, and `exit` behavior passes through the fully-bound representation.
Preserve prepare/commit and lane-local fault behavior; executor owns neither
frontend resolution nor program loading.

### WP5 — Add instruction families on demand

For each execution-ready family, add only:

- the fully-bound record fields it needs;
- lowering/support validation;
- static dispatch glue when repetition justifies it; and
- one focused lowering-to-execution test.

Do not generate or lower the complete PTX universe speculatively.

### Deferred — Calls, activations, and runtime frames

Do not create `Activation`, `ActivationId`, `CallStack`, call-frame storage, or
activation-owned runtime bindings before the first implemented call/return
path. That path will use `common::CodeLocation` for return, `FunctionId` for
entry, and activation identity for dynamic register/local/parameter frames.

## 8. Non-goals and review gates

This plan does not authorize a direct-execution frontend ABI, a global
`InstructionStream`, a `ProgramImage`, source/debug sidecars, a scheduler, or
a simulator plan.

Before each package, confirm:

1. every operand needed at runtime is fully bound;
2. frontend legality remains frontend-owned;
3. `FunctionId + local PC`, not a flat offset, is the semantic code location;
4. the new record/metadata is required by an implemented execution path; and
5. generated code replaces repetition rather than introducing a second schema.
