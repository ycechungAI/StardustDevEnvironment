"""Port of Instrumentation/LogFormattingUtil.h."""

from collections.abc import Iterable, Mapping


def format_probability_map(probabilities: Mapping[object, float], n: int = 3) -> str:
    """The n most likely entries, e.g. "A (50.00%), B (25.00%)"."""
    top = sorted(probabilities.items(), key=lambda item: item[1], reverse=True)[:n]
    return ", ".join(f"{key} ({value * 100.0:.2f}%)" for key, value in top)


def format_vectorlike(values: Iterable[object]) -> str:
    return "[" + ",".join(str(value) for value in values) + "]"
