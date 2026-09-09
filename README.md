# ptxsim

A C++23 library for functional PTX simulation.

ptxsim uses [ptx_frontend](https://github.com/endingly/ptx_frontend) to parse and
resolve PTX, lowers the result to an executable IR, and runs supported instructions
on a simulated grid of CTAs, warps, and threads. The public C++ API provides
single-step execution, bounded runs, simulated memory, and structured diagnostics.

```text
PTX source → ptx_frontend resolved IR → ptxsim executable IR
                                              ↓
                                  Simulator + launch runtime
                                              ↓
                               Register/memory results and reports
```

## Current scope

The project is under active development; its current package version is `0.0.1`.
It is intended for functional instruction experiments and execution testing.

- Deterministic warp issue, per-thread program counters, predication, branches,
  and multiple warps/CTAs.
- Movement and topology-register reads; the pinned Add, Sub, Mul, FMA, and Setp
  instruction forms; ordinary loads/stores; branch, exit, and named barriers.
- Entry-argument layout and packing, explicit memory bindings, and observable
  completion, traps, deadlocks, and issue-budget exhaustion.
- Modular CMake targets, including a standalone arithmetic library backed by
  SoftFloat and exact integer operations.

Support is specific to instruction forms, types, modifiers, and runtime resources.
Successful parsing or lowering does **not** establish execution support. The
[Supported PTX](https://github.com/endingly/ptxsim/wiki/Supported-PTX) page records
the boundaries, including the FMA out-of-bounds model choices awaiting hardware
validation. Do not assume that arbitrary CUDA-generated PTX will run unchanged.

Device call/return, asynchronous memory execution, and cycle/latency modeling are
not implemented. The current entry point is a C++ library API; there is no PTX
runner CLI or CUDA runtime replacement.

## Build and run a first check

The maintained build matrix is Linux with GCC and Clang. You need CMake 3.28+,
Ninja, Python 3.12+, and a compiler/standard library supporting C++23
`std::expected` and the `__int128` extension. CI uses Ubuntu 26.04. Other platforms
are not covered by that matrix.

On Ubuntu, install the build tools:

```sh
sudo apt-get update
sudo apt-get install -y build-essential ccache clang cmake curl flex git \
  ninja-build pkg-config python3 python3-pip python3-venv tar unzip zip
git clone https://github.com/endingly/ptxsim.git
cd ptxsim
```

Create the Python code-generation environment and a repository-local vcpkg
checkout at the manifest's pinned baseline. Run the following from the repository
root in the same shell:

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e .

export VCPKG_ROOT="$PWD/.cache/vcpkg"
git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
PTXSIM_VCPKG_REVISION=$(python -c 'import json; print(json.load(open("vcpkg.json"))["builtin-baseline"])')
git -C "$VCPKG_ROOT" checkout "$PTXSIM_VCPKG_REVISION"
"$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

cmake --preset ci-linux-gcc-debug
cmake --build --preset ci-linux-gcc-debug --parallel 2
ctest --preset ci-linux-gcc-debug --output-on-failure --no-tests=error \
  -R '^ptxsim_arith_(build-tree|installed)_consumer$'
```

For an existing environment, reuse its vcpkg checkout instead of cloning again.
The first configure downloads and builds dependencies. Reduce build parallelism
if compiler memory use is too high.

The two consumer checks compile and run a public C++ client against the build-tree
and installed packages. Despite their historical `arith` names, they also parse,
lower, and execute PTX, pack entry arguments, verify a global-memory result of
`42`, and check a fused floating-point result. Success is reported by CTest; the
client itself exits silently with status zero.

For the complete C++ suite:

```sh
ctest --preset ci-linux-gcc-debug --output-on-failure --no-tests=error
```

See [Building and Testing](https://github.com/endingly/ptxsim/wiki/Building-and-Testing)
for other presets, Python tests, installation, and dependency troubleshooting.

## Using the library

A typical kernel takes an address in simulated memory and a scalar argument:

```ptx
.entry store_value(.param .u64 output, .param .u32 value) {
  .reg .u64 %address;
  .reg .u32 %value;
  ld.param.u64 %address, [output];
  ld.param.u32 %value, [value];
  st.global.u32 [%address], %value;
  exit;
}
```

The host parses and resolves the PTX, calls `exec_ir_lowering::lower`, creates a
`runtime::LaunchRuntime`, allocates and binds simulated global memory, and packs
the two arguments with `simulator::pack_entry_arguments`. It then constructs a
`simulator::Simulator`, calls `run(issue_budget)`, checks the termination report,
and reads the output through the memory API. Pointer arguments contain simulated
addresses, not host pointers.

The [Getting Started tutorial](https://github.com/endingly/ptxsim/wiki/Getting-Started)
provides a complete C++ program and CMake commands for this kernel. The existing
[consumer source](submod/arith/test/consumer/main.cpp) and
[consumer CMake project](submod/arith/test/consumer/CMakeLists.txt) provide a
continuously tested integration example.

Installed clients use `find_package(ptxsim CONFIG REQUIRED)` and exported targets
such as `ptxsim::simulator` and `ptxsim::exec_ir_lowering`. Keep the launch runtime
and arithmetic context alive for the lifetime of the simulator. The
[public simulator header](submod/simulator/include/simulator.hpp) documents the
ownership, argument, and reporting contracts.

## Documentation

- [Wiki home](https://github.com/endingly/ptxsim/wiki): user and developer guides.
- [Running PTX](https://github.com/endingly/ptxsim/wiki/Running-PTX): loading,
  stepping, bounded execution, and reports.
- [Parameters and Memory](https://github.com/endingly/ptxsim/wiki/Parameters-and-Memory):
  launch ABI, addresses, bindings, and resource lifetimes.
- [Supported PTX](https://github.com/endingly/ptxsim/wiki/Supported-PTX): supported
  forms and known limitations.
- [Architecture and Development](https://github.com/endingly/ptxsim/wiki/Architecture-and-Development):
  module responsibilities and extending execution support.
- [Python generators](python/README.md): editable installs, generator commands,
  and wheel packaging.

## Contributing

Please include a minimal PTX reproducer, toolchain information, and the observed
diagnostic when [reporting an issue](https://github.com/endingly/ptxsim/issues).
For changes, add focused coverage for the affected behavior and follow the
[repository working rules](AGENTS.md). See the
[CI workflow notes](.github/CONTRIBUTING.md) for the acceptance matrix.

## License

[MIT](LICENSE).
