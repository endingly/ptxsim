# Lowering policy

## M2-00 status

`ptxsim::lowering` is created only when the `frontend-lowering` dependency is
available. Its smoke executable compiles generated `resolved_ir` declarations
and links a generated descriptor through that target. It is build-tree-only in
M2-00 and is not installed or exported before it has a public API.

## Boundary

Only `ptxsim::lowering` may link `ptx_frontend`. Lowering must copy execution
facts into simulator-owned data; frontend identities, references, and strings
must not survive in execution-core objects.

## Non-goals in M2-00

There is no module lowering, frontend-ID map, lowering diagnostic, or public
lowering API yet.
