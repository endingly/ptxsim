# CI workflows

| Workflow | Events | Work |
| --- | --- | --- |
| `linux-ci.yml` (Linux CI) | PR updates, monthly, manual | Formatting and all three Clang presets; unrestricted build and full CTest |
| `integration-smoke.yml` | Push to `main`/`dev`, manual | Build every preset; run both Debug public API consumers; trusted-main cache seeds |
| `python-and-package-consumer.yml` | PR updates, monthly, manual | CI helper tests, Python generator tests, wheel build and installed CLI generation |
| `dependency-prepare.yml` | Reused by Linux CI and integration | Prepare dependency archives once before native matrix jobs |
| `pr-cache-maintenance.yml` | Successful PR Linux CI, closed PR | Trusted code retains two PR compiler-cache generations, or deletes recognized compiler caches on closure |

Each C++ workflow first calls the shared dependency-preparation workflow. It
configures Debug using the same dependency manifest feature, dynamic triplet,
LLVM toolchain and Python environment as all three presets, and saves exact binary
and source caches immediately after configure succeeds. It does not compile
project production/test targets. The native matrix starts only after preparation
finishes, giving preparation and each native build/test job their own unchanged
60-minute budget. Each row still measures its own installed tools, restores its
own exact dependency key and configures normally. A preparation/native identity
mismatch or unavailable cache emits a warning and falls back to correct dependency
builds; an incompatible prepared key is never forced into use.

The full Linux matrix is the acceptance gate. Debug, Release and ASan+UBSan each
configure, build `ptxsim_simulator`, build the unrestricted default target (including
all test executables), and run unfiltered CTest with `--no-tests=error`. This keeps
both arithmetic build-tree/installed consumers and the two exact-link contract tests.
Integration builds the same targets in every row but executes only the two Debug
consumers. Manual integration dispatch performs this same useful build/smoke work.
The workflows use separate concurrency groups. Main integration runs serialize
seed upload/retention; other integration refs and full CI cancel superseded runs.

The LLVM toolchain, shared component defaults, dynamic frontend overlay triplet,
SoftFloat static PIC linkage, and exact frontend source/wheel pins are unchanged.
The compiler/tool paths remain unversioned system paths; cache identity measures
the installed versions and compiler bytes rather than assuming a tool version.

## Formatting policy

The existing `.clang-format` is authoritative and CI requires clang-format major 21.
The blocking check formats every changed or added tracked handwritten C/C++ file
against the PR's trusted base SHA. Periodic/manual runs compare with the checked-out
commit's parent. Checkout fetches history; missing or invalid bases fail the check.
For first adoption only, a valid trusted base lacking the formatter policy helper
causes an explicit nonblocking full-tree debt report. This is detected from the
base tree, without fixed commit exemptions. Once the helper exists in the trusted
base, changed files are strictly enforced. Thus this migration's existing PR49
C++ changes receive a visible bootstrap audit, not a claim that they pass formatting.
Generated/vendor/third_party paths and `*.gen.*` files are excluded explicitly.
The separate, visible full-tree audit is nonblocking: at migration baseline
`798bb5c`, 98 of 189 tracked handwritten files fail clang-format 21.1.8. This debt
is reported as file paths/counts; it is not an exemption list. A changed legacy
file must meet the complete file's format. No C++ sources were reformatted for
this CI migration. Run the same checks locally with:

```sh
python3 .github/scripts/check_clang_format.py --base <trusted-base-commit>
python3 .github/scripts/check_clang_format.py --audit
```

## Cache ownership and identity

`actions/setup-linux` restores APT, pip, source-download and binary caches.
The dependency-preparation job owns shared APT/pip/download writes and saves the
binary archives before the native matrix begins. Every matrix row can still save
its own missing exact binary cache after successful configuration/build, including
when its installed tool identity differs from preparation. Keys include runner OS/architecture;
binary identity includes installed C/C++ compilers, LLVM companion tools including
lld, CMake, Ninja, OS release, `ImageOS`, manifest baseline, manifests, overlay
ports, presets, toolchains and triplets. Image revisions alone do not invalidate
the caches. Pip keys include the actual Python interpreter identity, dependency
inputs and month; source-download keys include the exact vcpkg baseline.

Compiler caches use the new `ccache-v9` namespace, independently for Debug,
Release and ASan+UBSan, installed tool identity and exact commit SHA. Prefix
restoration reuses compatible seeds; exact entries are immutable. Successful PR
acceptance saves snapshots scoped by GitHub to that PR merge ref. Explicit job
`cache-mode: write` enables cache writes with normal `contents: read` permissions;
no PR code receives an `actions: write` token. See [GitHub cache scope and access
rules](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching).
Scheduled/manual Linux runs restore seeds without uploading compiler snapshots.
Only successful main push integration jobs upload main-SHA seeds. Dev/manual
integration runs do not create trusted main seeds.

After all three main rows succeed, a separate trusted cleanup job retains each
current seed and its newest strictly older seed of the same opaque cache version.
It verifies all pagination and the unique current key/ref/version before deleting
strictly older recognized entries. Successful PR runs retain two generations per
lineage/version; closure removes only recognized `ccache-v9` caches on the exact
PR merge ref. Maintenance checks out trusted `main`, never PR code or artifacts,
and alone receives `actions: write`. Both cleanup helpers default to a printed
dry-run plan; `--execute` is required for mutation. Invalid scope, incomplete lists
and pagination anomalies fail before any deletion. No live cache deletion is part
of local verification. Cache eviction or incompatible inputs can still cause a
cold build, and the new namespace initially needs warming.

## Measurements and package checks

Dependency preparation uploads its configure log, actual tool/cache identity,
phase duration and original command exit code. Native jobs log both the prepared
and actual identities/keys and the exact dependency-cache-hit result. Production
and test compilation share persistent entries while counters reset
between phases. JSON artifacts record phase/build duration, outcome, build exit
code and cache counters; missing data remains null. Local cache size is not network
transfer volume: transfer bytes/time remain in Actions cache logs. Capturing or
uploading diagnostics is best effort and cannot mask build/test failures.

The Python workflow runs generator and CI-helper unit tests, builds the normal
`ptxsim-codegen` wheel, installs it with its pinned frontend dependency in a fresh
environment, then runs all three installed generator commands outside the checkout.
It does not publish packages. Remote actions are full-SHA pinned and checked by
`scripts/check-action-pins.py`. CI helpers use only the Python standard library;
project Python remains under `python/`.

## Timeout diagnosis and expected effect

PR49's Debug/Release/ASan jobs all missed the same dependency binary key and
independently built the same frontend dependency ABI. Configure spent approximately
30.5/40.8/39.7 minutes, respectively. Debug then built native targets in about
16 minutes and ran all 700 tests in 170 seconds; Release and ASan reached the
60-minute job limit during native compilation, before CTest. The logs do not show
compiler-cache eviction or evidence that test execution caused those cancellations.

Preparing this same dependency set once removes roughly 30–40 minutes of repeated
cold dependency work from each matching native job's budget and avoids two
redundant dependency builds. That is an estimate from the old hosted logs, not a
measured new workflow duration. Cold dependency preparation still costs time;
cache transfer, runner variation and identity changes may alter elapsed time.
No timeout increase, reduced test coverage, optimization-level change or cache
capacity increase is used. Hosted performance remains unverified until a new run.
