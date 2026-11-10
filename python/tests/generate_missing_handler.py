"""Publish a small real typed entry requiring one intentionally missing handler."""
from pathlib import Path
import sys

from ptxsim_codegen.code_gen.engine import capabilities
from ptxsim_codegen.code_gen.engine.native import _group, _group_includes, _validation_declarations
from ptxsim_codegen.frontend.v080 import snapshot


def main() -> None:
    """Keep the positive and negative link controls on exactly the same generated object."""
    model = snapshot()
    form = next(form for form in model.forms if form.identity.variant == 'exit_bare')
    profile = capabilities.resolve(model, {'version': 1, 'rules': [
        {'form': 'exit/exit_bare', 'handler': 'required_contract', 'layouts': ['default']}]})
    text = (_group_includes((form,)) + _validation_declarations((form,))
            + 'namespace ptxsim::inst_execute_engine::detail {\n'
            + _group(form, profile.for_form(form)) + '}\n'
            + '/** @brief Retain the real generated entry and its required typed symbol at link. */\n'
            + 'int main() { volatile auto entry = &ptxsim::inst_execute_engine::detail::group_ExitBare; return entry == nullptr; }\n')
    Path(sys.argv[1]).write_text(text)


if __name__ == '__main__':
    main()
