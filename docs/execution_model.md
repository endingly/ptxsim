# Execution model

`ptxsim::execution_model` owns the immutable launch topology and the
execution-control state that belongs to that topology:

```text
Grid
└── CTA
    └── Warp
        └── Thread
```

The module owns `GridId`, `CtaId`, `WarpId`, `ThreadId`, and `LaneId`; these
are the canonical topology IDs. `ptxsim::common` retains program and storage
IDs such as `ProgramCounter`, `FunctionId`, and `RegisterSlot`.

`execution_model` does not own program representations, parser/frontend
integration, register storage, address spaces, or tensor memory. Those
resources remain in `ptxsim::memory`. `ptxsim::runtime` composes one execution
topology with memory managers and binds memory-owned handles to the
execution-model IDs for a launch.

The module is installed and exported as `ptxsim::execution_model`. Its public
dependency is `ptxsim::common`.
