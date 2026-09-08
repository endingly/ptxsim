# CI workflows

| Workflow | Events | Work |
| --- | --- | --- |
| `linux-ci.yml` (Linux CI) | Pull request updates, monthly schedule, manual dispatch | Python generators and all five GCC/Clang Debug/Release/sanitizer presets |
| `main-smoke.yml` | Push to `main` | GCC Debug public API smoke; populate Clang dependency cache only on an exact cache miss |

The full workflow keeps its existing job names for PR checks. Its matrix remains
the acceptance gate; the main workflow checks the merged branch using a smaller
build. The two workflows have separate concurrency groups, so a main push does
not cancel a scheduled full run. Normal merges should retain the repository's
pre-merge validation policy; the smoke job does not replace full PR acceptance.

## Main smoke build

Configuration uses the existing GCC Debug preset with testing enabled. The build
names only `ptxsim_simulator`, `ptxsim_exec_ir_lowering` and
`ptxsim_arith_validation`. Their dependency closure covers all installed project
libraries without compiling the unit-test executables.

CTest runs exactly the build-tree and installed-package consumers, with
`--no-tests=error`. These separately compile and run the public client, covering
package discovery, PTX resolution/lowering, parameter metadata and argument
packing, simulated memory output and FMA execution. The complete numerical,
instruction and sanitizer tests remain in the full workflow.

## Cache ownership and identity

Both workflows use `actions/setup-linux`. It identifies the installed C/C++
compilers, CMake, Ninja and runner image, and selects vcpkg using the manifest's
exact builtin baseline. The binary-cache key also hashes the manifests, overlay
ports and presets, including the test dependency feature selection.

The GCC smoke configure restores or fills GCC dependencies on every main push.
The Clang job uses an exact `lookup-only` probe without fallback keys. On a hit it
skips Python/vcpkg setup, archive download and configure. On a miss, configure
installs dependencies and saves their binary archives; it never builds ptxsim or
runs its tests. System package setup still runs to identify the actual toolchain.
Only the designated GCC job writes the shared APT/source-download caches.

Main caches can be restored by PR runs. PR-created caches belong to the PR merge
ref and cannot warm main or other PRs; changing a cache key does not remove that
scope boundary. See the [GitHub cache access rules](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching#restrictions-for-accessing-a-cache).

Main smoke updates only the GCC Debug compiler cache and only compiles library
objects plus the consumer. Other presets and unit-test objects are cached by
full runs; the split does not promise to precompile all five configurations.
The new cache-key namespaces cause an initial cache miss when first adopted.

Third-party action references in both workflows and local composite actions are
checked by `scripts/check-action-pins.py`. Local action references remain relative
to the checked-out repository. Both CI helper scripts use only the Python standard
library and run with system `python3` before the project's virtual environment
is created.
