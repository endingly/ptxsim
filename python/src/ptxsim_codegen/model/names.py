"""Owned C++ output naming, independent of semantic and frontend field IDs."""

import re

_CPP_KEYWORDS = frozenset(('alignas alignof and and_eq asm auto bitand bitor bool break case catch '
    'char class compl concept const consteval constexpr constinit const_cast continue '
    'co_await co_return co_yield decltype default delete do double dynamic_cast else enum '
    'explicit export extern false float for friend goto if inline int long mutable namespace '
    'new noexcept not not_eq nullptr operator or or_eq private protected public register '
    'reinterpret_cast requires return short signed sizeof static static_assert static_cast '
    'struct switch template this thread_local throw true try typedef typeid typename union '
    'unsigned using virtual void volatile wchar_t while xor xor_eq').split())


def cpp_identifier(value: str) -> str:
    """Produce an owned C++ identifier without changing semantic field identity."""
    result = re.sub(r'[^A-Za-z0-9_]', '_', value)
    if not result or result[0].isdigit():
        result = 'v_' + result
    return result + '_' if result in _CPP_KEYWORDS else result



def control_member_name(value: str) -> str:
    """Avoid collisions with the closed instruction API without aliasing semantics."""
    reserved = {'kind', 'opcode', 'operands', 'Controls', 'execution_predicate',
                'static_contract', 'get_if', 'instruction_kind', 'instruction_opcode'}
    return cpp_identifier('control_' + value if value in reserved else value)
