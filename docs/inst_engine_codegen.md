# Instruction-engine execution bindings

## Scope and ownership

The execution engine uses YAML to register its implemented instruction subset.
The generator reuses `ptxsim_exec_ir_codegen.projection.project_database()` and
`cpp_names.py`; the pinned frontend specification remains the authority for
opcode, form, operand layout, modifier names and values. The execution policy
is **not** another ISA database, a semantic expression language, or a promise
that every declaration can execute.

This first implementation generates the selector and thin lane adapters for the
existing `mov`, `add`, `setp`, `ld`, `st`, `bar.warp.sync`, `bra`, and `exit`
subset. It adds no instruction semantics. Register/resource access, arithmetic,
address resolution, predication, effects, scalar commit and collective commit
remain handwritten in `inst_execute_engine.cpp`. Public C++ interfaces and
`arith`/`exec_ir` ownership are unchanged.

There is one intentional defensive tightening: `mov` pack/unpack layouts are
rejected as unsupported before preparation instead of reaching the scalar
preparer's `std::get`. Invalid operand values inside an accepted scalar layout
still follow the existing lane-fault path.

## Execution policy

Edit `submod/inst_execute_engine/instructions/bindings.yaml`. For example:

```yaml
- id: add_u32
  opcode: add
  form: add_integer_no_sat
  modifiers: {type: [u32]}
  prepare:
    function: prepare_operation
    arguments: [registers, arithmetic, operation, successor]
  commit: scalar
```

`form` and `layout` use normalized frontend identifiers, not C++ type names.
A single-layout form can omit `layout`; a multi-layout form must select one
explicitly. Every dynamic modifier must be matched against a nonempty list or
listed under `ignored` with a nonempty rationale. A fixed modifier may also be
matched, but cannot be overridden. A new dynamic frontend field therefore
requires an execution-policy decision before generation can succeed.

Matching is order-independent. Two bindings for the same opcode/form/layout
must have disjoint modifier domains. Unknown keys, duplicate YAML keys, unknown
identifiers, illegal values, duplicate binding IDs, ambiguous layouts and
overlapping matches fail generation. Unregistered combinations fail closed.
This validates execution wiring, not every cross-field PTX legality constraint;
frontend validation and handwritten operand/resource checks retain their roles.

## Adapter vocabulary

`prepare.function` is the name of a handwritten internal C++ function. It is not
an arbitrary C++ expression. Argument order is explicit and uses this vocabulary:

| Argument | Value passed to the handwritten function |
| --- | --- |
| `resolver` | Lazy lane-resource resolver, without binding a frame |
| `registers` | Register view, resolving once and forwarding a binding fault |
| `thread` | Read-only topology thread reference |
| `arithmetic` | Existing arithmetic context |
| `operation` | Typed opcode record extracted from the instruction variant |
| `form` | Typed selected form |
| `successor` | Required fallthrough PC, already checked by the engine |
| `{modifier: state_space}` | That selected form's existing modifier member |
| `{kind: state_space, value: generic}` | Constant using the exec-IR backend mapping |

The C++ compiler checks the target function's signature and definition. Do not
use `successor` for a form whose engine fallthrough contract does not require
one. Do not register a collective handler as scalar. These semantic/protocol
contracts are reviewed and tested in C++, not inferred from YAML.

`commit: scalar` selects the existing lane-wise commit path; it does **not**
make the entire warp transactional. `commit: warp_sync` selects the existing
rendezvous protocol. An immediate barrier, branch or exit does not bind a frame
merely because it has a generated adapter. Predication runs before the selected
adapter, so masked lanes do not evaluate ordinary operands.

## Build integration

After installing the repository's Python requirements, CMake invokes:

```sh
python -m ptxsim_exec_ir_codegen.gen_engine \
  --bindings submod/inst_execute_engine/instructions/bindings.yaml \
  --output build/engine_dispatch.gen.inc \
  --coverage build/engine_coverage.json \
  --depfile build/engine_dispatch.d
```

Normal builds place these files under the engine's binary directory. The private
include is compiled within the engine's anonymous namespace; it is not installed
or checked into the source tree. CMake tracks both outputs and a depfile covering
the installed/editable generator, backend and frontend specification. The
`ptxsim_engine_codegen` target does not run unconditionally on every build.

The JSON report distinguishes `conditional` bindings from `unsupported`
declarations at form/layout granularity and records each accepted modifier
subset. It is a wiring inventory, not a runtime validity or semantic-conformance
report. Generated artifacts contain no timestamps, and source generation is
independent of YAML binding order.

## Tests and extension workflow

CTest runs the Python validator tests and pinned-frontend integration/CLI test:

```sh
PTXSIM_ENGINE_BINDINGS="$PWD/submod/inst_execute_engine/instructions/bindings.yaml" \
  python -m unittest ptxsim_exec_ir_codegen.test_engine
```

`test_engine_dispatch.cpp` exercises the public engine, including layout rejection,
modifier rejection before resource access, unsigned wraparound, masked invalid
sources, frame-free control instructions and collective synchronization. Existing
engine and simulator C++ tests continue to exercise the handwritten semantics.
Tests derive expected arithmetic results independently of the YAML.

To add execution support, implement or reuse a handwritten preparer, register a
non-overlapping binding, and add independent C++ success/failure tests. Extend the
small adapter vocabulary only when a reusable pattern requires it. Complex
synchronization, atomics, async work and multiple-result commit protocols should
remain explicit C++ implementations rather than expanding YAML into a language.
