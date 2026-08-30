# Lowering policy

## M2 lifetime-acceptance status

`ptxsim::lowering` is created only when the `frontend-lowering` dependency is
available. It owns build-tree-only structured diagnostics, `LoweringContext`,
and `lower_module`. Its smoke executable compiles generated `resolved_ir`
declarations and links a generated descriptor through that target. It is not
installed or exported yet; install/export remains later feature work.

## Boundary

Only `ptxsim::lowering` may link `ptx_frontend`. Lowering must copy execution
facts into simulator-owned data; frontend identities, references, and strings
must not survive in execution-core objects.

## M2-08 scope and non-goals

`LoweringDiagnostic` copies source, function, instruction/family, unsupported
feature, and operand/control context into simulator-owned strings. It does not
retain frontend objects.

`LoweringContext` temporarily maps frontend-global dense `binding::SymbolId`
values to simulator IDs/PCs. It is not retained by `ProgramImage`.

`lower_module(const AstModule&, const ResolvedModule&, std::string)` uses the
syntax module only for statement order, nested lexical blocks, labels, and
source ranges; all instruction and operand semantics come from resolved IR.
It performs a placement pass for functions, data symbols, dense register slots,
labels and PCs, then an instruction pass. `ProgramImage::create` remains the
single final verifier.

The initial supported subset is scalar/predicate `mov`, integer `add`/`sub`,
the four selected `mul` variants, b32 `and`/`or`/`xor`, direct non-`.uni`
`bra`, scalar explicit global/constant b32 loads, scalar explicit global b32
stores, and execution predicates. All other legal resolved forms fail with a
structured diagnostic. The target does not add storage, state, module traversal
abstractions, special-register values, calls, returns, or an install surface.

The feature-on acceptance test proves that the resulting `ProgramImage` outlives
all frontend and temporary lowering objects. It may copy an entry layout into
`ThreadState`, but that is test-only: `ptxsim::lowering` has no production
dependency on `state` and remains build-tree-only, not installed or exported.
