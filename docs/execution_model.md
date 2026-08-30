# Execution model

## M2-10 status

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
observable through `read`. `state::ThreadState` owns caller-supplied thread,
function, initial-PC, and register-layout metadata with a ready status, its
`RegisterFile`, and an empty call-frame placeholder. It has no execution
transitions or call behavior: the special-register provider is M2-11, executor
PC/status transitions are M4+ work, and call semantics are M7 work.

## Non-goals in M2-10

There is no ProgramImage adapter in production state, special-register provider
(M2-11), executor status/PC transition API (M4+), call semantics (M7),
scheduler, or memory model yet. Symbol storage addresses and lowering remain
future work.
