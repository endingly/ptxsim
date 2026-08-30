# Execution model

## M2-07 status

The installed frontend-independent core has the following build graph:

```text
ptxsim::common
  +--> ptxsim::exec_ir --> ptxsim::program
  +--> ptxsim::state
```

`ptxsim::common`, `ptxsim::exec_ir`, `ptxsim::program`, and `ptxsim::state`
are installed/exported public targets. `ProgramImage` owns verified execution IR,
function PC partitions and dense register layouts, copied names and source
metadata, and entry IDs without retaining frontend objects. `ptxsim::lowering`
is an optional `frontend-lowering` feature-on build-tree diagnostics/context
target; it is not installed or exported. Only that target privately links
`ptx_frontend`; `arith` remains independent. Its `LoweringContext` is a
temporary frontend-ID-to-simulator-ID/PC map, never `ProgramImage` data. No
PTX program is lowered or executed yet.
`state::RegisterFile` is a standalone,
frontend-independent dense register store: callers provide its ordered
`RawWidth` layout, reads of unwritten slots return a structured
`uninitialized_read` error, and writes require an exact declared width. Its
internal zero placeholders only satisfy `RawValue` storage and are never
observable through `read`. `state::ThreadState` owns caller-supplied thread,
function, initial-PC, and register-layout metadata with a ready status, its
`RegisterFile`, and an empty call-frame placeholder. `SpecialRegisterProvider`
is now an interface-only, mockable state boundary: it has no production values
or launch configuration. Executor PC/status transitions are M4+ work, and call
semantics are M7 work.

## Non-goals in M2-07

There is no ProgramImage adapter in production state, special-register production
implementation, launch configuration, executor status/PC transition API (M4+),
call semantics (M7), scheduler, or memory model yet. `%tid`, `%ntid`, `%ctaid`,
`%nctaid`, `%laneid`, and `%warpid` values are M6 work. Symbol storage addresses
remain future work. There is no module or instruction lowering, or lowering
install/export surface yet.
