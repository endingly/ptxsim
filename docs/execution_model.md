# Execution model

## M2-03 status

The build graph has the following frontend-independent execution path:

```text
ptx_frontend::resolved_ir -> ptxsim::lowering -> ptxsim::exec_ir
                                               -> ptxsim::program
                                               -> ptxsim::state
```

`ptxsim::common` and `ptxsim::exec_ir` are installed/exported public targets.
`ptxsim::program`, `ptxsim::state`, and `ptxsim::lowering` remain build-tree
skeletons. Only `ptxsim::lowering` links `ptx_frontend`; `arith` remains
independent. M2-03 supplies typed operands, but no PTX program is lowered or
executed yet.

## Non-goals in M2-03

There is no `ProgramImage`, `ThreadState`, register file, executor, scheduler,
or memory model yet.
