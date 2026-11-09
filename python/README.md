# PTXSim Python generators

`python/pyproject.toml` builds the single `ptxsim-codegen` distribution. All
Python source, resources and templates belong to this directory; C++ submodules
contain their C++ sources and CMake integration.

| Package | Responsibility |
| --- | --- |
| `ptxsim_codegen.model` | Typed projection records, C++ names and backend leaf mappings |
| `ptxsim_codegen.frontend` | Frontend database loading and projection |
| `ptxsim_codegen.binding` | Frontend-to-execution binding generation |
| `ptxsim_codegen.code_gen.exec_ir` | IR declarations and diagnostics |
| `ptxsim_codegen.code_gen.lowering` | Binding generator CLI |
| `ptxsim_codegen.code_gen.engine` | Numeric, predicate, memory, control-flow and barrier preparation |
| `ptxsim_codegen.resources` | Packaged backend YAML |

The frontend Python dependency must match the C++ overlay port. The physical
layout migration temporarily retains commit
`1a32fac33be61c0f540caaecf2ce14373c281b45` and the direct Jinja2 declaration.
The [active migration plan](../.agents/milestone_plan/inherited_exec_ir_execution_plan.md)
advances both frontend pins to release 0.8.0 and removes direct Jinja2 with the
full projection change; the current package layout alone does not complete
that dependency migration.

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
metadata or dependencies; ordinary Python/YAML/template edits are visible
through an editable install. When replacing a frontend revision without a
version change, reinstall the dependency and inspect its `direct_url.json`
provenance rather than trusting the version string alone.

All generator commands use packaged frontend and backend resources:

```sh
.venv/bin/python -m ptxsim_codegen.code_gen.exec_ir \
  --output out/exec_ir.gen.hpp --source-output out/exec_ir.gen.cpp
.venv/bin/python -m ptxsim_codegen.code_gen.lowering \
  --output out/exec_ir_lowering.gen.hpp --source-output out/exec_ir_lowering.gen.cpp
.venv/bin/python -m ptxsim_codegen.code_gen.engine \
  --output out/instruction_preparation.gen.hpp --source-output out/instruction_preparation.gen.cpp
```

The console commands remain `ptxsim-exec-ir-codegen`,
`ptxsim-exec-ir-lowering-codegen` and `ptxsim-inst-engine-codegen`. Optional
`--backend` and `--spec-dir` arguments override the packaged inputs.

Execution preparation templates live in
`src/ptxsim_codegen/code_gen/templates/`. Python derives typed operand paths;
Jinja2 renders preparation and dispatch. Arithmetic semantics, memory-transfer
helpers and commit remain handwritten C++. Templates load from the installed
package with strict undefined variables and HTML escaping disabled. CMake
recursively tracks Python sources, YAML and nested templates, plus the one
package metadata file. Tests are outside the import packages.

## Distribution

Build from the Python project directory through its root-relative path:

```sh
python -m pip wheel ./python --no-deps --wheel-dir out/wheels
python -m build --sdist ./python --outdir out/sdist
```

`build` is an optional local distribution-validation tool, not a runtime
requirement. Build isolation installs setuptools and wheel from
`build-system.requires`. The wheel and source distribution contain the backend
YAML and nested Jinja2 templates. Install a rebuilt wheel to update a regular
installation; editing the checkout does not change installed resources.
