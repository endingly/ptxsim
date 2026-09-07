"""Generate executor wiring, never instruction semantics, from execution bindings."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import importlib
from importlib.resources import as_file, files
import json
from pathlib import Path
import re
from typing import Any

import yaml

from .cpp_names import instruction_cpp_name, layout_cpp_name, op_enum_name, variant_cpp_name
from .model import BackendSpec, GenerationError, ProjectedForm, ProjectedInstruction


class _UniqueLoader(yaml.SafeLoader):
    """Reject duplicate mapping keys rather than silently dropping a binding."""


def _mapping(loader: _UniqueLoader, node: yaml.MappingNode, deep: bool = False) -> dict:
    """Construct a mapping without YAML merge keys or duplicate-key precedence."""
    result = {}
    for key_node, value_node in node.value:
        key = loader.construct_object(key_node, deep=deep)
        if not isinstance(key, str) or key in result:
            raise GenerationError(f"duplicate or non-string mapping key: {key!r}")
        result[key] = loader.construct_object(value_node, deep=deep)
    return result


_UniqueLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _mapping)


@dataclass(frozen=True)
class EngineBinding:
    """A validated connection to one existing opcode/form/operand layout."""

    id: str
    instruction: ProjectedInstruction
    form: ProjectedForm
    layout: Any
    modifiers: dict[str, tuple[Any, ...]]
    ignored: dict[str, str]
    function: str
    arguments: tuple[Any, ...]
    commit: str


def load_bindings(path: Path) -> dict:
    """Read execution policy with strict YAML keys and actionable diagnostics."""
    try:
        return yaml.load(path.read_text(encoding="utf-8"), Loader=_UniqueLoader)
    except yaml.YAMLError as error:
        raise GenerationError(f"invalid execution YAML: {error}") from error


def _keys(value: Any, required: set[str], optional: set[str], context: str) -> None:
    """Require a mapping with an explicitly bounded set of string keys."""
    if not isinstance(value, dict) or not all(isinstance(key, str) for key in value):
        raise GenerationError(f"{context}: expected a string-keyed mapping")
    missing, unknown = required - value.keys(), value.keys() - required - optional
    if missing or unknown:
        raise GenerationError(f"{context}: missing {sorted(missing)}, unknown {sorted(unknown)}")


def _identifier(value: Any, context: str) -> str:
    """Accept a C++ identifier, not an embedded expression or function body."""
    if not isinstance(value, str) or not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]*", value):
        raise GenerationError(f"{context}: expected an identifier")
    return value


def _value_key(value: Any) -> tuple[type, Any]:
    """Keep YAML booleans distinct from integers during domain validation."""
    if type(value) not in (str, bool, int):
        raise GenerationError(f"expected a scalar modifier value, got {value!r}")
    return type(value), value


def _domain(modifier: Any) -> set[tuple[type, Any]]:
    """Collect normalized values, respecting fixed selectors and flag defaults."""
    if modifier.presence == "fixed":
        return {_value_key(modifier.value)}
    values = {_value_key(item.value) for item in modifier.values}
    if modifier.default is not None:
        values.add(_value_key(modifier.default))
    if modifier.kind == "flag":
        values.update({_value_key(False), _value_key(True)})
    return values


def _argument(argument: Any, modifiers: dict, backend: BackendSpec) -> None:
    """Validate the small adapter vocabulary; arbitrary C++ is not accepted."""
    if isinstance(argument, str):
        if argument not in {"resolver", "registers", "thread", "arithmetic", "operation", "form", "successor"}:
            raise GenerationError(f"unknown adapter argument: {argument!r}")
    elif isinstance(argument, dict) and set(argument) == {"modifier"}:
        if not isinstance(argument["modifier"], str) or argument["modifier"] not in modifiers:
            raise GenerationError(f"unknown modifier argument: {argument!r}")
    else:
        _keys(argument, {"kind", "value"}, set(), "constant argument")
        kind = argument["kind"]
        if not isinstance(kind, str) or kind not in backend.modifier_kinds:
            raise GenerationError(f"unknown constant kind: {kind!r}")
        values = {_value_key(value) for value in backend.modifier_kinds[kind].values}
        if _value_key(argument["value"]) not in values:
            raise GenerationError(f"unknown constant value: {argument!r}")


def bind(document: dict, backend: BackendSpec,
         projected: tuple[ProjectedInstruction, ...]) -> tuple[EngineBinding, ...]:
    """Resolve policy against frontend topology and reject ambiguity or drift."""
    _keys(document, {"schema", "default", "bindings"}, set(), "execution policy")
    if document["schema"] != "ptxsim-engine/v1" or document["default"] != "unsupported":
        raise GenerationError("expected ptxsim-engine/v1 with default: unsupported")
    if not isinstance(document["bindings"], list) or not document["bindings"]:
        raise GenerationError("bindings must be a non-empty list")
    instructions = {item.opcode: item for item in projected}
    result: list[EngineBinding] = []
    ids: set[str] = set()
    for raw in document["bindings"]:
        _keys(raw, {"id", "opcode", "form", "modifiers", "prepare", "commit"},
              {"layout", "ignored"}, "binding")
        name = _identifier(raw["id"], "binding id")
        if name in ids:
            raise GenerationError(f"duplicate binding id: {name}")
        ids.add(name)
        if not isinstance(raw["opcode"], str) or raw["opcode"] not in instructions:
            raise GenerationError(f"{name}: unknown opcode {raw['opcode']!r}")
        instruction = instructions[raw["opcode"]]
        forms = {item.variant.name: item for item in instruction.forms}
        if not isinstance(raw["form"], str) or raw["form"] not in forms:
            raise GenerationError(f"{name}: unknown form {raw['form']!r}")
        form = forms[raw["form"]]
        layouts = {item.name: item for item in form.layouts}
        layout_id = raw.get("layout")
        if "layout" not in raw and len(layouts) == 1:
            layout_id = next(iter(layouts))
        if not isinstance(layout_id, str) or layout_id not in layouts:
            raise GenerationError(f"{name}: select one existing operand layout from {sorted(layouts)}")
        modifiers = {item.name: item for item in form.variant.modifiers if item.presence != "absent"}
        selected, ignored = raw["modifiers"], raw.get("ignored", {})
        _keys(selected, set(), set(modifiers), f"{name} modifiers")
        _keys(ignored, set(), set(modifiers), f"{name} ignored modifiers")
        if selected.keys() & ignored.keys():
            raise GenerationError(f"{name}: modifier is both matched and ignored")
        dynamic = {key for key, value in modifiers.items() if value.presence != "fixed"}
        if dynamic - selected.keys() - ignored.keys():
            raise GenerationError(f"{name}: unhandled dynamic modifiers {sorted(dynamic - selected.keys() - ignored.keys())}")
        if any(not isinstance(reason, str) or not reason.strip() for reason in ignored.values()):
            raise GenerationError(f"{name}: ignored modifiers require a rationale")
        constraints = {}
        for key, values in selected.items():
            if not isinstance(values, list) or not values:
                raise GenerationError(f"{name}/{key}: expected a non-empty value list")
            keys = [_value_key(value) for value in values]
            if len(set(keys)) != len(keys) or not set(keys) <= _domain(modifiers[key]):
                raise GenerationError(f"{name}/{key}: duplicate or illegal modifier values")
            constraints[key] = tuple(sorted(values, key=lambda value: (type(value).__name__, str(value))))
        prepare = raw["prepare"]
        _keys(prepare, {"function", "arguments"}, set(), f"{name} prepare")
        function = _identifier(prepare["function"], f"{name} function")
        if not isinstance(prepare["arguments"], list):
            raise GenerationError(f"{name}: arguments must be a list")
        for argument in prepare["arguments"]:
            _argument(argument, modifiers, backend)
        if raw["commit"] not in ("scalar", "warp_sync"):
            raise GenerationError(f"{name}: unknown commit protocol")
        binding = EngineBinding(name, instruction, form, layouts[layout_id], constraints,
                                ignored, function, tuple(prepare["arguments"]), raw["commit"])
        for other in result:
            if (other.instruction.opcode, other.form.variant.name, other.layout.name) != (instruction.opcode, form.variant.name, layout_id):
                continue
            common = other.modifiers.keys() & constraints.keys()
            if all({_value_key(v) for v in other.modifiers[key]} &
                   {_value_key(v) for v in constraints[key]} for key in common):
                raise GenerationError(f"overlapping execution bindings: {other.id}, {name}")
        result.append(binding)
    return tuple(sorted(result, key=lambda item: item.id))


def _cpp_value(backend: BackendSpec, kind: str, value: Any) -> str:
    """Qualify an existing backend enum spelling for the engine namespace."""
    mapping = backend.modifier_kinds[kind]
    expression = mapping.cpp_value(value)
    if mapping.cpp_type == "bool":
        return expression
    return f"::{backend.namespace}::{expression}"


def _form_type(binding: EngineBinding) -> str:
    """Reuse exec-IR naming rather than inventing a second C++ name mapping."""
    opcode = binding.instruction.opcode
    return f"exec_ir::{instruction_cpp_name(opcode)}::{variant_cpp_name(opcode, binding.form.variant.name)}"


def _emit_adapter(binding: EngineBinding, backend: BackendSpec) -> str:
    """Adapt the uniform lane handler signature to one handwritten preparer."""
    arguments = binding.arguments
    needs_form = "form" in arguments or any(isinstance(arg, dict) and "modifier" in arg for arg in arguments)
    lines = [f"/** @brief Prepare the `{binding.id}` binding without committing effects. */",
             f"auto prepare_binding_{binding.id}(",
             "    [[maybe_unused]] LaneResourceResolver& resolver,",
             "    [[maybe_unused]] const arith::context& arithmetic,",
             "    [[maybe_unused]] const exec_ir::Instruction& operation,",
             "    [[maybe_unused]] std::optional<common::ProgramCounter> successor)",
             "    -> std::expected<PreparedEffect, LaneFaultCause> {"]
    if "registers" in arguments:
        lines += ["  const auto registers = resolver.resolve();", "  if (!registers)",
                  "    return std::unexpected(registers.error());"]
    if "operation" in arguments or needs_form:
        lines.append(f"  const auto& typed = std::get<exec_ir::{instruction_cpp_name(binding.instruction.opcode)}>(operation);")
    if needs_form:
        lines.append(f"  const auto& form = std::get<{_form_type(binding)}>(typed.variant);")
    atoms = {"resolver": "resolver", "registers": "registers->get()", "thread": "resolver.thread()",
             "arithmetic": "arithmetic", "operation": "typed", "form": "form", "successor": "*successor"}
    expressions = []
    for argument in arguments:
        if isinstance(argument, str):
            expressions.append(atoms[argument])
        elif "modifier" in argument:
            expressions.append(f"form.{argument['modifier']}")
        else:
            expressions.append(_cpp_value(backend, argument["kind"], argument["value"]))
    lines += [f"  return {binding.function}({', '.join(expressions)});", "}", ""]
    return "\n".join(lines)


def source(bindings: tuple[EngineBinding, ...], backend: BackendSpec) -> str:
    """Emit private adapters and fail-closed dispatch into one includable unit."""
    output = ["// Generated from execution bindings. Do not edit.",
              "// Included inside the engine's anonymous namespace.", ""]
    output.extend(_emit_adapter(item, backend) for item in bindings)
    output += ["/** @brief Select a registered form/layout or reject it before lane access. */",
               "auto select_preparer(const exec_ir::Instruction& operation)",
               "    -> std::expected<SelectedPreparer, StepErrorCode> {",
               "  switch (exec_ir::op(operation)) {"]
    for opcode in sorted({item.instruction.opcode for item in bindings}):
        output.append(f"    case exec_ir::Op::{op_enum_name(opcode)}: {{")
        for item in (item for item in bindings if item.instruction.opcode == opcode):
            output += [f"      if (const auto* form = std::get_if<{_form_type(item)}>(",
                       f"              &std::get<exec_ir::{instruction_cpp_name(opcode)}>(operation).variant)) {{"]
            conditions = []
            if len(item.form.layouts) != 1:
                layout = f"{_form_type(item)}::{layout_cpp_name(item.layout.name)}"
                conditions.append(f"std::holds_alternative<{layout}>(form->operands)")
            modifiers = {modifier.name: modifier for modifier in item.form.variant.modifiers}
            for key, values in sorted(item.modifiers.items()):
                expressions = [f"form->{key} == {_cpp_value(backend, modifiers[key].kind, value)}" for value in values]
                conditions.append("(" + " || ".join(expressions) + ")")
            condition = " &&\n            ".join(conditions) or "true"
            output += [f"        if ({condition})",
                       f"          return SelectedPreparer{{prepare_binding_{item.id}, PrepareKind::{item.commit}}};",
                       "      }"]
        output += ["      return unsupported_instruction();", "    }"]
    output += ["    default:", "      return unsupported_instruction();", "  }", "}", ""]
    return "\n".join(output)


def coverage(bindings: tuple[EngineBinding, ...], projected: tuple[ProjectedInstruction, ...]) -> str:
    """Report conditional mappings separately from declaration-only layouts."""
    mapped = [{"id": item.id, "opcode": item.instruction.opcode, "form": item.form.variant.name,
               "layout": item.layout.name, "modifiers": item.modifiers, "ignored": item.ignored,
               "function": item.function, "arguments": item.arguments, "commit": item.commit}
              for item in bindings]
    declared = []
    for instruction in sorted(projected, key=lambda item: item.opcode):
        for form in sorted(instruction.forms, key=lambda item: item.variant.name):
            for layout in sorted(form.layouts, key=lambda item: item.name):
                ids = [item.id for item in bindings if (item.instruction.opcode, item.form.variant.name, item.layout.name) ==
                       (instruction.opcode, form.variant.name, layout.name)]
                declared.append({"opcode": instruction.opcode, "form": form.variant.name, "layout": layout.name,
                                 "bindings": ids, "status": "conditional" if ids else "unsupported"})
    return json.dumps({"schema": "ptxsim-engine-coverage/v1", "default": "unsupported",
                       "bindings": mapped, "declared": declared}, indent=2, sort_keys=True) + "\n"


def _dependencies(bindings: Path, backend: Path, spec_dir: Path | None) -> list[Path]:
    """Track installed generator/specification files, including editable installs."""
    result = {bindings.resolve(), backend.resolve()}
    roots = {Path(__file__).resolve().parent}
    for name in ("ptx_frontend.base", "ptx_frontend.ir", "ptx_frontend.spec", "ptx_frontend.code_gen"):
        module = importlib.import_module(name)
        roots.update(Path(path).resolve() for path in module.__path__)
    if spec_dir is not None:
        roots.add(spec_dir.resolve())
    for root in roots:
        result.update(path.resolve() for path in root.rglob("*") if path.is_file() and path.suffix in {".py", ".yaml", ".yml"})
        # A newly added specification must also invalidate the generated outputs.
        result.add(root)
        result.update(path.resolve() for path in root.rglob("*") if path.is_dir() and "__pycache__" not in path.parts)
    return sorted(result)


def _escape(path: Path) -> str:
    """Escape a filesystem path for a CMake-compatible Make/Ninja depfile."""
    return path.resolve().as_posix().replace("$", "$$").replace("#", r"\#").replace(" ", r"\ ").replace(":", r"\:")


def main() -> None:
    """Validate the installed frontend projection and atomically publish wiring."""
    from .backend import load_yaml
    from .cli import _write
    from .projection import database, project_database

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bindings", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--coverage", type=Path, required=True)
    parser.add_argument("--depfile", type=Path)
    parser.add_argument("--backend", type=Path)
    parser.add_argument("--spec-dir", type=Path)
    args = parser.parse_args()
    resource = args.backend or files("ptxsim_exec_ir_codegen.instructions").joinpath("backend.yaml")
    try:
        with as_file(resource) as backend_path:
            backend = load_yaml(backend_path)
            projected = project_database(database(args.spec_dir), backend)
            bindings = bind(load_bindings(args.bindings), backend, projected)
            generated, report = source(bindings, backend), coverage(bindings, projected)
            dependencies = _dependencies(args.bindings, backend_path, args.spec_dir) if args.depfile else []
            _write(args.output, generated)
            _write(args.coverage, report)
            if args.depfile:
                _write(args.depfile, f"{_escape(args.output)}: {' '.join(_escape(path) for path in dependencies)}\n")
    except (GenerationError, ImportError, OSError, ValueError) as error:
        raise SystemExit(f"engine generation error: {error}") from error


if __name__ == "__main__":
    main()
