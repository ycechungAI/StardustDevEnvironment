"""Reading and writing the binary format of the bitsery C++ library Stardust uses for its mining optimization data:
little-endian fixed-width values, and container sizes in a compact 1, 2 or 4 byte encoding."""

from __future__ import annotations

import struct

_I8 = struct.Struct("<b")
_U16 = struct.Struct("<H")
_I16 = struct.Struct("<h")
_U32 = struct.Struct("<I")


class BitseryError(Exception):
    pass


class Reader:
    __slots__ = ("data", "pos")

    def __init__(self, data: bytes) -> None:
        self.data = data
        self.pos = 0

    def _check(self, count: int) -> None:
        if self.pos + count > len(self.data):
            raise BitseryError("Unexpected end of data")

    def u8(self) -> int:
        self._check(1)
        value = self.data[self.pos]
        self.pos += 1
        return value

    def i8(self) -> int:
        self._check(1)
        value: int = _I8.unpack_from(self.data, self.pos)[0]
        self.pos += 1
        return value

    def u16(self) -> int:
        self._check(2)
        value: int = _U16.unpack_from(self.data, self.pos)[0]
        self.pos += 2
        return value

    def i16(self) -> int:
        self._check(2)
        value: int = _I16.unpack_from(self.data, self.pos)[0]
        self.pos += 2
        return value

    def u32(self) -> int:
        self._check(4)
        value: int = _U32.unpack_from(self.data, self.pos)[0]
        self.pos += 4
        return value

    def size(self, max_size: int = 0x7FFFFFFF) -> int:
        """bitsery's details::readSize."""
        hb = self.u8()
        if hb < 0x80:
            size = hb
        else:
            lb = self.u8()
            if hb & 0x40:
                lw = self.u16()
                size = ((((hb & 0x3F) << 8) | lb) << 16) | lw
            else:
                size = ((hb & 0x7F) << 8) | lb
        if size > max_size:
            raise BitseryError(f"Size {size} exceeds maximum {max_size}")
        return size

    def raw(self, count: int) -> bytes:
        self._check(count)
        value = self.data[self.pos:self.pos + count]
        self.pos += count
        return value


class Writer:
    __slots__ = ("data",)

    def __init__(self) -> None:
        self.data = bytearray()

    def u8(self, value: int) -> None:
        self.data.append(value & 0xFF)

    def i8(self, value: int) -> None:
        self.data += _I8.pack(value)

    def u16(self, value: int) -> None:
        self.data += _U16.pack(value)

    def i16(self, value: int) -> None:
        self.data += _I16.pack(value)

    def u32(self, value: int) -> None:
        self.data += _U32.pack(value)

    def size(self, size: int) -> None:
        """bitsery's details::writeSize."""
        if size < 0x80:
            self.u8(size)
        elif size < 0x4000:
            self.u8((size >> 8) | 0x80)
            self.u8(size)
        else:
            assert size < 0x40000000
            self.u8((size >> 24) | 0xC0)
            self.u8(size >> 16)
            self.u16(size & 0xFFFF)

    def raw(self, value: bytes) -> None:
        self.data += value
