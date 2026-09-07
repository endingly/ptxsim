# PTXSim Python generators

The repository root builds one `ptxsim-codegen` wheel. Its `setup.cfg`
maps import packages to their owning C++ modules:

| Import package | Source directory | Responsibility |
| --- | --- | --- |
| `ptxsim_codegen` | `python/ptxsim_codegen` | Shared artifact publishing |
| `ptxsim_codegen.exec_ir` | `submod/exec_ir/python/codegen` | Target model, backend mappings, IR declarations and diagnostics |
| `ptxsim_codegen.exec_ir.instructions` | `submod/exec_ir/python/instructions` | Packaged backend YAML |
| `ptxsim_codegen.exec_ir_lowering` | `submod/exec_ir_lowering/python/codegen` | Frontend-to-execution-IR lowering |

Lowering imports the execution-IR model and mappings; execution-IR generation
does not import lowering. The pinned `ptx_frontend` dependency lives only in
the root package metadata and must match the vcpkg port revision.

## Development

From the repository root:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -e .
.venv/bin/python -m unittest discover -s submod/exec_ir/python/tests -v
.venv/bin/python -m unittest discover -s submod/exec_ir_lowering/python/tests -v
```

For an environment created before the package split, first remove the obsolete
editable distribution with `python -m pip uninstall ptxsim-exec-ir-codegen`
using that environment's interpreter. Reinstall after changing root package
mappings or dependencies; ordinary source/YAML edits are visible immediately
through the editable installation.

The separate entry points use their packaged backend and frontend resources:

```sh
.venv/bin/python -m ptxsim_codegen.exec_ir \
  --output out/exec_ir.gen.hpp --source-output out/exec_ir.gen.cpp
.venv/bin/python -m ptxsim_codegen.exec_ir_lowering \
  --output out/exec_ir_lowering.gen.hpp --source-output out/exec_ir_lowering.gen.cpp
```

The equivalent console commands are `ptxsim-exec-ir-codegen` and
`ptxsim-exec-ir-lowering-codegen`. Both accept optional `--backend` and
`--spec-dir` overrides; normal CMake builds do not need either.

CMake tracks the owning generator sources, shared Python helpers, package
metadata, and backend YAML. Lowering also tracks the execution-IR Python
sources it depends on. Tests are outside the import packages and do not
trigger C++ regeneration.

## Distribution

Build the wheel from the repository root, not an individual submodule:

```sh
python -m pip wheel . --no-deps --wheel-dir out/wheels
```

The wheel contains both generators and the backend YAML. A non-editable wheel
installation uses its bundled files; source changes require rebuilding and
reinstalling that wheel.
