# PTXSim Memory Subsystem Construction Plan

> **Document type:** implementation plan for coding agents  
> **Target project:** `endingly/ptxsim`  
> **Working baseline:** `fix/memory_layer`  
> **PTX specification baseline:** PTX ISA 9.3  
> **Language/build:** C++23 / CMake / GTest  
> **Primary objective:** build a deterministic, inspectable PTX architectural storage and data-movement subsystem that remains cleanly separated from the execution-topology model.

---

## 1. Background and current architectural decisions

The first version of `execution_model` is considered structurally closed for the purpose of this work.

The execution model owns the runtime execution topology and execution-control state:

```text
Grid
└── CTA
    └── Warp
        └── Thread
```

The following rules are already assumed:

- `Grid / CTA / Warp / Thread` are stable-identity runtime topology nodes.
- Topology nodes are not storage owners for PTX program data.
- `Thread` owns thread execution-control state such as PC/status.
- `Warp` owns genuinely warp-scoped persistent execution state.
- `CTA` owns the sixteen implicit CTA barrier resources.
- Grid completion/trap/progress are derived rather than duplicated.
- Register data, local/shared/global/parameter/constant storage, TMEM, and asynchronous data-movement state must not be embedded into the topology tree.
- The future simulator/runtime composition root owns topology-to-memory-handle
  bindings. Memory APIs consume only memory-owned handles and allocation
  specifications.

The memory subsystem therefore answers a different question from `execution_model`:

> `execution_model` describes **who is executing and where execution is**.  
> `memory` describes **where architectural data is stored, how it is addressed, and how data moves**.

Target high-level composition:

```text
                              common
                             /      \
                            v        v
                 execution_model   memory
                        \             /
                         v           v
                      simulator / runtime
                              |
                    topology-to-handle bindings
```

`execution_model` and `memory` must remain sibling subsystems. Neither may
include, link, or expose the other; the future simulator/runtime composes both.

---

## 2. PTX ISA 9.3 coverage model

PTX ISA 9.3 defines the following architectural state spaces:

| PTX state space | Address model | Main ownership/sharing model | Memory-subsystem treatment |
|---|---|---|---|
| `.reg` | non-addressable | per-thread (runtime-bound) | `RegisterManager` |
| `.sreg` | non-addressable, read-only | derived from execution/machine/runtime state | no backing allocation |
| `.const` | byte-addressable, read-only during execution | grid/context-visible (runtime-bound) | `AddressSpaceManager` |
| `.global` | byte-addressable R/W | context/device-visible | `AddressSpaceManager` |
| `.local` | byte-addressable R/W | per-thread / function-stack lifetime (runtime-bound) | `AddressSpaceManager` |
| `.param` | specialized addressability | entry and call-frame bindings | `ParameterSpaceManager` |
| `.shared` | byte-addressable R/W | CTA/cluster lifetime (runtime-bound) | `SharedSpaceManager` |
| `.tex` | specialized texture access | context resource | later resource manager |

Do **not** expand `StateSpace` with unrelated concepts merely because they are memory-related.

The following PTX 9.3 concepts are **not additional state spaces** and require separate abstractions:

- generic addressing;
- Tensor Memory / TMEM;
- surface resources;
- `mbarrier` semantic state;
- multimem addresses;
- fabric operations/resources;
- async memory-operation state;
- tensor-map descriptors.

A design such as the following is prohibited:

```cpp
enum class MemorySpace {
  Reg,
  Sreg,
  Const,
  Global,
  Local,
  Param,
  Shared,
  Tex,
  Generic,   // wrong category
  Tmem,      // wrong category
  MultiMem,  // wrong category
  Fabric,    // wrong category
};
```

The subsystem must preserve the distinction between:

```text
state space
addressing mechanism
specialized storage
opaque resource
transport/data-movement mechanism
```

---

## 3. Critical preflight audit — complete before manager implementation

### P0.1 Canonical topology IDs

Topology IDs are owned only by `execution_model`. Memory resource managers do
not consume them; `runtime` binds execution-model IDs to memory-owned handles.

Recommended ownership:

```text
common
  ProgramCounter / FunctionId / RegisterSlot / SymbolId / LabelId / ...
  raw scalar types

execution_model
  GridId / CtaId / WarpId / ThreadId / LaneId
```

### P0.2 Dependency direction

Target dependencies:

```text
       common
       ^   ^
       |   |
execution_model memory
       ^   ^
       \   /
   simulator/runtime
```

More concretely:

```text
memory -> common
memory -X-> execution_model

memory -X-> scheduler
memory -X-> executor
```

The entire `memory` target and all of its public headers must not depend on
`execution_model`. The future simulator/runtime target may depend on both.
Do not add a neutral-ID adapter or conversion layer to bypass this boundary.

Where program metadata is required for allocation, use a narrow allocation specification or view.

---

## 4. Architectural invariants

All implementation agents must preserve these invariants.

### 4.1 Storage and topology are separated

Correct:

```cpp
RegisterManager registers;
registers.view(frame_handle);
AddressSpaceManager address_spaces;
address_spaces.view_shared(shared_handle);
address_spaces.view_local(local_frame_handle);
TensorMemoryManager tensor_memory;
tensor_memory.view(tmem_handle);
```

Avoid:

```cpp
thread.register_file_storage_;
cta.shared_bytes_;
cta.tensor_memory_storage_;
```

Topology nodes may eventually cache non-owning handles/views if profiling justifies it, but backing storage remains owned by `memory`.

### 4.2 No fixed physical-GPU register model

Do not implement:

```cpp
constexpr std::size_t MAX_REGISTERS_PER_THREAD = 255;
```

and do not allocate physical-NVIDIA-style register files.

Register allocation is driven by executable/function metadata and currently instantiated runtime frames.

### 4.3 Store only canonical state

Do not maintain redundant counters/state if the value can be derived without unacceptable cost.

Examples:

- register initialization state is canonical in `RegisterManager`;
- memory-byte initialization state is canonical in `MemoryRegion`;
- async operation completion is canonical in `AsyncMemoryEngine`;
- thread readiness remains canonical in `execution_model::Thread`.

### 4.4 Deterministic first

The functional simulator must prefer deterministic semantics.

Async operations do not require cycle accuracy. They require:

- explicit pending state;
- deterministic progress/completion rules;
- correct visibility/ordering boundaries for supported semantics.

### 4.5 Inspectability is a first-class requirement

Every storage manager should eventually support a stable inspection/snapshot path.

Debugging must not require exposing raw mutable backing buffers as public API.

---

# 5. Target module layout

The final memory module may evolve toward:

```text
submod/memory/
├── CMakeLists.txt
│
├── include/
│   ├── memory.hpp
│   │
│   ├── core/
│   │   ├── state_space.hpp
│   │   ├── address.hpp
│   │   ├── access.hpp
│   │   ├── memory_error.hpp
│   │   └── memory_region.hpp
│   │
│   ├── register/
│   │   ├── register_error.hpp
│   │   ├── register_frame.hpp
│   │   ├── register_view.hpp
│   │   └── register_manager.hpp
│   │
│   ├── address_space/
│   │   ├── address_space_manager.hpp
│   │   ├── global_space.hpp
│   │   ├── constant_space.hpp
│   │   ├── local_space.hpp
│   │   ├── parameter_space.hpp
│   │   ├── shared_space.hpp
│   │   └── generic_address.hpp
│   │
│   ├── sync/
│   │   └── mbarrier_state.hpp
│   │
│   ├── tmem/
│   │   ├── tensor_memory_address.hpp
│   │   ├── tensor_memory_allocation.hpp
│   │   └── tensor_memory_manager.hpp
│   │
│   ├── async/
│   │   ├── async_memory_op.hpp
│   │   ├── async_memory_handle.hpp
│   │   └── async_memory_engine.hpp
│   │
│   ├── resource/
│   │   ├── texture.hpp
│   │   └── surface.hpp
│   │
│   └── advanced/
│       ├── multimem.hpp
│       └── fabric.hpp
│
├── src/
│   ├── memory_region.cpp
│   ├── register_manager.cpp
│   ├── address_space_manager.cpp
│   ├── generic_address.cpp
│   ├── tensor_memory_manager.cpp
│   └── async_memory_engine.cpp
│
└── test/
    ├── test_state_space.cpp
    ├── test_address.cpp
    ├── test_access.cpp
    ├── test_memory_region.cpp
    ├── test_register_manager.cpp
    ├── test_address_space_manager.cpp
    ├── test_generic_address.cpp
    ├── test_mbarrier_state.cpp
    ├── test_tensor_memory_manager.cpp
    └── test_async_memory_engine.cpp
```

Do not create every file immediately. Implement by phase.

---

# 6. Phase 1 — Memory core primitives

## Goal

Create a PTX-independent-enough byte-storage foundation with explicit PTX state-space/access taxonomy.

## Required files

```text
include/core/state_space.hpp
include/core/address.hpp
include/core/access.hpp
include/core/memory_error.hpp
include/core/memory_region.hpp
src/memory_region.cpp
include/memory.hpp
```

If the project prefers flat public includes initially, the same types may temporarily live directly under `include/`; avoid unnecessary directory churn during the first implementation.

## Required contracts

### `StateSpace`

Must represent only:

```text
Register
SpecialRegister
Constant
Global
Local
Parameter
Shared
Texture
```

Required helpers:

```cpp
is_byte_addressable(StateSpace)
is_register_space(StateSpace)
to_string(StateSpace)
```

### `Address`

Use a strong type around `uint64_t`.

Required helpers:

```cpp
is_power_of_two()
is_aligned()
checked_add()
```

`Address` at `MemoryRegion` level means **resolved region-relative byte offset**, not generic PTX virtual address.

### `AccessDescriptor`

Must keep these concepts distinct:

```text
AccessKind
MemorySemantic
MemoryScope
MemoryProxy
size
alignment
async flag
```

Minimum intended enums:

```text
AccessKind:
  Load / Store / Atomic / Reduction / Prefetch

MemorySemantic:
  Weak / Relaxed / Acquire / Release / AcquireRelease / Volatile / Mmio

MemoryScope:
  Cta / Cluster / Gpu / System

MemoryProxy:
  Generic / Async / TensorMap / Alias
```

Do not wire `AccessDescriptor` directly into `MemoryRegion`.

### `MemoryRegion`

Responsibilities:

```text
dense byte storage
bounds checking
alignment checking
read/write permission
initialized-byte tracking
snapshot
loader/runtime initialization path
```

Non-responsibilities:

```text
ThreadId/CtaId/GridId
state-space resolution
generic address resolution
memory consistency
scope/proxy ordering
register storage
TMEM
async completion
```

Required runtime distinction:

```cpp
initialize(...) // allowed for loader/runtime even on read-only region
write(...)      // obeys runtime write permission
```

Use structured errors (`std::expected`), never `abort()`.

## Required tests

`test_memory_region.cpp` must cover at least:

- construction and size;
- zero-size region;
- successful aligned read/write;
- uninitialized read rejection;
- optional ignore-initialization read;
- partial initialization;
- out-of-bounds start address;
- out-of-bounds range;
- arithmetic-overflow-safe bounds behavior;
- valid/invalid power-of-two alignment;
- misalignment;
- read-only runtime-write rejection;
- loader initialization of read-only storage;
- writes mark bytes initialized;
- reset-to-uninitialized;
- zero/fill initialization;
- snapshot;
- zero-length operation policy;
- move construction/assignment if enabled.

## Acceptance

```text
the memory target builds independently of execution_model
all core tests pass under Debug + sanitizers
memory core depends only on common
```

---

# 7. Phase 2 — Register subsystem

## Goal

Implement `.reg` as a dedicated non-addressable storage system.

The current project already has:

```text
common::RawValue
common::RawWidth
common::Bits128
common::RegisterSlot
```

Use these rather than inventing another scalar representation.

## Files

```text
include/register/register_error.hpp
include/register/register_frame.hpp
include/register/register_view.hpp
include/register/register_manager.hpp
src/register_manager.cpp
test/test_register_manager.cpp
```

## Storage model

Recommended conceptual model:

```text
RegisterManager
└── RegisterFrame
    ├── declared RawWidth per RegisterSlot
    └── optional<RawValue> per RegisterSlot
```

The simulator/runtime call-frame binding associates a `RegisterFrameHandle`
with topology and program identity. `RegisterManager` stores neither
`ThreadId` nor `FunctionId`.

Because `RawValue` intentionally has no default constructor, an uninitialized register should naturally be represented explicitly, e.g.:

```cpp
std::optional<common::RawValue>
```

Do not fabricate a zero value for uninitialized registers.

## Public API target

Conceptually:

```cpp
std::expected<RegisterFrameHandle, RegisterError> create_frame(
    RegisterFrameSpec spec);

std::expected<void, RegisterError> destroy_frame(RegisterFrameHandle);

std::expected<RegisterView, RegisterError> view(RegisterFrameHandle);
std::expected<ConstRegisterView, RegisterError> view(RegisterFrameHandle) const;
```

`RegisterFrameSpec` may contain a slot-width vector/span. It is an allocation contract, not executable IR.

The manager should not require a whole `ProgramImage`.

## Future call-stack compatibility

Do not hard-code one eternal register file per execution entity. The
foundational memory identity is `RegisterFrameHandle`; the simulator can bind
multiple handles to nested call frames for the same thread.

Future intended relationship:

```text
Thread CallFrame
├── return PC
├── RegisterFrameHandle
├── LocalFrameHandle
└── function parameter frame
```

## RegisterView

A view should provide:

```cpp
read(RegisterSlot)
write(RegisterSlot, RawValue)
initialized(RegisterSlot)
declared_width(RegisterSlot)
slot_count()
```

Width mismatch must produce a structured register error.

Do not expose the backing vector publicly.

## Required tests

Cover:

- empty frame if allowed;
- frame with pred/b8/b16/b32/b64/b128 slots;
- uninitialized read;
- correct write/read;
- width mismatch;
- invalid slot;
- independent-frame isolation;
- multiple frames;
- frame destruction invalidates handle;
- stale-handle behavior;
- `Bits128` round trip;
- frame handle uniqueness;
- const view;
- no fixed `MAX_REGISTERS_PER_THREAD`.

## Acceptance

A standalone test should be possible without constructing a full simulator:

```cpp
RegisterManager regs;
auto frame = regs.create_frame(spec);
auto file = regs.view(frame);
file.write(slot, RawValue::b32(...));
EXPECT_EQ(file.read(slot), ...);
```

---

# 8. Phase 3 — Byte-addressable PTX state spaces

## Goal

Build state-space ownership/lifetime on top of `MemoryRegion`.

## Files

```text
include/address_space/global_space.hpp
include/address_space/constant_space.hpp
include/address_space/local_space.hpp
include/address_space/parameter_space.hpp
include/address_space/shared_space.hpp
include/address_space/address_space_error.hpp
include/address_space/address_space_handle.hpp
include/address_space/address_space_manager.hpp
src/address_space_manager.cpp
test/test_address_space_manager.cpp
```

## Global space

Architectural ownership:

```text
device/context lifetime
```

Do not bind it to one simulator grid binding.

Required capabilities:

```text
allocate/initialize module/global symbols
read/write
stable simulator addresses
snapshot
```

`create_global(GlobalSpaceSpec{capacity})` returns a `GlobalSpaceHandle`.
`allocate(GlobalSpaceHandle, size, alignment)` returns a stable,
region-relative `AddressRange` from that resource's monotonic allocator. It
does not free individual allocations.

## Constant space

Runtime PTX access is read-only.

Loader/runtime initialization remains allowed.

Do not duplicate `MemoryRegion`; wrap/configure it.

`create_constant(ConstantSpaceSpec{capacity})` returns a
`ConstantSpaceHandle` with the same monotonic allocation contract. Runtime
`write` is rejected; `initialize` remains available for loading.

## Local space

Local storage is bound by the simulator to a thread/call-frame lifetime and
must be future-compatible with nested calls. The memory API uses only a
memory-owned local-frame handle.

Preferred:

```cpp
LocalFrameHandle create_local_frame(
    LocalFrameSpec);
```

`LocalFrameSpec` contains only a fixed size. The returned handle has no
sub-allocation API.

## Parameter space

Split explicitly:

```text
entry/kernel parameter storage
  per simulator grid binding
  read-only to kernel execution

function parameter storage
  per simulator call-frame binding
  supports function input/output semantics
```

Do not model both with one ambiguous raw `ParamMemory`.

`create_entry_parameter(EntryParameterSpec)` and
`create_function_parameter(FunctionParameterSpec)` return distinct strong
handle types. Entry parameters are read-only at runtime; function parameters
are read-write.

## Shared space

Backing storage is bound by the simulator to a CTA lifetime:

```cpp
AddressSpaceView view(SharedSpaceHandle);
```

`create_shared(SharedSpaceSpec)` creates one fixed-size shared resource.

All address-space handle types use the same `AddressSpaceView`; permissions are
enforced by the underlying `RegionAccess`.

Memory owns each shared backing behind a `SharedSpaceHandle`; the simulator
binds that handle to a CTA lifetime. Cluster resolution is expressed with a
narrow context containing the participating `SharedSpaceHandle`s, without
making a memory API consume CTA IDs.

All six resources use `manager token + index + generation` handles.
`destroy(handle)` and `view(handle)` return structured stale errors; views hold
weak manager state and delegate byte operations to `MemoryRegion`.

## Required tests

Cover:

- global lifetime independent of one Grid;
- constant runtime write rejection;
- entry parameter binding isolation;
- local-frame isolation;
- shared-space isolation;
- partial allocation / bounds;
- initialized-byte semantics retained through wrappers;
- lifecycle release;
- snapshot paths.

---

# 9. Phase 4 — Generic address resolver

## Goal

Represent PTX generic addressing as address resolution, not as a ninth storage allocation.

## Files

```text
include/address_space/generic_address.hpp
src/generic_address.cpp
test/test_generic_address.cpp
```

## Rules

`GenericAddress` is a distinct `std::uint64_t` strong type. It uses this fixed,
non-configurable layout with half-open windows of `1ULL << 60` bytes:

```text
[0w, 1w) global
[1w, 2w) constant
[2w, 3w) entry parameter
[3w, 4w) local frame
[4w, 5w) shared
[5w, UINT64_MAX] unmapped
```

The resolved region-relative `Address` is `generic - window_base`. The fixed
constants need no run-time overlap validation or configurable layout object.

Required API:

```cpp
std::expected<ResolvedAddress, AddressResolutionError> resolve(
    GenericAddress address,
    const ExecutionAddressContext& context);
```

`ExecutionAddressContext` contains only optional `GlobalSpaceHandle`,
`ConstantSpaceHandle`, `EntryParameterHandle`, `LocalFrameHandle`, and
`SharedSpaceHandle`. Function parameters are not part of this MVP. It has no
execution-model IDs, manager, view, pointers, or topology/program identity;
the simulator obtains handles from binding tables before calling memory.

`ResolvedAddress` contains a `StateSpace`, a variant of exactly those five
memory-owned handles, and an `Address`. The function is pure and deterministic:
it neither owns a manager nor validates handle staleness or resource capacity.
The relevant address-space manager performs those checks later.

An address in a mapped window with no corresponding handle returns
`missing_binding` and the requested `StateSpace`; it never falls back to a
different resource. An address at or above `5w` returns `unmapped_address`
with no state space.

Do not create a generic `MemoryRegion`, generic backing storage, resolver
class, `cvta`/`isspacep` helpers, or virtual-window allocator. Those later
consumers call this pure resolver when they are introduced.

## Tests

Cover:

- every mapped-window base and final address, including the correct resource
  variant and region-relative offset;
- each adjacent window boundary;
- local-frame and shared-space handle selection;
- missing binding without fallback;
- the first unmapped address and `UINT64_MAX`;
- deterministic repeated resolution;
- the compile-time distinction between `GenericAddress` and `Address`, and
  the absence of function-parameter or topology fields in the context.

---

# 10. Phase 5 — Shared-memory semantic objects / `mbarrier`

## Goal

Support addressable synchronization objects that live in shared memory without confusing them with implicit CTA barriers.

## Architectural rule

Keep these separate:

```text
bar.cta / barrier.cta
  -> execution_model::CTA barrier state

mbarrier.*
  -> shared-memory-resident semantic object
```

## Files

```text
include/sync/mbarrier_state.hpp
src/mbarrier_state.cpp
test/test_mbarrier_state.cpp
```

## Recommended model

Shared backing remains byte storage.

Semantic state is associated with a shared address:

```text
SharedSpace
├── MemoryRegion bytes
└── semantic sidecars
    └── MBarrierState keyed by address
```

Required lifecycle:

```text
init -> valid semantic object
arrive / complete-tx / wait state transitions
inval -> semantic state removed/invalid
address may again behave as ordinary shared storage when specification permits
```

Do not encode the entire simulator-visible `mbarrier` state only as opaque raw bytes.

PTX layout v0 uses an expected arrival count in `[1, 2^20 - 1]`, pending
arrivals, and a signed transaction count in `[-(2^20 - 1), +(2^20 - 1)]`.
`arrive(count = 1)` validates its non-zero count and deducts it atomically.
`complete_tx` may precede `expect_tx`; their operands are accepted whenever
the resulting signed transaction count remains in range. Each successful state
update checks its arithmetic and automatically advances a non-wrapping phase,
resetting pending arrivals to expected, exactly when pending arrivals and
transaction count are both zero. `arrive` returns the prior phase token;
`test_wait` observes it without blocking.

The sidecar belongs to a `SharedSpaceHandle` resource: `AddressSpaceManager`
returns an `MBarrierView` for that handle, and the resource keeps its own
address-to-state table. Consequently manager token/generation validation,
destroy cleanup, stale views, and shared-space isolation are inherited from
the existing resource lifecycle. Tokens include a distinct object incarnation
plus phase, so invalidate/reinitialize at the same address cannot satisfy an
old token. Every mbarrier address operation validates its 8-byte alignment and
range against the shared resource; the state is not serialized into raw bytes.

This MVP excludes v1/report, arrive-drop, blocking wait, thread identity,
ordinary-byte overlap interception, and a generic sidecar abstraction.

## Tests

At minimum:

- init/inval lifecycle;
- phase/generation progression;
- expected-arrival accounting;
- transaction accounting where supported;
- address isolation;
- shared-space isolation;
- misaligned and out-of-bounds diagnostics;
- stale/destroy behavior;
- sidecar removal/reuse with token ABA protection.

---

# 11. Phase 6 — Tensor Memory / TMEM

## Goal

Implement TMEM as specialized storage managed by the memory subsystem but **not** as `StateSpace::Tmem`.

## Files

```text
include/tmem/tensor_memory_address.hpp
include/tmem/tensor_memory_allocation.hpp
include/tmem/tensor_memory_manager.hpp
src/tensor_memory_manager.cpp
test/test_tensor_memory_manager.cpp
```

## Required architectural properties

TMEM is:

```text
specialized 2D storage
backing bound by the simulator to a CTA/group lifetime
dynamically allocated in columns
accessed through tcgen05-specific address/shape semantics
capable of group coordination over TMEM-space handles
```

`TensorMemoryManager` owns:

```text
per-TMEM-space backing
allocation bookkeeping
allocation permits/state
multiple-space/group coordination metadata
address validation
inspection/snapshot support
```

Do not reuse `MemoryRegion` as the public abstraction merely because backing may internally be bytes/words.

Use a dedicated `TensorMemoryAddress`.

## CTA-group future compatibility

`.cta_group::1` and `.cta_group::2` must not force a later storage redesign.

Recommended split:

```text
per-TMEM-space physical/logical storage
+
manager-level multiple-TMEM-space/group allocation and coordination
```

The simulator binds TMEM-space handles to CTA groups; `TensorMemoryManager`
does not consume CTA IDs.

## Tests

Cover:

- TMEM-space isolation;
- 2D address mapping;
- allocation;
- deallocation;
- reallocation;
- allocation exhaustion;
- invalid range;
- one-space group coordination;
- two-space group coordination;
- deterministic allocation;
- snapshot.

---

# 12. Phase 7 — Async memory engine

## Goal

Model asynchronous architectural memory/data-movement operations without introducing cycle-accurate scheduling.

Use the name:

```text
AsyncMemoryEngine
```

rather than `AsyncTransferEngine`, because PTX 9.3 async operations include more than simple copies.

## Files

```text
include/async/async_memory_handle.hpp
include/async/async_memory_op.hpp
include/async/async_memory_engine.hpp
src/async_memory_engine.cpp
test/test_async_memory_engine.cpp
```

## Responsibilities

The engine owns:

```text
issued async operation descriptors
pending/completed state
deterministic operation ordering required by implemented semantics
data movement/reduction completion
completion handles
```

The engine does **not** own:

```text
ThreadStatus
Warp rendezvous state
CTA barrier state
scheduler queues
instruction decode
tcgen05/cp.async instruction semantics
```

Correct interaction:

```text
executor/semantics
    |
    | translates instruction
    v
AsyncMemoryOp
    |
    v
AsyncMemoryEngine
    |
    +-- pending state
    +-- completion state
```

Higher runtime/executor code observes completion and updates execution-model waiting state.

## Operation model

Start with a variant-based operation contract that can grow toward:

```text
CopyOp
StoreOp
ReductionOp
TMEM-related memory op
multimem-related op
fabric-related op
```

Do not expose a single universal:

```cpp
copy(src, dst, size)
```

as the only long-term abstraction.

## Deterministic functional progress

The first implementation may use an explicit:

```cpp
progress()
```

or deterministic completion point rather than simulated cycles.

Tests must never depend on host thread timing.

## Tests

Cover:

- unique handles;
- pending -> complete transition;
- source/destination correctness;
- multiple operations deterministic order;
- independent operations;
- reset/cancellation policy if supported;
- invalid source/destination;
- no direct mutation of execution-model Thread state.

---

# 13. Phase 8 — Texture / surface resources

## Goal

Add compatibility only when an execution milestone requires it.

Texture state space is not ordinary byte-addressed generic storage.

Surface resources are similarly opaque-resource operations rather than a new PTX state-space entry.

Potential files:

```text
include/resource/texture.hpp
include/resource/surface.hpp
```

Keep bindings/descriptors separate from `MemoryRegion`.

This phase is non-blocking for initial scalar/global/shared execution.

---

# 14. Phase 9 — Multimem and Fabric

These are PTX 9.3 advanced features and should be implemented only after core memory and async machinery are stable.

## Multimem

Do not define:

```cpp
StateSpace::MultiMem
```

Model multimem as specialized address resolution over multiple underlying memory locations.

Potential abstraction:

```cpp
MultiMemResolver
```

Underlying bytes remain in ordinary memory resources, typically global storage.

## Fabric

Do not define:

```cpp
StateSpace::Fabric
```

Fabric is a remote/data-movement/access mechanism.

Its integration will likely sit above resource/address resolution and reuse `AsyncMemoryEngine`.

---

# 15. Phase 10 — Simulator/runtime composition root

Do not create a `MemorySubsystem` facade or a topology adapter inside
`memory`. The future simulator/runtime target owns composition of
`execution_model` and the independently usable memory managers. It may expose
explicit manager access without re-forwarding every storage operation.

---

# 16. Machine / launch binding — simulator integration contract

Resource creation must not happen inside topology constructors.

Prohibited:

```cpp
Thread::Thread(...) {
  register_manager.create_frame(...);
}

CTA::CTA(...) {
  address_space_manager.create_shared(...);
}
```

Instead, a higher launch/machine orchestration layer performs:

```text
1. instantiate execution topology
2. inspect program allocation metadata
3. instantiate memory resources and receive memory-owned handles
4. bind launch parameters / initial global-constant state
5. begin execution
```

Conceptual example:

```cpp
Grid grid{grid_id, launch_shape};
RegisterManager register_manager;
AddressSpaceManager address_space_manager;
TensorMemoryManager tensor_memory_manager;

for (auto& cta : grid) {
  auto shared = address_space_manager.create_shared(shared_spec);
  runtime.bind_shared(cta.id(), shared);

  auto tmem = tensor_memory_manager.create_space(tmem_spec);
  runtime.bind_tmem(cta.id(), tmem);

  for (auto& warp : cta) {
    for (auto& thread : warp) {
      auto frame = register_manager.create_frame(entry_register_spec);
      runtime.bind_register_frame(thread.id(), entry_function_id, frame);

      auto local = address_space_manager.create_local_frame(local_spec);
      runtime.bind_local_frame(thread.id(), entry_function_id, local);

      // The runtime binding tables/call frames own the association; memory
      // never receives cta.id() or thread.id().
    }
  }
}
```

---

# 17. Special registers — explicit non-goal of the memory module

Do **not** build a `SpecialRegisterManager` or `SpecialRegisterFile`.

Most PTX special registers are projections of:

```text
Thread
Warp
CTA
Grid
Machine/runtime
memory metadata
```

The later cross-subsystem accessor should look conceptually like:

```cpp
RawValue read_special_register(
    const Machine& machine,
    ThreadId thread,
    SpecialRegisterId reg);
```

Examples:

```text
%tid/%laneid/%ctaid/...   -> execution_model
%dynamic_smem_size        -> memory metadata
%clock/%globaltimer       -> machine/runtime
%envreg                   -> launch environment
```

The accessor belongs to the simulator/runtime, may read both execution-model
state and memory metadata, and is not itself a memory API or storage owner.

---

# 18. Memory consistency model strategy

Do not attempt the complete PTX Chapter 8 memory-consistency model in the first implementation.

However, the core API must avoid making it impossible later.

Keep these facts explicit in `AccessDescriptor`:

```text
operation kind
memory semantic
scope
proxy
size/alignment
async nature
```

Recommended layering:

```text
instruction semantics
       |
       v
AccessDescriptor / architectural request
       |
       v
memory-model / ordering layer       [initially minimal]
       |
       v
address resolution
       |
       v
MemoryRegion / RegisterManager / TMEM
```

Never move `.acquire/.release/.scope/.proxy` behavior into raw byte-storage methods.

---

# 19. Error model

Use domain-specific structured errors.

Examples:

```text
MemoryError
RegisterError
AddressResolutionError
TensorMemoryError
AsyncMemoryError
```

Low-level memory code must not decide:

```text
PTX trap
unsupported instruction
undefined behavior policy
host exception policy
```

It reports a structured fact.

The executor/runtime decides how the simulator responds.

`assert` is appropriate for internal impossible-state invariants, not user/PTX input failures.

---

# 20. Testing policy

Every phase must ship tests in the same commit/PR.

## Unit tests

Each manager is independently constructible and testable.

Avoid requiring a complete PTX frontend pipeline just to test storage.

## Isolation tests

Required cross-instance tests include:

```text
memory unit tests:
  RegisterFrameHandle A != RegisterFrameHandle B
  LocalFrameHandle A != LocalFrameHandle B
  SharedSpaceHandle A != SharedSpaceHandle B
  ParameterSpaceHandle A != ParameterSpaceHandle B
  TMEM-space handle A != TMEM-space handle B

simulator integration tests:
  Thread/CTA/Grid bindings resolve to their intended memory handles
```

## Lifetime tests

Explicitly test:

```text
create -> access -> destroy -> stale handle rejected
```

for handle-based resources.

## Property-style tests

Useful deterministic loops/randomized corpora:

```text
address bounds
alignment
slot indexing
frame allocation
generic-address window resolution
TMEM allocation/free
```

Use fixed seeds.

## Sanitizers

The memory subsystem should be run under the project's sanitizer configuration.

Pay particular attention to:

```text
use-after-free through stale handles
integer overflow in address/range calculations
out-of-bounds spans
lifetime of views
```

## Debug/release behavior

Do not rely on `assert` for externally observable correctness checks because assertions disappear in Release.

---

# 21. CMake/build contract

Target:

```text
real target:   ptxsim_memory
public alias:  ptxsim::memory
```

Dependencies should remain narrow.

Expected early stage:

```text
ptxsim_memory
  PRIVATE/PUBLIC as appropriate:
    ptxsim::common

ptxsim_memory -X-> ptxsim::execution_model

future simulator/runtime target:
    ptxsim::execution_model + ptxsim::memory
```

Public headers should follow the repository's existing exported-header/install conventions.

Every new source/header must be included in install/export handling if the module is public.

---

# 22. Agent work packages

The implementation can be delegated using the following work packages.

## WP0 — ID and PC contract audit

Deliverables:

- audit report of duplicated topology IDs and PC types;
- one canonical ownership decision for future simulator bindings;
- focused cleanup patch if independently warranted;
- existing execution-model and exec-ir tests remain green.

This does not block memory managers, which consume only memory-owned handles.

## WP1 — Memory core

Deliverables:

- core files from Phase 1;
- full core GTest suite;
- no execution-model dependency.

## WP2 — Register manager

Deliverables:

- frame/handle/view/manager;
- width checking;
- uninitialized state;
- full GTest suite;
- supports multiple independent frames.

Depends on WP1/common scalar contracts.

## WP3 — Address-space manager

Deliverables:

- global/const/local/param/shared ownership;
- lifecycle APIs;
- isolation tests.

Depends on WP1; topology/resource binding is future simulator work.

## WP4 — Generic addressing

Deliverables:

- simulator virtual windows;
- resolver;
- `ResolvedAddress`;
- exhaustive boundary tests.

Depends on WP3.

## WP5 — `mbarrier`

Deliverables:

- shared-address sidecar semantics;
- lifecycle/state tests.

Depends on WP3.

## WP6 — TMEM

Deliverables:

- per-TMEM-space backing;
- allocator;
- CTA-group-ready coordination model;
- tests.

Depends on memory-owned TMEM-space handles; can otherwise run partially
parallel with WP4/WP5.

## WP7 — Async memory engine

Deliverables:

- async operation descriptors;
- handles;
- deterministic progress/completion;
- tests.

Depends on WP3; TMEM-specific async operations can be added after WP6.

## WP8 — Simulator/runtime composition/integration

Deliverables:

- simulator/runtime composition root outside `memory`;
- launch-resource binding adapter;
- integration tests with execution_model;
- no instruction semantics added here.

Depends on WP2/WP3, optionally WP6/WP7. These integration tests belong to the
simulator target, not the memory test target.

## WP9 — Advanced PTX 9.3 features

Deliverables separately:

- texture/surface;
- multimem;
- fabric;
- expanded memory-consistency behavior.

Do not block MVP core on this package.

---

# 23. Suggested commit/PR sequence

Keep changes reviewable.

Recommended sequence:

```text
1. audit: unify execution identity contracts for simulator bindings
2. feat(memory): add core memory primitives
3. feat(memory): add register manager
4. feat(memory): add basic PTX address spaces
5. feat(memory): add generic address resolver
6. feat(memory): add mbarrier semantic state
7. feat(memory): add tensor memory manager
8. feat(memory): add async memory engine
9. feat(runtime): compose execution-model and memory handles
```

Do not submit the entire subsystem as one giant PR.

---

# 24. Explicit prohibited designs

Agents should reject or flag the following patterns during review.

### No universal resource manager

Do not create:

```cpp
ResourceManager::allocate(ResourceKind);
ResourceManager::read(ResourceKind, ...);
```

Register, byte-addressable memory, and TMEM have different access models.

### No universal `MemorySpace` enum

Do not conflate state spaces, generic addressing, TMEM, multimem, and fabric.

### No storage in topology nodes

Do not place backing register/shared/local/TMEM data into `Thread/CTA/Warp/Grid`.

### No fixed hardware capacity assumptions

Functional PTX simulation must not hardcode physical RF sizes, SM counts, cache topology, or physical register allocation as architectural storage limits unless a specific PTX semantic requires a target-dependent limit.

### No instruction semantics in memory managers

Do not add:

```cpp
memory.execute_cp_async(inst);
memory.execute_tcgen05(inst);
```

Instruction semantics translate instructions into memory-subsystem requests.

### No scheduler manipulation from memory

`AsyncMemoryEngine` must not directly mark Threads ready/waiting.

### No raw string-based register access

Register lookup uses `RegisterSlot`, never PTX register names.

---

# 25. MVP completion criteria

The memory subsystem MVP is complete when all of the following work:

```text
RegisterManager -> RegisterFrameHandle -> typed RawValue slots
SharedSpaceManager -> SharedSpaceHandle -> isolated byte region
LocalSpaceManager -> LocalFrameHandle -> isolated byte region
ParameterSpaceManager -> parameter-space handles
device/context -> Global + Constant

simulator/runtime binds topology and program/call identity to those handles
```

and:

- core access/bounds/alignment tests pass;
- resource isolation tests pass;
- read-only semantics pass;
- stale handles are detected;
- no topology node owns program storage;
- no memory manager depends on execution_model;
- sanitizers are clean;
- debug snapshots can inspect each implemented storage domain.

TMEM and async machinery may be accepted as the second functional tier if the implementation is split into multiple milestones.

---

# 26. Full PTX 9.3-oriented completion criteria

A later full memory milestone should additionally provide:

```text
generic addressing
mbarrier
cluster-aware shared access
TMEM allocation/access
async memory operations
texture/surface compatibility as required
multimem
fabric
supported PTX memory-ordering/scope/proxy semantics
```

with a documented support matrix distinguishing:

```text
implemented
structurally modeled but not semantically complete
unsupported
architecture-specific
```

---

# 27. Review checklist for every memory PR

Reviewers should answer:

1. Does this change preserve topology/storage separation?
2. Is the state canonical, or is it duplicating another source of truth?
3. Does the API use strong IDs/addresses rather than raw integers/strings where identity matters?
4. Is the resource lifetime explicit?
5. Can this component be unit-tested without constructing the whole simulator?
6. Does it accidentally depend on execution_model?
7. Is TMEM/generic/multimem incorrectly treated as a normal state space?
8. Are bounds and integer-overflow paths tested?
9. Are read-only vs loader-initialization semantics separated?
10. Are stale handles/views safe or detectably invalid?
11. Are memory semantics/scope/proxy kept above raw storage?
12. Does async code remain deterministic and scheduler-independent?
13. Are public headers documented in English?
14. Are all new public APIs covered by GTest?
15. Does Debug + sanitizer build pass?
16. Do generic template parameters use a concept/requires clause that states
    their real contract, with small fixed type sets expressed as ordinary
    overloads rather than bare `typename` and compiler errors?

---

# 28. Immediate next action

Do **not** start with `MemorySubsystem`.

Start with:

```text
WP1: MemoryRegion/core
        |
        v
WP2: RegisterManager
```

The first memory architectural checkpoint should be:

```text
memory::RegisterManager
        |
        v
RegisterFrame / RegisterView
```

If this boundary remains clean in memory-only tests and calling code, proceed
to byte-addressable state-space ownership. Later, the simulator binds its
execution-model topology to the resulting memory handles.

---

## Appendix A — Current-branch observations

At the time this plan was prepared, the remote `fix/memory_layer` snapshot already contained a substantial `submod/execution_model` implementation but did not yet expose a committed `submod/memory` directory.

The branch currently has:

```text
submod/common/include/raw_value.hpp
submod/common/include/ids.hpp

submod/execution_model/include/
  cta.hpp
  cta_state.hpp
  execution_model.hpp
  execution_state.hpp
  forward_def.hpp
  grid.hpp
  ids.hpp
  lane_mask.hpp
  thread.hpp
  warp.hpp
  warp_state.hpp
```

Important audit observation:

```text
execution_model::ThreadId/CtaId/WarpId/LaneId
```

and:

```text
execution_model::ThreadId/CtaId/WarpId/LaneId
```

currently coexist.

Likewise, `ProgramCounter` currently has more than one representation.

These should be resolved deliberately for simulator bindings rather than
allowing a future runtime integration to pick one accidentally. Memory itself
consumes neither ID family.

If a local uncommitted `submod/memory` skeleton already exists, agents should treat the contracts in this plan as a review target and adapt the local work rather than blindly overwriting it.

---

## Appendix B — Specification references

Primary specification baseline:

```text
NVIDIA PTX ISA 9.3

Chapter 5:
  State Spaces, Types, and Variables

Chapter 6:
  Instruction Operands / Generic Addressing

Chapter 8:
  Memory Consistency Model
  - memory operations
  - state spaces
  - operation types
  - scope
  - proxies
  - multimem addresses

Chapter 9:
  memory instructions
  asynchronous copy/bulk operations
  mbarrier
  TensorCore 5th generation / TMEM
  multimem
  fabric

Chapter 10:
  Special Registers
```

Implementation agents should consult the exact PTX ISA 9.3 section for any operation-specific semantic rule rather than extrapolating from an older PTX release.
