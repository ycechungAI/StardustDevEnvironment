"""Port of Util/CsvTools.{h,cpp}."""

from typing import TextIO


def read_next_line(stream: TextIO, sep: str = ";") -> list[str]:
    line = stream.readline().rstrip("\n")
    return _split(line, sep)


def tokenize_list(text: str, sep: str = ",") -> list[str]:
    return _split(text, sep)


def _split(text: str, sep: str) -> list[str]:
    # std::getline splitting: no trailing empty item, and an empty string gives no items
    items = text.split(sep)
    if items and items[-1] == "":
        items.pop()
    return items
