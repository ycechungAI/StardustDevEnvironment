"""Port of Map/StartingLocation.h: a start location's main and natural bases and chokes."""

from __future__ import annotations

from dataclasses import dataclass

from stardust.map.base import Base
from stardust.map.choke import Choke


@dataclass(eq=False)
class StartingLocation:
    main: Base
    natural: Base | None
    main_choke: Choke | None
    natural_choke: Choke | None
