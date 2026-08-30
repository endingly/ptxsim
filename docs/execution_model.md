# Execution model

## M2-05 status

The build graph has the following frontend-independent execution path:

```text
ptx_frontend::resolved_ir -> ptxsim::lowering -> ptxsim::exec_ir
                                               -> ptxsim::program
                                               -> ptxsim::state
```

`ptxsim::common`, `ptxsim::exec_ir`, and `ptxsim::program` are
installed/exported public targets. `ProgramImage` owns verified execution IR,
function PC partitions and dense register layouts, copied names and source
metadata, and entry IDs without retaining frontend objects. Only
`ptxsim::lowering` links `ptx_frontend`; `arith` remains independent. No PTX
program is lowered or executed yet.

## Non-goals in M2-05

There is no `ThreadState`, register file, executor, scheduler, or memory model
yet. Symbol storage addresses and lowering remain future work.
