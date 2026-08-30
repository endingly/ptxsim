# Execution model

## M2-00 status

The build graph now reserves the frontend-independent execution path:

```text
ptx_frontend::resolved_ir -> ptxsim::lowering -> ptxsim::exec_ir
                                               -> ptxsim::program
                                               -> ptxsim::state
```

Only the optional lowering target links `ptx_frontend`; `arith` remains
independent. These new targets are build-tree-only in M2-00; package
install/export remains the existing `arith` surface until a module has a public
API. No PTX program is lowered or executed in M2-00.

## Non-goals in M2-00

There is no `ProgramImage`, `ThreadState`, register file, executor, scheduler,
or memory model yet.
