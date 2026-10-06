"""Port of Producer/ProductionLocation.h: where to produce something.

Either None (N/A or don't care), a neighbourhood, a specific build location (buildings only) or a base.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from stardust.builder.building_placement import BuildLocation, Neighbourhood

if TYPE_CHECKING:
    from stardust.map.base import Base

type ProductionLocation = Neighbourhood | BuildLocation | Base | None
