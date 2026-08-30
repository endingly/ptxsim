# Lowering policy

## M2-06 status

`ptxsim::lowering` is created only when the `frontend-lowering` dependency is
available. It owns a build-tree-only structured diagnostic API and its smoke
executable compiles generated `resolved_ir` declarations and links a generated
descriptor through that target. It is not installed or exported yet; M2-08
will stabilize the feature's install/export surface.

## Boundary

Only `ptxsim::lowering` may link `ptx_frontend`. Lowering must copy execution
facts into simulator-owned data; frontend identities, references, and strings
must not survive in execution-core objects.

## M2-06 scope and non-goals

`LoweringDiagnostic` copies source, function, instruction/family, unsupported
feature, and operand/control context into simulator-owned strings. It does not
retain frontend objects.

There is no frontend-ID map, lowering context, module lowering, or instruction
lowering yet. Those remain M2-07 and M2-08 work.
