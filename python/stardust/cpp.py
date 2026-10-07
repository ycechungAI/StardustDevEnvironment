"""Helpers that reproduce C++ arithmetic semantics where they differ from Python's.

Use these when translating Stardust code whose operands can be negative or whose results are rounded:
- C++ integer `/` and `%` truncate toward zero; Python's `//` and `%` floor.
- `std::round` rounds halves away from zero; Python's `round` rounds halves to even.
- C++ `float` is 32-bit; Python floats are 64-bit.
- Casting a floating point value to int truncates toward zero, as `int()` does.
- Floating point division by zero and log(0) give infinities in C++; Python raises.
"""

import math
import struct

INT_MAX = 2**31 - 1
INT_MIN = -(2**31)
USHRT_MAX = 2**16 - 1
LONG_MAX = 2**63 - 1


def cdiv(a: int, b: int) -> int:
    """C++ integer division (truncates toward zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def cmod(a: int, b: int) -> int:
    """C++ integer remainder (takes the sign of the dividend)."""
    return a - b * cdiv(a, b)


def cround(x: float) -> int:
    """std::round: halves round away from zero."""
    if math.isnan(x):
        return 0
    return int(math.floor(x + 0.5)) if x >= 0 else -int(math.floor(-x + 0.5))


_FLOAT = struct.Struct("f")


def f32(x: float) -> float:
    """Round to 32-bit float precision. Apply after each operation to emulate C++ `float` arithmetic."""
    return float(_FLOAT.unpack(_FLOAT.pack(x))[0])


def wrap_i32(x: int) -> int:
    """32-bit signed integer overflow as it happens in practice (two's complement wraparound)."""
    return ((x + 2**31) & 0xFFFFFFFF) - 2**31


def to_int(x: float) -> int:
    """`(int)x` for a floating point value: truncates toward zero. NaN becomes 0 and infinities saturate (as on
    ARM)."""
    if math.isnan(x):
        return 0
    if math.isinf(x):
        return INT_MAX if x > 0 else INT_MIN
    return int(x)


def fdiv(a: float, b: float) -> float:
    """IEEE floating point division: dividing by zero gives an infinity (NaN for 0/0) instead of raising."""
    if b == 0.0:
        if a == 0.0 or math.isnan(a):
            return math.nan
        return math.copysign(math.inf, a) * math.copysign(1.0, b)
    return a / b


def clog(x: float) -> float:
    """std::log: -inf for 0 and NaN for negative values instead of raising."""
    if x == 0.0:
        return -math.inf
    if x < 0.0 or math.isnan(x):
        return math.nan
    return math.log(x)


def clog10(x: float) -> float:
    """std::log10: -inf for 0 and NaN for negative values instead of raising."""
    if x == 0.0:
        return -math.inf
    if x < 0.0 or math.isnan(x):
        return math.nan
    return math.log10(x)
