# PTXSim Python generators

`python/pyproject.toml` builds the single `ptxsim-codegen` distribution. All
Python source and resources belong to this directory; C++ submodules
contain their C++ sources and CMake integration.

| Package | Responsibility |
| --- | --- |
| `ptxsim_codegen.model` | Full immutable projection, owned C++ names and representation mappings |
| `ptxsim_codegen.frontend` | Version-limited 0.8 adapter and full normalized projection intake |
| `ptxsim_codegen.binding` | Frontend-to-execution binding generation |
| `ptxsim_codegen.code_gen.exec_ir` | IR declarations and diagnostics |
| `ptxsim_codegen.code_gen.lowering` | Binding generator CLI |
| `ptxsim_codegen.code_gen.engine` | Numeric, predicate, memory, control-flow and barrier preparation |
| `ptxsim_codegen.resources` | Packaged backend YAML |

The Python and C++ dependencies both select frontend release 0.8.0 (tag commit
`14879089d2a0f8c5fcb962c621c43b6dc3de193c`). The wheel URL and SHA-256 are
pinned in `pyproject.toml`; the matching source SHA-512 is pinned in the overlay
port. Python 3.14 or newer is required. The package declares the frontend as its
only runtime dependency; the frontend supplies its own production dependencies.
Build isolation uses setuptools 80 or newer and wheel.

One immutable projection preserves all 137 opcodes, 5,453 exact semantic forms
and 7,457 layouts. IR, cold binding and issue-group generation consume this same
projection. Execution support selects handlers independently of representation
and binding. The [active plan](../.agents/milestone_plan/inherited_exec_ir_execution_plan.md)
records current acceptance and pending execution/testing gates.

## Development

From the repository root:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -e ./python
.venv/bin/python -m unittest discover -s python/tests -v
```

Linux CI runs this consolidated unittest suite once in the Python job. The
compiler matrix retains its Python environment for generation. No pytest or
unittest-parallel dependency is needed. Reinstall after changing project
metadata or dependencies; ordinary Python/YAML edits are visible
through an editable install. When replacing a frontend revision without a
version change, reinstall the dependency and inspect its `direct_url.json`
provenance rather than trusting the version string alone.

All generator commands use packaged frontend and backend resources:

```sh
.venv/bin/python -m ptxsim_codegen.code_gen.exec_ir \
  --output-dir out/exec_ir
.venv/bin/python -m ptxsim_codegen.code_gen.lowering \
  --output-dir out/exec_ir_lowering
.venv/bin/python -m ptxsim_codegen.code_gen.engine \
  --output-dir out/inst_execute_engine
```

The console commands remain `ptxsim-exec-ir-codegen`,
`ptxsim-exec-ir-lowering-codegen` and `ptxsim-inst-engine-codegen`. Optional
`--backend` and `--spec-dir` arguments override the packaged inputs.

Each command publishes `artifact_plan.json` and a bounded set of generated
headers and sources. `--cmake-plan-output PATH` writes the exact CMake output
membership without publishing C++ artifacts. IR public headers are partitioned
by opcode and form shard. Cold binding checks exact frontend dynamic classes,
then copies all selected controls, operands and identities into owned types.
Issue entries select a typed handler before looping over lanes. Arithmetic
semantics, allocation, memory-transfer helpers and commit remain handwritten C++.

CMake tracks shared model/frontend/resources and each command's own emitter
closure. Tests are outside the import packages. Publication keeps unchanged
artifact contents and modification times stable for incremental builds.

## Distribution

Build from the Python project directory through its root-relative path:

```sh
python -m pip wheel ./python --no-deps --wheel-dir out/wheels
python -m build --sdist ./python --outdir out/sdist
```

`build` is an optional local distribution-validation tool, not a runtime
requirement. Build isolation installs setuptools and wheel from
`build-system.requires`. The wheel and source distribution contain the backend
YAML and all generator modules. Install a rebuilt wheel to update a regular
installation; editing the checkout does not change installed resources.
