# Execution model

## M2 package-acceptance status

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
is an optional `frontend-lowering` target. It is installed/exported only as
the explicit `lowering` package component. `arith` remains independent. Its
`LoweringContext` is a
temporary frontend-ID-to-simulator-ID/PC map, never `ProgramImage` data. PTX
program lowering is available through the optional component:
it converts a parsed AST plus resolved module into a self-contained,
`ProgramImage`-verified initial instruction subset. The AST supplies labels,
nested ordering, and source ranges; resolved IR supplies semantics. No executor
or state integration is added.
The feature-on lifetime acceptance covers destruction of source, parser, AST,
resolved IR, and temporary lowering context before walking, dumping and
verifying the resulting `ProgramImage`, then copying an entry register layout
into a standalone ready `ThreadState`. This is test-only wiring; production
lowering does not depend on `state`.
Use `find_package(ptxsim CONFIG REQUIRED COMPONENTS lowering)` to load the
component and its public `ptx_frontend` dependency. Default
`find_package(ptxsim CONFIG REQUIRED)` remains core-only and does not discover
the frontend; both build-tree and installed lowering consumers exercise
parse/resolve/lower/verify.
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

## Non-goals in M2 package acceptance

There is no ProgramImage adapter in production state, special-register production
implementation, launch configuration, executor status/PC transition API (M4+),
call semantics (M7), scheduler, or memory model yet. `%tid`, `%ntid`, `%ctaid`,
`%nctaid`, `%laneid`, and `%warpid` values are M6 work. Symbol storage addresses
remain future work. Lowering is never a default core dependency.
