# CI workflows

| Workflow | Events | Work |
| --- | --- | --- |
| `linux-ci.yml` (Linux CI) | Pull request updates, monthly schedule, manual dispatch | Python generators and all five GCC/Clang Debug/Release/sanitizer presets |
| `main-smoke.yml` | Push to `main` | Build all five presets to warm library/test caches; run GCC Debug public API smoke |

The full workflow keeps its existing job names for PR checks. Its matrix remains
the acceptance gate; the main workflow builds the same targets and runs a smaller
test selection. The two workflows have separate concurrency groups, so a main
push does not cancel a scheduled full run. Normal merges should retain the repository's
pre-merge validation policy; the smoke job does not replace full PR acceptance.

## Main cache warmup and smoke tests

Configuration and build use all five existing PR presets with testing enabled:
GCC Debug/Release, Clang Debug/Release and GCC ASan+UBSan. Each job builds the
default target, including the unit-test executables, with the same compiler and
options as its PR counterpart. Matrix jobs have independent compiler caches and
do not cancel each other on failure.

Only GCC Debug invokes CTest. It runs exactly the build-tree and installed-package
consumers, with `--no-tests=error`. These separately compile and run the public client, covering
package discovery, PTX resolution/lowering, parameter metadata and argument
packing, simulated memory output and FMA execution. The complete numerical,
instruction and sanitizer tests remain in the full workflow.

## Cache ownership and identity

Both workflows use `actions/setup-linux`. It identifies the installed C/C++
compilers, CMake, Ninja, OS release and `ImageOS`, excluding `ImageVersion` so
an image revision alone does not invalidate caches. It selects vcpkg using the manifest's
exact builtin baseline. The binary-cache key also hashes the manifests, overlay
ports and presets, including the test dependency feature selection.

Every matrix job restores dependencies and configures its build on each main
push. In both workflows, every matrix job that misses its exact binary-cache key
can save the resulting archives: after successful configure in the main workflow,
or after the successful CMake workflow in full CI. This covers different installed
toolchains within one compiler family instead of relying on a fixed Debug writer.
Jobs sharing an exact key may race to save; the cache action handles duplicate
saves without failing the job. Only GCC Debug writes the shared APT/source-download
caches. Project compilation still runs on a dependency-cache hit so new or
changed project and test sources enter the compiler cache.

Main caches can be restored by PR runs. PR-created caches belong to the PR merge
ref and cannot warm main or other PRs; changing a cache key does not remove that
scope boundary. See the [GitHub cache access rules](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching#restrictions-for-accessing-a-cache).

Successful main jobs update all five compiler caches using the same keys and
restore prefixes as PR jobs. Debug, Release and sanitizer compilations need
distinct cache entries because their compiler options differ. Existing cache
namespaces are retained; configurations not previously warmed on main initially
require a cold build. Main now pays for five incremental builds, including test
compilation, while avoiding repeated execution of the full test suites.

Cache restoration does not guarantee a compiler-cache hit: changed inputs,
compiler/image identity, paths or eviction can still require recompilation.
Consumer clients built during CTest are warmed only for GCC Debug.

Third-party action references in both workflows and local composite actions are
checked by `scripts/check-action-pins.py`. Local action references remain relative
to the checked-out repository. Both CI helper scripts use only the Python standard
library and run with system `python3` before the project's virtual environment
is created.
