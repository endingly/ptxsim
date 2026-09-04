# Audit and Decision: `resolved_ir` to Executable Program

> **Status:** original direct-execution proposal audited and not adopted;
> follow-up executable-lowering architecture adopted
> **Scope:** program representation, instruction lowering, code location,
> call/return state, register identity, and executable-program printing
> **Current authority:** this document records the architecture decision that
> should guide the next revision of the `exec_ir` and simulator plans
> **Purpose:** preserve the original audit evidence and the later design
> discussion that resolved its open questions

## 1. Historical context

`ptx_frontend::resolved_ir::ResolvedInstruction` is already a generated
`std::variant` whose top-level alternatives represent PTX opcodes. Each opcode
record owns another variant for its legal forms. This already provides the two
dispatch levels required by an execution engine:

```text
ResolvedInstruction
  -> Add / Mov / Bra / Exit / ...
       -> opcode-specific form, type, layout, and modifiers
```

The original audit therefore questioned whether ptxsim should generate a second
opcode enum, instruction payload hierarchy, and frontend-derived schema that
would merely duplicate the frontend instruction structure.

The resolved module also owns its symbol table, functions, instruction vectors,
operand spellings, stable symbol identities, types, and source locations. A
simulator can consume a resolved module without retaining the parser, CST, AST,
or source buffer.

## 2. Original audited proposal: direct `resolved_ir` execution

The original proposal was to keep one immutable
`ptx_frontend::resolved_ir::ResolvedModule` for the simulator lifetime and use
its resolved instructions as the canonical static instruction representation.
Initialization would build only runtime information absent from frontend IR:

- frontend function identity to `ptxsim::common::FunctionId`;
- frontend register identity and parameterized index to
  `ptxsim::common::RegisterSlot`;
- function-local label identity to a function-local instruction position;
- register-frame layouts and other runtime resource bindings;
- an early diagnostic for legal frontend forms that ptxsim does not execute.

The execution engine could then dispatch directly with visitors:

```text
std::visit over ResolvedInstruction     -> opcode handler
std::visit over opcode.variant          -> supported form handler
prepare all participating lanes         -> commit effects
```

That design is feasible and avoids a second instruction hierarchy, but it was
not adopted. It would couple simulator execution directly to the frontend C++
IR ABI and would require runtime lookup of frontend symbolic identities.

The later discussion reached a different boundary: retain a ptxsim-owned
execution representation, but define its value by **binding away frontend
symbolic identities**, not by recreating PTX legality or syntax.

## 3. Adopted boundary: symbolic resolved IR -> fully-bound executable IR

The adopted model is:

```text
ptx_frontend::resolved_ir::ResolvedModule
                 |
                 | lowering / symbolic identity elimination
                 v
    ptxsim::exec_ir::ExecutableProgram
                 |
                 v
              Simulator
```

`resolved_ir` is the symbolic semantic representation. It owns PTX-facing facts
such as:

- opcode and legal instruction form;
- operand shape and type compatibility;
- modifier legality and defaults;
- target availability;
- declaration binding and stable `SymbolId` values;
- lexical-scope resolution;
- source spellings and source locations.

The ptxsim executable representation is the fully-bound execution
representation. It must not reinterpret those frontend legality rules. Its
lowering responsibility is instead to remove identities that require frontend
symbol/scope interpretation and replace them with ptxsim execution identities.

Conceptually:

```text
frontend function SymbolId              -> FunctionId
frontend register SymbolId + member     -> RegisterSlot
frontend label SymbolId                  -> function-local PC
frontend direct-call function SymbolId   -> FunctionId
frontend immediate                       -> owned RawValue / executable value
frontend addressable symbol              -> bound executable resource identity
```

This transformation is sufficient semantic work to justify a ptxsim-owned IR,
even when an opcode record remains structurally similar to the resolved opcode
record. The distinction is not `Add -> some different opcode`; it is:

```text
symbolic Add -> fully-bound executable Add
```

The complete executable-IR opcode/form topology is generated from the packaged
frontend instruction database. The ptxsim backend YAML maps frontend modifier
and operand kinds to ptxsim-owned C++ leaf types; it does not select opcodes,
forms, layouts, modifier values, or PTX legality. Generated records retain the
frontend opcode then form nesting, while their leaves contain only fully-bound
ptxsim values. Every active resolved modifier is retained in frontend order:
fixed modifiers are inline `static constexpr` selectors and dynamic modifiers
are record fields. A multi-layout frontend variant retains all layout records
in an `Operands` variant.

Declaration coverage is deliberately broader than execution support. Lowering,
program validation, and executor dispatch explicitly whitelist the forms they
implement and reject all other generated records as unsupported. Adding a
frontend instruction therefore keeps the C++ model aligned without silently
adding simulator behavior.

The executable representation therefore must not contain frontend `SymbolId`,
label/register spelling, lexical scope, or source-range state required only for
frontend interpretation.

## 4. Static code location

A single module-global program counter is not the authoritative semantic code
location.

The adopted static location is:

```cpp
namespace ptxsim::common {
struct CodeLocation {
  FunctionId function;
  ProgramCounter pc;  // function-local dense instruction index
};
}  // namespace ptxsim::common
```

`ProgramCounter` is therefore function-local. A complete code location is the
pair `(FunctionId, ProgramCounter)`. The value type belongs to `common` so
`execution_model` can store it without depending on executable IR.

This directly models the control-flow distinctions:

```text
fallthrough:
  {F, pc} -> {F, pc + 1}

branch:
  {F, pc} -> {F, target_pc}

call:
  {F, pc} -> {G, entry_pc}

return:
  {G, pc} -> saved caller CodeLocation
```

A branch remains inside the current function. A direct call changes the current
function and creates a new activation. Return restores a saved location from
the call stack.

## 5. Function-local labels

PTX control-flow labels belong to the function control-flow namespace rather
than a module-global namespace. Lexical blocks affect variable declarations,
but label identity is function-local.

The frontend already resolves a branch target to a stable function-local label
`SymbolId`. What was missing from `ResolvedFunction` in the original audit was
the label's position in the resolved instruction body.

The frontend should preserve this information while it already has the
structured function body available. A suitable representation is conceptually:

```cpp
struct ResolvedLabelPosition {
  binding::SymbolId symbol_id;
  std::uint32_t instruction_offset;
};
```

The offset denotes an **instruction boundary** in the flattened resolved
function body and therefore may be in `[0, body.size()]`. Consecutive labels
naturally map to the same boundary. The frontend should record these positions
while resolving/flattening the AST body; ptxsim must not reconstruct them later
from source ranges or retained AST nodes.

The position remains frontend-neutral with respect to simulator PC semantics:
frontend calls it an instruction offset/boundary, not a ptxsim
`ProgramCounter`.

During ptxsim lowering:

```text
ResolvedFunction + ResolvedLabelPosition
                 |
                 v
(FunctionId, label SymbolId) -> function-local ProgramCounter
```

A lowered direct branch stores the bound local target PC and no longer needs the
label identity:

```cpp
struct Branch {
  common::ProgramCounter target;
};
```

The label table may be discarded after lowering when no other supported
operation requires it.

## 6. Function identity and call lowering

Each resolved function receives a stable ptxsim `FunctionId` during program
lowering:

```text
frontend function SymbolId -> FunctionId
```

A direct resolved call already refers to a bound function symbol. The lowered
call stores the bound callee identity rather than a string or global address:

```cpp
struct Call {
  common::FunctionId callee;
  // bound call-ABI operands as required by the supported form
};
```

The callee entry PC is a property of the function layout, normally zero for the
initial dense representation.

A call is **not** modeled as merely setting a global instruction index. Its
runtime meaning includes:

1. derive the caller fallthrough location;
2. save that `CodeLocation` in a call frame;
3. create/select a new callee activation;
4. switch the current function/activation;
5. enter the callee at its entry local PC.

A lowered `ret` has no static target:

```cpp
struct Return {};
```

Its destination comes from the dynamic call stack.

## 7. Register lowering and scope elimination

Frontend resolution has already converted lexical name lookup into stable
symbol identity. Ptxsim must not parse or reinterpret register names.

For each function, lowering builds a static register layout:

```text
(frontend register SymbolId, optional parameterized member)
                       |
                       v
                 RegisterSlot
```

For example, shadowed declarations with the same spelling already have distinct
frontend symbol identities and therefore lower to distinct slots without
retaining lexical-scope information.

`RegisterSlot` is a **static function-local layout identity**, not a physical GPU
register and not a dynamic storage allocation.

All activations of the same function share the same slot layout:

```text
Function foo static layout
  slot 0
  slot 1
  slot 2
       |
       +--> foo activation #1 storage
       |
       +--> foo activation #2 storage
```

This distinction permits recursion and re-entry without changing instruction
operands.

## 8. Dynamic execution state: `CodeLocation + Activation + CallStack`

A complete dynamic execution context is not represented by `CodeLocation`
alone.

The adopted conceptual model is:

```cpp
struct ThreadExecutionState {
  common::CodeLocation location;
  ActivationId activation;
  CallStack call_stack;
  // status / wait state / other execution-model state
};
```

An activation conceptually owns or identifies the dynamic frame resources for
one function invocation:

```cpp
struct Activation {
  ActivationId id;
  common::FunctionId function;
  RegisterFrameHandle registers;
  LocalFrameHandle locals;
  ParameterFrameHandle parameters;
};
```

A call frame minimally preserves the caller state required by return:

```cpp
struct CallFrame {
  common::CodeLocation return_to;
  ActivationId caller_activation;
};
```

The current runtime binding `(ThreadId, FunctionId) -> RegisterFrame` is
sufficient only while a thread cannot have two live activations of the same
function. It should therefore be treated as an MVP restriction. Supporting
recursive or re-entrant calls requires activation identity, because
`(ThreadId, FunctionId)` cannot distinguish two live invocations of the same
function.

Function call parameters for device functions likewise belong to activation
state. They should not be conflated with launch-wide entry-parameter storage.

## 9. Executable program structure

The logical program structure remains function-oriented:

```text
ExecutableProgram
  Function 0
    local pc 0
    local pc 1
    ...

  Function 1
    local pc 0
    local pc 1
    ...
```

However, the immutable physical instruction storage may be flattened into one
contiguous vector for locality, serialization, and canonical printing:

```cpp
namespace ptxsim::exec_ir {
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
}  // namespace ptxsim::exec_ir
```

The initial function layout contains only metadata consumed by implemented
operations. Frontend-independent local and parameter layouts are added with
their first executable consumer; no layout contains allocated memory handles.

Fetch uses semantic `CodeLocation`:

```text
FunctionLayout.begin + location.pc -> flat instruction storage index
```

The flat index is a **derived program-layout address**, not the authoritative
thread PC and not the semantic target of `call`.

This gives both desirable properties:

```text
semantic execution address = FunctionId + local PC
physical code storage       = one flat immutable instruction vector
```

## 10. Canonical executable-program printing

The simulator requirement is to print the program it actually executes, not to
reconstruct the original PTX source.

Therefore the executable IR does not need a debug-info/source-spelling sidecar
for this requirement. The canonical printer uses PTX-like bound identities,
but is deliberately not PTX re-emission or a parser round-trip format:

```text
gpc0  [func:0 pc:0]  mov.b32 register:0, register:1
gpc1  [func:0 pc:1]  exit
```

The leading `gpcN` is the derived global/flat program counter. The bracketed
`[func:F pc:P]` preserves the semantic address. A printer may display branch
and call targets using derived flat addresses for readability, but those values
remain presentation/layout data rather than execution semantics.
Each instruction occupies exactly one line; its metadata and instruction text
share that line.

This canonical textual form is useful for:

- inspecting lowering output;
- golden tests;
- simulator diagnostics;
- trace/log correlation;
- serialization/debug tooling later.

Original whitespace, comments, label names, register spellings, and source text
are intentionally not recoverable from the executable representation. Generated
instruction diagnostics use PTX-like normalized opcodes and frontend modifier
tokens with bound IDs and raw values in specification operand order. They are
stable diagnostics, not source spellings, and do not promise parser round trips.

The generated public header declares topology records and diagnostic functions
only. A generated implementation source defines form/layout and instruction
printers, while stable leaf printers are out of line. `fmt` and `magic_enum`
are absent from public headers and the public compile interface, and linked
with `PRIVATE` visibility by `ptxsim::exec_ir`.

## 11. Lowering pipeline

The adopted lowering pipeline is conceptually:

```text
ResolvedModule
    |
    | 1. allocate FunctionId values
    | 2. establish function-local instruction boundaries / label PCs
    | 3. allocate static register/local/parameter layouts
    | 4. bind every supported operand to executable identity
    | 5. bind branch labels to local PCs
    | 6. bind direct calls to FunctionId
    | 7. copy/normalize owned immediates and execution controls
    | 8. reject frontend-legal forms not implemented by ptxsim
    v
ExecutableProgram
    |
    +-- flat immutable instruction vector
    +-- FunctionLayout table
    +-- static per-function frame layouts
```

No instruction may survive lowering with an unresolved frontend symbolic
identity required for execution.

The lowering pass owns simulator-support validation. Frontend checker success
proves PTX legality, not simulator implementation support.

## 12. Instruction representation consequences

The earlier concern about a duplicated instruction `std::variant` is resolved
by the new boundary.

A ptxsim operation record may still resemble the resolved PTX opcode record, but
its fields must be executable identities. For example:

```text
Resolved Add
  dst = ResolvedRegisterRef(SymbolId, member)
  lhs = ResolvedRegisterRef(SymbolId, member)
  rhs = ResolvedImmediate

        lowering
           |
           v

Executable Add
  dst = RegisterSlot
  lhs = RegisterSlot
  rhs = RawValue
```

Likewise:

```text
Resolved Branch target SymbolId -> local ProgramCounter
Resolved Call function SymbolId  -> FunctionId
```

The exec IR generator must not copy PTX legality facts merely to own them a
second time. Generated records and dispatch glue may still be useful for
repetitive structure, but legality remains owned by the frontend specification
and checker. Handwritten lowering and execution own ptxsim support and
semantics.

## 13. Relationship to executor and warp-issue work

The existing executor probe remains useful for the already-proven execution
properties:

- scheduler-selected `WarpIssueGroup` is the stable issue unit;
- Thread remains authoritative for dynamic control state;
- executor performs prepare/commit rather than per-thread independent
  instruction engines;
- predication and lane-local failures remain execution concerns;
- executor consumes an already-fetched instruction and does not own scheduling
  or program loading.

The future production fetch path becomes:

```text
WarpIssueGroup identifies current local PC
Thread/activation identifies current FunctionId
                 |
                 v
            CodeLocation
                 |
                 v
ExecutableProgram::fetch(CodeLocation)
                 |
                 v
        InstExecuteEngine
```

The current probe constructor's explicit `FunctionId` and the current
`(ThreadId, FunctionId)` register binding are temporary assumptions to revisit
when call/activation support is implemented.

## 14. Required frontend completion

The concrete frontend requirement exposed by this architecture is narrow:

- `ResolvedFunction` must preserve each function-local label `SymbolId` and its
  resolved instruction-boundary offset while the structured function body is
  still available.

The frontend should not emit ptxsim `ProgramCounter`, `FunctionId`,
`RegisterSlot`, activation information, or flat program-layout addresses.

Ptxsim lowering owns all of those executable identities.

## 15. Decision summary

The original proposal to execute `ResolvedInstruction` directly is not adopted.
The adopted architecture is:

```text
ptx_frontend resolved_ir
        |
        | fully-bound lowering
        v
ptxsim::exec_ir::ExecutableProgram
        |
        +-- semantic address: FunctionId + function-local PC
        +-- physical storage: flat immutable instruction vector
        +-- static function-local frame layouts
        |
        v
Simulator
        |
        +-- CodeLocation
        +-- Activation
        +-- CallStack
```

Key invariants:

1. `common::CodeLocation` (`FunctionId + function-local PC`) is the semantic
   static code location.
2. A flat instruction index is derived storage/printing information only.
3. Labels are function-local and lower to local PCs.
4. Direct calls lower to `FunctionId`; return destinations remain dynamic.
5. Register slots are static function-local identities, not physical registers.
6. Register/local/parameter storage belongs to dynamic activations.
7. The executable IR contains no frontend symbol/spelling/scope dependency
   required for execution.
8. Printing is canonical executable-program printing and does not require
   original-source debug information.
9. The next `exec_ir`/simulator plan revision should use this model rather than
   the earlier single global `InstructionStream` abstraction.

## References

- [PTX Frontend README](https://github.com/endingly/ptx_frontend)
- [Resolved IR public types](https://github.com/endingly/ptx_frontend/blob/dev/submod/resolved_ir/include/ptx_resolved_ir.hpp)
- [Resolved module construction](https://github.com/endingly/ptx_frontend/blob/dev/submod/resolved_ir/src/ptx_resolved_module.cpp)
- [Resolved IR CMake target](https://github.com/endingly/ptx_frontend/blob/dev/submod/resolved_ir/CMakeLists.txt)
