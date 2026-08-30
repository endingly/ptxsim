# Execution model

## M2-09 status

The build graph has the following frontend-independent execution path:

```text
ptx_frontend::resolved_ir -> ptxsim::lowering -> ptxsim::exec_ir
                                               -> ptxsim::program
                                               -> ptxsim::state
```

`ptxsim::common`, `ptxsim::exec_ir`, `ptxsim::program`, and `ptxsim::state`
are installed/exported public targets. `ProgramImage` owns verified execution IR,
function PC partitions and dense register layouts, copied names and source
metadata, and entry IDs without retaining frontend objects. Only
`ptxsim::lowering` links `ptx_frontend`; `arith` remains independent. No PTX
program is lowered or executed yet. `state::RegisterFile` is a standalone,
frontend-independent dense register store: callers provide its ordered
`RawWidth` layout, reads of unwritten slots return a structured
`uninitialized_read` error, and writes require an exact declared width. Its
internal zero placeholders only satisfy `RawValue` storage and are never
observable through `read`.

## Non-goals in M2-09

There is no `ThreadState`, ProgramImage-to-RegisterFile adapter, special-register
provider, executor, scheduler, or memory model yet. Symbol storage addresses and
lowering remain future work.
