"""Port of Units/MyCarrier.h and Units/MyUnit/MyCarrier.cpp."""

from __future__ import annotations

import bwapi
from bwapi import UnitTypes
from stardust.units.my_unit import MyUnit


class MyCarrier(MyUnit):
    __slots__ = ()

    def update(self, unit: bwapi.Unit | None) -> None:
        if unit is None or not unit.exists():
            return
        super().update(unit)

        # Build interceptors as needed
        if not unit.isTraining() and unit.canTrain(UnitTypes.Protoss_Interceptor):
            unit.train(UnitTypes.Protoss_Interceptor)
