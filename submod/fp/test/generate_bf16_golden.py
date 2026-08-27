#!/usr/bin/env python3
"""Emit the fixed BF16 arithmetic/FMA oracle in test_bf16.cpp.

This deliberately uses only Fraction/integer arithmetic.  It neither imports
nor invokes ptxsim/SoftFloat, so the checked-in results remain an independent
oracle for the integer BF16 core.
"""

from fractions import Fraction

INEXACT, UNDERFLOW, OVERFLOW, INVALID = 1, 2, 4, 16
NE, Z, DN, UP = range(4)
MODE = {NE: "NearestEven", Z: "TowardZero", DN: "TowardNegative", UP: "TowardPositive"}


def parts(bits):
    return bool(bits & 0x8000), (bits >> 7) & 0xff, bits & 0x7f


def nan(bits):
    _, exp, frac = parts(bits)
    return exp == 0xff and frac != 0


def snan(bits):
    return nan(bits) and not (bits & 0x40)


def inf(bits):
    _, exp, frac = parts(bits)
    return exp == 0xff and frac == 0


def zero(bits):
    return (bits & 0x7fff) == 0


def finite(bits):
    sign, exp, frac = parts(bits)
    if exp:
        value = Fraction(128 + frac) * Fraction(2) ** (exp - 127 - 7)
    else:
        value = Fraction(frac) * Fraction(2) ** -133
    return -value if sign else value


def power2(exponent):
    return Fraction(2**exponent) if exponent >= 0 else Fraction(1, 2**-exponent)


def floor_log2(value):
    exponent = value.numerator.bit_length() - value.denominator.bit_length()
    return exponent if value >= power2(exponent) else exponent - 1


def rounded_integer(value, negative, mode):
    quotient, remainder = divmod(value.numerator, value.denominator)
    if not remainder:
        return quotient, False
    if mode == NE:
        increment = 2 * remainder > value.denominator or (
            2 * remainder == value.denominator and quotient & 1)
    elif mode == UP:
        increment = not negative
    elif mode == DN:
        increment = negative
    else:
        increment = False
    return quotient + int(increment), True


def overflow_result(negative, mode):
    finite_result = mode == Z or (negative and mode == UP) or (not negative and mode == DN)
    return (0xff7f if negative else 0x7f7f) if finite_result else (0xff80 if negative else 0x7f80)


def round_bf16(value, zero_sign, mode):
    if not value:
        return (0x8000 if zero_sign else 0), 0
    negative = value < 0
    value = abs(value)
    exponent = floor_log2(value)
    if exponent > 127:
        return overflow_result(negative, mode), OVERFLOW | INEXACT
    if exponent < -126:
        significand, inexact = rounded_integer(value / power2(-133), negative, mode)
        if significand >= 128:
            return (0x8080 if negative else 0x0080), int(inexact)
        bits = significand | (0x8000 if negative else 0)
        return bits, (INEXACT | UNDERFLOW) if inexact else 0
    significand, inexact = rounded_integer(value / power2(exponent - 7), negative, mode)
    if significand == 256:
        significand = 128
        exponent += 1
    if exponent > 127:
        return overflow_result(negative, mode), OVERFLOW | INEXACT
    bits = ((exponent + 127) << 7) | (significand - 128)
    return bits | (0x8000 if negative else 0), INEXACT if inexact else 0


def quiet(bits):
    return bits | 0x40


def nan_result(a, b):
    selected = a if nan(a) else b
    return quiet(selected), INVALID if snan(a) or snan(b) else 0


def oracle(operation, a, b, mode):
    if nan(a) or nan(b):
        return nan_result(a, b)
    if operation == "s":
        b ^= 0x8000
    sign = bool((a ^ b) & 0x8000)
    if operation in "as":
        if inf(a) and inf(b):
            return (0x7fc0, INVALID) if (a ^ b) & 0x8000 else (a, 0)
        if inf(a):
            return a, 0
        if inf(b):
            return b, 0
        value = finite(a) + finite(b)
        return round_bf16(value, mode == DN, mode)
    if inf(a) or inf(b):
        return (0x7fc0, INVALID) if zero(a) or zero(b) else ((0xff80 if sign else 0x7f80), 0)
    return round_bf16(finite(a) * finite(b), sign, mode)


def fma_oracle(a, b, c, mode):
    """Exact a*b+c with precisely one final BF16 rounding."""
    product_sign = bool((a ^ b) & 0x8000)
    invalid_product = (inf(a) and zero(b)) or (zero(a) and inf(b))
    # This is intentionally before NaN propagation: it matches the explicit
    # architecture policy for inf*0 + cNaN, rather than Python evaluation.
    if invalid_product:
        return 0x7fc0, INVALID
    if nan(a) or nan(b) or nan(c):
        selected = a if nan(a) else (b if nan(b) else c)
        return quiet(selected), INVALID if snan(a) or snan(b) or snan(c) else 0
    if inf(a) or inf(b):
        if inf(c) and bool(c & 0x8000) != product_sign:
            return 0x7fc0, INVALID
        return (0xff80 if product_sign else 0x7f80), 0
    if inf(c):
        return c, 0
    product_zero = zero(a) or zero(b)
    if product_zero and zero(c):
        product_sign = bool((a ^ b) & 0x8000)
        c_sign = bool(c & 0x8000)
        sign = product_sign if product_sign == c_sign else mode == DN
        return (0x8000 if sign else 0), 0
    value = finite(a) * finite(b) + finite(c)
    return round_bf16(value, mode == DN, mode)


def round_f32(value, zero_sign, mode):
    """Independent exact binary32 rounding, used only to label regressions."""
    if not value:
        return Fraction(-0 if zero_sign else 0)
    negative = value < 0
    magnitude = abs(value)
    exponent = floor_log2(magnitude)
    if exponent < -126:
        significand, _ = rounded_integer(magnitude / power2(-149), negative, mode)
        return (-1 if negative else 1) * Fraction(significand) * power2(-149)
    significand, _ = rounded_integer(magnitude / power2(exponent - 23), negative, mode)
    if significand == 1 << 24:
        significand >>= 1
        exponent += 1
    return (-1 if negative else 1) * Fraction(significand) * power2(exponent - 23)


def naive_f32_muladd_then_bf16(a, b, c, mode):
    """The prohibited implementation, for checked regression witnesses."""
    intermediate = round_f32(finite(a) * finite(b) + finite(c), mode == DN, mode)
    return round_bf16(intermediate, mode == DN, mode)


# Exact arithmetic, halfway/directed rounding, gradual underflow, cancellation,
# large exponent gaps, overflow, signed zero, and NaN/infinity flags.
CASES = [
    ("a", 0x3f80, 0x0000, NE), ("a", 0x3f80, 0x3f80, NE),
    ("a", 0xc000, 0x3f00, NE), ("s", 0x3fc0, 0x3f00, NE),
    ("s", 0xbf80, 0x3f00, NE), ("m", 0x3fc0, 0x4000, NE),
    ("m", 0xbf80, 0x3f80, NE), ("a", 0x3f80, 0x3b80, NE),
    ("a", 0x3f80, 0x3b80, Z), ("a", 0x3f80, 0x3b80, DN),
    ("a", 0x3f80, 0x3b80, UP), ("a", 0xbf80, 0xbb80, NE),
    ("a", 0xbf80, 0xbb80, Z), ("a", 0xbf80, 0xbb80, DN),
    ("a", 0xbf80, 0xbb80, UP), ("m", 0x3f81, 0x3f81, NE),
    ("m", 0x3f81, 0x3f81, Z), ("m", 0x3f81, 0x3f81, DN),
    ("m", 0x3f81, 0x3f81, UP), ("m", 0xbf81, 0x3f81, NE),
    ("m", 0xbf81, 0x3f81, Z), ("m", 0xbf81, 0x3f81, DN),
    ("m", 0xbf81, 0x3f81, UP), ("a", 0x3f80, 0x0001, NE),
    ("a", 0x3f80, 0x0001, UP), ("a", 0xbf80, 0x8001, DN),
    ("a", 0x0001, 0x0001, NE), ("a", 0x007f, 0x0001, NE),
    ("s", 0x0080, 0x0001, NE), ("m", 0x0001, 0x3f00, NE),
    ("m", 0x0001, 0x3f00, UP), ("m", 0x8001, 0x3f00, DN),
    ("s", 0x3f80, 0x3f80, NE), ("s", 0x3f80, 0x3f80, DN),
    ("a", 0x0000, 0x8000, NE), ("a", 0x0000, 0x8000, DN),
    ("m", 0x8000, 0x3f80, NE), ("m", 0x8000, 0xbf80, NE),
    ("m", 0x7f7f, 0x4000, NE), ("m", 0x7f7f, 0x4000, Z),
    ("m", 0xff7f, 0x4000, DN), ("a", 0x7f7f, 0x7f7f, UP),
    ("a", 0x7f80, 0x3f80, NE), ("a", 0xff80, 0x3f80, NE),
    ("a", 0x7f80, 0xff80, NE), ("s", 0x7f80, 0x7f80, NE),
    ("m", 0x7f80, 0xbf80, NE), ("m", 0x7f80, 0x0000, NE),
    ("m", 0xff80, 0x8000, NE), ("a", 0x7fc1, 0x3f80, NE),
    ("a", 0x3f80, 0xffc2, NE), ("m", 0x7f82, 0x3f80, NE),
    ("a", 0x7fc1, 0x7f82, NE), ("s", 0x7f82, 0x7fc1, NE),
    ("m", 0xff82, 0x7f80, NE), ("a", 0x0080, 0x8080, UP),
    ("a", 0x0080, 0x8080, DN), ("s", 0x0001, 0x8001, NE),
    ("m", 0x007f, 0x3f81, NE), ("m", 0x007f, 0x3f81, UP),
    ("a", 0x7e80, 0x3f80, NE), ("a", 0x0081, 0x8080, NE),
]

# `double-rounding`: exact FMA differs from the prohibited F32 mulAdd followed
# by BF16 narrowing.  `tiny-midpoint`: a one-subnormal c changes an exact
# product's BF16 midpoint direction.  All cases are Fraction-oracle generated.
FMA_CASES = [
    ("exact", 0x3f80, 0x4000, 0x3f80, NE),
    ("exact-negative", 0xbf80, 0x4000, 0xbf80, NE),
    ("halfway-ne", 0x3f81, 0x3f81, 0x0000, NE),
    ("halfway-zero", 0x3f81, 0x3f81, 0x0000, Z),
    ("halfway-negative", 0xbf81, 0x3f81, 0x0000, DN),
    ("halfway-positive", 0xbf81, 0x3f81, 0x0000, UP),
    ("midpoint-base", 0x3f82, 0x3fa0, 0x0000, NE),
    ("tiny-midpoint-double-rounding", 0x3f82, 0x3fa0, 0x0001, NE),
    ("subnormal-exact", 0x0001, 0x3f80, 0x0001, NE),
    ("subnormal-underflow", 0x0001, 0x3f00, 0x0000, NE),
    ("subnormal-directed", 0x8001, 0x3f00, 0x0000, DN),
    ("overflow-ne", 0x7f7f, 0x4000, 0x3f80, NE),
    ("overflow-zero", 0x7f7f, 0x4000, 0x3f80, Z),
    ("cancel-plus-zero", 0x3f80, 0x3f80, 0xbf80, NE),
    ("cancel-minus-zero", 0x3f80, 0x3f80, 0xbf80, DN),
    ("signed-zero-same", 0x8000, 0x3f80, 0x8000, NE),
    ("signed-zero-opposite", 0x8000, 0x3f80, 0x0000, DN),
    ("qnan-a", 0x7fc1, 0x3f80, 0x3f80, NE),
    ("qnan-b", 0x3f80, 0xffc2, 0x3f80, NE),
    ("qnan-c", 0x3f80, 0x3f80, 0xffc3, NE),
    ("snan-a", 0x7f85, 0x3f80, 0x3f80, NE),
    ("snan-b", 0x3f80, 0xff86, 0x3f80, NE),
    ("snan-c", 0x3f80, 0x3f80, 0x7f84, NE),
    ("inf-product", 0xff80, 0x3f80, 0x3f80, NE),
    ("inf-c", 0x3f80, 0x3f80, 0xff80, NE),
    ("inf-plus-opposite-inf", 0x7f80, 0x3f80, 0xff80, NE),
    ("inf-zero", 0x7f80, 0x0000, 0x3f80, NE),
    ("inf-zero-cnan", 0x7f80, 0x0000, 0x7fc1, NE),
]


def main():
    regression = (0x3f82, 0x3fa0, 0x0001, NE)
    if fma_oracle(*regression) != (0x3fa3, INEXACT):
        raise RuntimeError("tiny-midpoint exact FMA regression changed")
    if naive_f32_muladd_then_bf16(*regression) != (0x3fa2, INEXACT):
        raise RuntimeError("expected F32-to-BF16 double-rounding witness lost")
    for operation, a, b, mode in CASES:
        value, flags = oracle(operation, a, b, mode)
        print(f"    {{'{operation}', 0x{a:04X}u, 0x{b:04X}u, RoundingMode::{MODE[mode]}, 0x{value:04X}u, 0x{flags:02X}u}},")
    print("\nFMA:")
    for label, a, b, c, mode in FMA_CASES:
        value, flags = fma_oracle(a, b, c, mode)
        print(f'    {{"{label}", 0x{a:04X}u, 0x{b:04X}u, 0x{c:04X}u, '
              f'RoundingMode::{MODE[mode]}, 0x{value:04X}u, 0x{flags:02X}u}},')


if __name__ == "__main__":
    main()
