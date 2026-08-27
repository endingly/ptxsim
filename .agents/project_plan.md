# ptxsim Project Roadmap

> Status: Draft v0.4  
> Project: `ptxsim`  
> Frontend: `endingly/ptx_frontend`  
> PTX specification baseline: PTX ISA 9.x  
> Primary goal: deterministic PTX functional ISA simulation with explicit execution IR, reproducible floating-point semantics, inspectable machine state, and extensible SIMT/memory support.

---

## 1. Architecture decisions

The simulator uses the following execution pipeline:

```text
PTX source
   |
   v
ptx_frontend
   |
   v
ptx_frontend::resolved_ir
   |
   | lowering
   v
ptxsim::exec_ir
   |
   v
ptxsim::program::ProgramImage
   |
   +--------------+---------------+
   |              |               |
   v              v               v
state           memory            fp
   \              |              /
    \             |             /
     +---------- semantics -----+
                  |
                  v
               executor
                  |
                  v
              scheduler
                  |
                  v
               runtime
                  |
                  v
                debug
```

The main architectural constraints are:

1. `ptx_frontend::resolved_ir` is the frontend/simulator input boundary.
2. `ptxsim::exec_ir` is the formal execution boundary.
3. `semantics`, `executor`, `scheduler`, `state`, `memory`, and `fp` must not depend on frontend IR.
4. `exec_ir` is not SASS. It is a PTX functional execution IR.
5. SASS decoding, physical register allocation, machine scheduling, and cycle-accurate simulation remain out of scope for the first major development phase.
6. TMEM is a specialized storage resource, not a normal byte-addressed PTX state space.
7. Floating-point execution is isolated in an independent `ptxsim::fp` module.
8. Berkeley SoftFloat is used as the canonical IEEE arithmetic backend; PTX-specific floating-point semantics remain in `ptxsim::fp`.
9. The simulator must be deterministic unless PTX semantics explicitly require an allowed-set or memory-model style validation.
10. Memory and specialized storage state must remain inspectable and dumpable.

---

## 2. Module layout

```text
ptxsim/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── project_plan.md
├── project_roadmap.md
│
├── cmake/
│   ├── ptxsim_register_headers.cmake
│   ├── ptxsim_add_test.cmake
│   └── ptxsim_sanitizers.cmake
│
├── third_party/
│   ├── ptx_frontend/
│   └── berkeley-softfloat-3/
│
├── submod/
│   ├── common/
│   ├── exec_ir/
│   ├── program/
│   ├── state/
│   ├── memory/
│   ├── fp/
│   ├── semantics/
│   ├── executor/
│   ├── scheduler/
│   ├── runtime/
│   └── debug/
│
├── tools/
│   └── ptxsim/
│
├── test/
│   ├── e2e/
│   ├── conformance/
│   └── differential/
│
└── docs/
    ├── execution_model.md
    ├── floating_point.md
    ├── memory_dump.md
    └── support_matrix.md
```

Public CMake aliases:

```text
ptxsim::common
ptxsim::exec_ir
ptxsim::program
ptxsim::state
ptxsim::memory
ptxsim::fp
ptxsim::semantics
ptxsim::executor
ptxsim::scheduler
ptxsim::runtime
ptxsim::debug
ptxsim::ptxsim
```

Real target names should be prefixed:

```text
ptxsim_common
ptxsim_exec_ir
ptxsim_program
ptxsim_state
ptxsim_memory
ptxsim_fp
ptxsim_semantics
ptxsim_executor
ptxsim_scheduler
ptxsim_runtime
ptxsim_debug
```

This avoids collisions with targets exported by `ptx_frontend`.

---

## 3. Floating-point module

### 3.1 Position in the architecture

`ptxsim::fp` is a numerical execution primitive layer.

Dependency direction:

```text
common
  |
  v
 fp
  ^
  |
semantics
```

The following dependencies are forbidden:

```text
fp -> exec_ir
fp -> state
fp -> memory
fp -> scheduler
fp -> runtime
fp -> ptx_frontend
```

`semantics` converts an `exec_ir` instruction into a call to `fp`.

Example:

```text
exec_ir::FmaF32
      |
      v
semantics::execute()
      |
      v
fp::Environment::fma()
      |
      +--> PTX FTZ preprocessing
      |
      +--> SoftFloat arithmetic
      |
      +--> PTX postprocessing
      |
      v
result bits
```

### 3.2 SoftFloat role

Berkeley SoftFloat is used as an IEEE arithmetic backend.

Its responsibilities include:

- binary16 / binary32 / binary64 arithmetic primitives;
- explicit rounding-mode selection;
- fused multiply-add;
- division;
- square root;
- integer/floating conversions;
- comparison;
- exception flags useful for diagnostics and conformance.

PTX rounding mapping:

```text
PTX .rn -> softfloat_round_near_even
PTX .rz -> softfloat_round_minMag
PTX .rm -> softfloat_round_min
PTX .rp -> softfloat_round_max
```

SoftFloat is not allowed to become visible in public instruction semantics APIs.

Bad:

```cpp
void execute(const exec_ir::FmaF32& inst) {
    softfloat_roundingMode = ...;
    ...
}
```

Preferred:

```cpp
auto result = ctx.fp().fma(
    a,
    b,
    c,
    inst.rounding_mode,
    inst.fp_control);
```

### 3.3 PTX-specific FP semantics

The following remain `ptxsim` responsibilities:

- `.ftz`;
- `.sat`;
- instruction-specific NaN rules;
- signed-zero behavior;
- exact vs approximate instruction contracts;
- PTX conversion rules;
- PTX-specific packed/vector forms;
- BF16;
- TF32;
- FP8 families;
- tensor-operation input/output format rules;
- architecture-specific precision rules documented by PTX ISA.

Therefore:

```text
SoftFloat != PTX floating-point semantics
```

Instead:

```text
SoftFloat = IEEE arithmetic primitive
ptxsim::fp = PTX floating-point execution environment
```

### 3.4 Floating-point environment object

Recommended interface:

```cpp
namespace ptxsim::fp {

enum class RoundingMode {
    NearestEven,
    TowardZero,
    TowardNegative,
    TowardPositive,
};

struct FpControl {
    RoundingMode rounding;
    bool flush_subnormal;
    bool saturate;
};

struct Fp32 {
    std::uint32_t bits;
};

struct Fp64 {
    std::uint64_t bits;
};

class Environment {
public:
    Fp32 add(Fp32, Fp32, FpControl);
    Fp32 sub(Fp32, Fp32, FpControl);
    Fp32 mul(Fp32, Fp32, FpControl);
    Fp32 fma(Fp32, Fp32, Fp32, FpControl);
    Fp32 div(Fp32, Fp32, FpControl);
    Fp32 sqrt(Fp32, FpControl);

    Fp64 add(Fp64, Fp64, FpControl);
    Fp64 mul(Fp64, Fp64, FpControl);
    Fp64 fma(Fp64, Fp64, Fp64, FpControl);
};

}
```

The implementation should use raw bits at its API boundary.

### 3.5 SoftFloat environment isolation

SoftFloat uses mode variables for rounding, tininess detection, and exception state.

The wrapper must ensure:

- simulator execution contexts do not accidentally leak rounding state into one another;
- future host-side parallel simulation remains possible;
- exception flags are reset/read in a controlled scope;
- no external code mutates SoftFloat mode variables directly.

Preferred implementation options:

1. build a SoftFloat port with thread-local mode variables; or
2. serialize access inside the backend initially and later replace it with an isolated context-capable port; or
3. maintain one host worker thread per independent SoftFloat environment.

The first implementation should prioritize correctness and deterministic behavior rather than host parallel execution.

### 3.6 Native fast path

A native backend may be introduced later:

```text
ptxsim::fp
├── SoftFloatBackend
└── NativeBackend
```

The SoftFloat path remains the canonical correctness backend.

A native optimization may only be enabled for a PTX operation after differential tests demonstrate equivalence over:

- normal values;
- subnormals;
- signed zero;
- infinities;
- qNaN/sNaN classification;
- overflow;
- underflow;
- cancellation;
- halfway rounding cases.

The simulator must never globally enable `-ffast-math` or `-Ofast` for FP semantics targets.

---

## 4. Validation policy

Instruction correctness must not use a universal epsilon.

Validation classes:

```text
V0 Structural
V1 Bit Exact
V2 Specification-Bounded Numeric
V3 Allowed-Set
V4 Memory-Model Conformance
```

Examples:

```text
integer add                 -> BitExact
fma.rn.f32 finite result    -> BitExact
signed zero                 -> BitExact
unspecified NaN payload     -> FloatClass / AllowedSet
div.approx.f32              -> ISA-specified ULP bound
sqrt.approx.f32             -> ISA-specified relative bound
concurrent racy memory      -> AllowedSet / MemoryModel
```

FP validation should compare raw result bits wherever the PTX specification determines a unique result.

Recommended conformance infrastructure:

```text
test/conformance/
├── integer/
├── floating_exact/
├── floating_approx/
├── conversion/
├── memory/
├── atomic/
└── tensor/
```

Triangular validation:

```text
                 PTX specification
                    /          \
                   /            \
          Reference model ---- ptxsim
                   \            /
                    \          /
                    NVIDIA GPU
```

The reference model for exact IEEE operations should normally use the SoftFloat backend.

A separate MPFR-based oracle may later be added for high-precision verification of approximate/transcendental instructions.

---

# 5. Milestones

## Milestone 0 — Repository and Build Contract

Goal: reproducible build skeleton with frontend and third-party dependencies.

### Independent issues

| ID | Issue | Done condition |
|---|---|---|
| M0-I01 | Repository layout | `submod`, `third_party`, `test`, `docs`, `cmake`, `tools` created |
| M0-I02 | Top-level CMake | CMake 3.28+, C++23, Ninja/GCC path, testing, sanitizer options |
| M0-I03 | Header registration helper | `<ptxsim/<module>/...>` build-tree includes work |
| M0-I04 | CMake presets | Debug and Release presets configure cleanly |
| M0-I05 | vcpkg manifest | dependency resolution works from clean checkout |
| M0-I06 | test helper | GTest targets are discoverable by CTest |
| M0-I07 | third-party license inventory | frontend and SoftFloat license notices documented |

### Coupling issues

| ID | Issue | Done condition |
|---|---|---|
| M0-C01 | Pin `ptx_frontend` | fixed source dependency builds with simulator |
| M0-C02 | Pin Berkeley SoftFloat | fixed revision/release builds into private backend library |
| M0-C03 | Build all empty module targets | all public aliases resolve without CMake name collision |
| M0-C04 | Clean Debug/Release workflow | clean checkout configures, builds, and tests |

---

## Milestone 1 — Exec IR / Program Image / Core State

Goal: formalize the lowering boundary and create executable program/state structures.

### Independent issues

| ID | Issue | Done condition |
|---|---|---|
| M1-I01 | core index/value types | `ProgramCounter`, `RegisterSlot`, `FunctionId`, thread/CTA/warp IDs |
| M1-I02 | raw scalar representation | pred and 8/16/32/64/128-bit storage |
| M1-I03 | exec_ir operand model | register/immediate/special/address operand types |
| M1-I04 | initial typed exec_ir instructions | mov/add/sub/bra/ld forms |
| M1-I05 | ProgramImage structure | executable instruction spans and metadata side tables |
| M1-I06 | RegisterFile | dense slot storage and initialization tracking |
| M1-I07 | ThreadState | PC/status/register state |
| M1-I08 | special-register provider interface | fake provider testable without scheduler |
| M1-I09 | lowering diagnostics | structured errors, no abort on input problem |

### Coupling issues

| ID | Issue | Done condition |
|---|---|---|
| M1-C01 | lowering context | `SymbolId -> RegisterSlot/FunctionId/PC` mappings |
| M1-C02 | first instruction lowering | frontend IR lowers to typed exec_ir |
| M1-C03 | ResolvedModule -> ProgramImage | executable program survives frontend object destruction |
| M1-C04 | execution context | ProgramImage + ThreadState context without frontend dependency |
| M1-C05 | source-to-ProgramImage smoke | minimal PTX reaches executable image |

---

## Milestone 2 — Generic Memory and Dump

Goal: implement inspectable addressable PTX memory.

### Independent issues

| ID | Issue | Done condition |
|---|---|---|
| M2-I01 | StateSpace / VirtualAddress | global/const/param/shared/local |
| M2-I02 | MemoryRegion | bounds/alignment/read/write |
| M2-I03 | Global/Const storage | mutability and initialization semantics |
| M2-I04 | Param storage | per-launch parameter region |
| M2-I05 | Shared/Local factories | CTA/thread isolation |
| M2-I06 | initialized-byte tracking | uninitialized reads are observable |
| M2-I07 | MemorySnapshot | immutable range/symbol/scope snapshot |
| M2-I08 | dump formatters | stable hex/raw/manifest output |

### Coupling issues

| ID | Issue | Done condition |
|---|---|---|
| M2-C01 | program data layout -> memory | PTX symbols receive stable simulator addresses |
| M2-C02 | launch-scoped instances | param/shared/local lifetimes correct |
| M2-C03 | symbol-aware dump | all MVP addressable state spaces dump correctly |

---

## Milestone 3 — Single-Thread Integer Execution

Goal: first complete executable vertical slice.

### Independent issues

| ID | Issue | Done condition |
|---|---|---|
| M3-I01 | exec_ir dispatch | `std::visit` on typed execution instruction |
| M3-I02 | operand access helpers | register/immediate/special read and destination write |
| M3-I03 | predicate execution | true/false/negated behavior |
| M3-I04 | mov semantics | initial scalar forms |
| M3-I05 | integer add/sub semantics | wrap and bit interpretation verified |
| M3-I06 | bra semantics | direct PC target, no runtime label lookup |
| M3-I07 | initial load semantics | selected global forms |
| M3-I08 | StepResult contract | stop/trap/unsupported/step-limit |

### Coupling issues

| ID | Issue | Done condition |
|---|---|---|
| M3-C01 | ThreadExecutor | fetch -> execute -> PC/status update |
| M3-C02 | source-to-register E2E | PTX arithmetic/branch result matches golden |
| M3-C03 | load + dump E2E | memory input and post-run inspection coexist |

---

## Milestone 4 — Floating-Point Environment

Goal: establish a deterministic PTX floating-point backend before broad FP instruction support.

### Independent issues

| ID | Issue | Done condition |
|---|---|---|
| M4-I01 | `ptxsim::fp` public value/control types | raw-bit `Fp16/Fp32/Fp64`, rounding enum, control flags |
| M4-I02 | SoftFloat CMake integration | SoftFloat built privately and not exposed through public headers |
| M4-I03 | SoftFloat environment guard | rounding/exception/tininess state cannot leak between calls/tests |
| M4-I04 | rounding-mode mapping | `.rn/.rz/.rm/.rp` mapped and unit-tested |
| M4-I05 | exact binary32 primitives | add/sub/mul/fma/div/sqrt wrapper paths |
| M4-I06 | exact binary64 primitives | add/sub/mul/fma/div/sqrt wrapper paths |
| M4-I07 | conversion primitives | integer<->float and f32<->f64 selected forms |
| M4-I08 | FTZ helper | signed subnormal input/result flush behavior |
| M4-I09 | NaN/signed-zero classification helpers | raw-bit semantics testable independently |
| M4-I10 | FP validation policies | BitExact/FloatClass/ULP/relative/absolute comparator infrastructure |
| M4-I11 | randomized SoftFloat self-check corpus | deterministic raw-bit vectors for corner classes |
| M4-I12 | document `docs/floating_point.md` | backend boundary, rounding mapping, unsupported formats documented |

### Coupling issues

| ID | Issue | Done condition |
|---|---|---|
| M4-C01 | first FP exec_ir forms | frontend-gated f32 operations lower with explicit rounding/FTZ facts |
| M4-C02 | semantics -> fp integration | scalar `add/mul/fma` selected variants use only `ptxsim::fp` |
| M4-C03 | four-rounding-mode FMA conformance | `.rn/.rz/.rm/.rp` selected f32 test vectors match SoftFloat reference bits |
| M4-C04 | NVIDIA differential harness for exact FP | optional GPU job compares supported exact operations against real execution |
| M4-C05 | FTZ E2E | PTX `.ftz` signed-zero/subnormal cases match expected raw bits |

Milestone acceptance:

```text
PTX source
 -> frontend
 -> exec_ir
 -> semantics
 -> ptxsim::fp
 -> SoftFloat
 -> raw result bits
```

works for an initial exact scalar FP subset, and no host `fesetround()` is required by the canonical path.

---

## Milestone 5 — CTA / SIMT / Shared Memory / Barrier

Goal: deterministic multi-thread functional execution.

### Independent issues

| ID | Issue | Done condition |
|---|---|---|
| M5-I01 | LaunchConfig | 1D/2D/3D thread enumeration |
| M5-I02 | basic special registers | tid/ntid/ctaid/nctaid |
| M5-I03 | warp/lane grouping | partial warp supported |
| M5-I04 | deterministic scheduler | fixed selection order |
| M5-I05 | CTA barrier state | arrive/wait/release generations |
| M5-I06 | bar semantics | scheduler-visible waiting effect |
| M5-I07 | store semantics gate | global/shared initial store |
| M5-I08 | shared/global load expansion | explicit state-space forms |
| M5-I09 | deadlock detection | no-progress report |

### Coupling issues

| ID | Issue | Done condition |
|---|---|---|
| M5-C01 | grid/CTA scheduling | multiple threads and CTAs finish deterministically |
| M5-C02 | shared-memory barrier E2E | write -> barrier -> read |
| M5-C03 | per-CTA shared dump | CTA storage remains isolated |

---

## Milestone 6 — ABI / Calls / Runtime API / CLI

Goal: expose the simulator as a usable library and command-line tool.

### Independent issues

| ID | Issue | Done condition |
|---|---|---|
| M6-I01 | entry parameter layout | scalar/pointer/alignment behavior |
| M6-I02 | host buffer import/export | stable global-memory exchange |
| M6-I03 | call-frame model | return PC/function/register/local metadata |
| M6-I04 | local frame lifetime | function local memory lifecycle |
| M6-I05 | ret/exit gate | exec_ir + semantics |
| M6-I06 | call gate | target lowering and call semantics |
| M6-I07 | generic address/CVTA gate | explicit address conversion subset |
| M6-I08 | public Simulator API | load/launch/run/step/inspection |
| M6-I09 | CLI | source/entry/grid/block/dump/step-limit |

### Coupling issues

| ID | Issue | Done condition |
|---|---|---|
| M6-C01 | parameterized output kernel | host input -> kernel -> host output |
| M6-C02 | device call E2E | function call and local state |
| M6-C03 | CLI full-path test | source -> run -> dump in one command |

---

## Milestone 7 — Conformance and v0.1

Goal: make the implemented subset maintainable and externally verifiable.

### Independent issues

| ID | Issue | Done condition |
|---|---|---|
| M7-I01 | support matrix | frontend/lowering/semantics/validation status separated |
| M7-I02 | E2E corpus | each implemented instruction family has regression coverage |
| M7-I03 | exact FP conformance corpus | raw-bit edge vectors for each supported exact FP form |
| M7-I04 | approximate FP policy table | per-op ULP/relative/absolute bounds from PTX ISA |
| M7-I05 | NVIDIA hardware oracle harness | optional GPU-backed differential execution |
| M7-I06 | ASan/UBSan workflows | clean unit and E2E execution |
| M7-I07 | unsupported-feature audit | no silent semantic downgrade |
| M7-I08 | frontend isolation audit | execution-core targets do not link frontend |
| M7-I09 | FP backend isolation audit | semantics do not call SoftFloat directly |
| M7-I10 | execution/debug documentation | reproducible from clean checkout |

### Coupling issues

| ID | Issue | Done condition |
|---|---|---|
| M7-C01 | integer/memory differential bundle | selected kernels agree with hardware oracle |
| M7-C02 | exact FP differential bundle | finite exact operations agree bit-for-bit where PTX determines unique result |
| M7-C03 | deterministic regression bundle | repeated runs produce identical result/dump |
| M7-C04 | v0.1 release checklist | pinned dependencies, support matrix, limitations, all CI green |

---

## Milestone 8 — Advanced PTX Semantics

Goal: extend the stable functional core toward atomics, memory ordering, warp collectives, and advanced floating formats.

### Independent issues

| ID | Issue | Done condition |
|---|---|---|
| M8-I01 | atomic memory primitives | deterministic typed RMW backend |
| M8-I02 | atom lowering + semantics gate | initial integer atomics |
| M8-I03 | fence/membar gate | supported order/scope explicit |
| M8-I04 | WarpContext / active mask | divergence-aware execution context |
| M8-I05 | vote/ballot gate | active-lane semantics |
| M8-I06 | shfl gate | source-lane/mask semantics |
| M8-I07 | BF16 support in fp | PTX conversion/arithmetic rules isolated from SoftFloat |
| M8-I08 | TF32 support in fp | explicit input quantization/rounding rules |
| M8-I09 | FP8 format layer | e4m3/e5m2 family raw format support where required |
| M8-I10 | approximate FP reference framework | MPFR or equivalent high-precision oracle for selected ops |
| M8-I11 | cluster topology | target-gated cluster IDs/dimensions |
| M8-I12 | mbarrier base model | lifecycle and transitions |

### Coupling issues

| ID | Issue | Done condition |
|---|---|---|
| M8-C01 | atomic/fence E2E | selected synchronized kernels validated |
| M8-C02 | warp collective E2E | divergence + ballot/shfl |
| M8-C03 | reduced-precision FP E2E | BF16/TF32/FP8 selected kernels match reference policy |
| M8-C04 | cluster synchronization E2E | minimal target-gated cluster scenario |

---

## Milestone 9 — TMEM / tcgen05 Foundation

Goal: add Tensor Memory as a specialized resource without corrupting the generic memory abstraction.

### Independent issues

| ID | Issue | Done condition |
|---|---|---|
| M9-I01 | TensorMemoryGeometry / Address | target-aware lane/column geometry |
| M9-I02 | TMEM allocation map | per-CTA allocation ownership |
| M9-I03 | allocation permit/lifetime | leak and misuse detection |
| M9-I04 | TMEM cell state | unallocated/uninitialized/initialized |
| M9-I05 | warp/lane access validator | supported lane ownership rules |
| M9-I06 | TensorMemorySnapshot | deterministic 2D dump |
| M9-I07 | exec_ir TMEM operands | dedicated address/shape types |
| M9-I08 | async completion contract | explicit functional completion model |
| M9-I09 | alloc/dealloc lowering gate | frontend-gated typed exec_ir |
| M9-I10 | ld/st lowering gate | frontend-gated supported shape subset |
| M9-I11 | tensor FP-format adapters | tcgen05 data-format conversion uses `ptxsim::fp` |
| M9-I12 | tensor numeric validation policy | exact vs bounded result rules recorded per supported tcgen05 form |

### Coupling issues

| ID | Issue | Done condition |
|---|---|---|
| M9-C01 | alloc/dealloc E2E | lifecycle closes with no leaked TMEM |
| M9-C02 | TMEM st -> ld E2E | register/TMEM movement matches raw-bit golden |
| M9-C03 | TMEM dump E2E | allocation and cell states inspectable |
| M9-C04 | first tensor arithmetic E2E | only after storage/format semantics are stable |

---

# 6. Floating-point implementation priority

Recommended order:

```text
f32 exact primitives
   |
   v
f64 exact primitives
   |
   v
conversion semantics
   |
   v
FTZ / NaN / signed zero
   |
   v
exact PTX FP instruction coverage
   |
   v
approx instruction policies
   |
   v
BF16 / TF32
   |
   v
FP8
   |
   v
tensor arithmetic
```

Do not begin with tensor arithmetic.

The scalar floating-point environment must be proven first.

---

# 7. v0.1 floating-point definition of done

For v0.1, the minimum FP architecture requirement is:

- [ ] `ptxsim::fp` exists as an independent module.
- [ ] Berkeley SoftFloat is pinned as a private third-party dependency.
- [ ] `.rn/.rz/.rm/.rp` mappings are tested.
- [ ] canonical FP execution does not depend on host `fesetround()`.
- [ ] exact f32 FMA can be evaluated through SoftFloat.
- [ ] exact result validation compares raw bits.
- [ ] signed zero is preserved.
- [ ] FTZ behavior is modeled outside SoftFloat as PTX-specific semantics.
- [ ] NaN comparison policy distinguishes classification from unspecified payload.
- [ ] no `-ffast-math` / `-Ofast` is used by canonical FP semantics.
- [ ] `semantics` does not include SoftFloat headers.
- [ ] support matrix distinguishes exact and approximate FP instructions.
- [ ] optional NVIDIA differential tests exist for at least one exact FP family.

Full BF16/TF32/FP8/tensor arithmetic coverage is not required for v0.1.

---

# 8. Risks

## 8.1 SoftFloat mode-variable leakage

Risk: rounding mode or exception state leaks between simulated instructions or host workers.

Mitigation:

- wrapper-owned scoped environment;
- thread-local SoftFloat port where practical;
- no direct SoftFloat API calls outside `ptxsim::fp`;
- deterministic tests that alternate rounding modes every instruction.

## 8.2 Confusing SoftFloat semantics with PTX semantics

Risk: `.ftz`, `.sat`, NaN handling, and PTX-specific formats are accidentally delegated to SoftFloat.

Mitigation:

```text
PTX preprocessing
 -> SoftFloat primitive
 -> PTX postprocessing
```

must remain explicit and tested independently.

## 8.3 Native optimization changes results

Risk: future `std::fma`/hardware-FMA fast paths differ at edge cases.

Mitigation:

- SoftFloat remains canonical;
- native fast path is opt-in;
- randomized differential corpus is required before enabling it.

## 8.4 Reduced precision infects the core FP API

Risk: TF32/BF16/FP8 are shoehorned into binary32 types and implicit host conversions.

Mitigation:

- explicit raw format types;
- explicit conversion/quantization operations;
- no host `float` as the canonical state representation.

## 8.5 Tensor arithmetic implemented before storage and format semantics

Risk: `mma/tcgen05` becomes an untestable monolith.

Mitigation:

- scalar FP foundation first;
- reduced-precision format layer second;
- TMEM storage third;
- tensor arithmetic coupling last.

---

# 9. Reference dependencies

- NVIDIA PTX ISA 9.x
- `endingly/ptx_frontend`
- Berkeley SoftFloat Release 3e
- optional MPFR for high-precision oracle work
- NVIDIA Driver/GPU as an optional differential oracle
- GPGPU-Sim and Accel-Sim as architecture references only

Berkeley SoftFloat should be treated as an implementation dependency, not as the PTX semantic specification.

The PTX ISA remains authoritative.
