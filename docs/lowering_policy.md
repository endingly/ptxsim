# Lowering policy

## M2-07 status

`ptxsim::lowering` is created only when the `frontend-lowering` dependency is
available. It owns build-tree-only structured diagnostics and a temporary
`LoweringContext`; its smoke executable compiles generated `resolved_ir`
declarations and links a generated descriptor through that target. It is not
installed or exported yet; M2-08 will stabilize the feature's install/export
surface.

## Boundary

Only `ptxsim::lowering` may link `ptx_frontend`. Lowering must copy execution
facts into simulator-owned data; frontend identities, references, and strings
must not survive in execution-core objects.

## M2-07 scope and non-goals

`LoweringDiagnostic` copies source, function, instruction/family, unsupported
feature, and operand/control context into simulator-owned strings. It does not
retain frontend objects.

`LoweringContext` temporarily maps frontend-global dense `binding::SymbolId`
values to simulator IDs/PCs. It is not retained by `ProgramImage`.

There is no module or instruction lowering yet. Those remain M2-08 work.
