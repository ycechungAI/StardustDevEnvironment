"""Port of Units/MyObserver.h: an observer, with the activity it is doing for the army it is attached to.

Observers scouting enemy bases are handled fully by the scouting play.
"""

from __future__ import annotations

from enum import Enum

import bwapi
from stardust import common
from stardust.instrumentation import cherryvis
from stardust.units.my_unit import MyUnit


class ObserverActivity(Enum):
    None_ = 0  # The observer has no activity yet
    DetectingEnemy = 1  # Moving to detect a specific enemy so our army can attack it
    EscortingArmy = 2  # Escorting the army, trying to stay close to its vanguard unit
    ScoutingEnemyArmy = 3  # Scouting the enemy army to get better information for our combat sim

    def __str__(self) -> str:
        return "None" if self is ObserverActivity.None_ else self.name


class MyObserver(MyUnit):
    __slots__ = ("_activity", "_frame_activity_updated")

    def __init__(self, unit: bwapi.Unit) -> None:
        super().__init__(unit)
        self._activity = ObserverActivity.None_
        self._frame_activity_updated = -2

    def get_activity(self) -> ObserverActivity:
        if self._frame_activity_updated >= common.current_frame - 1:
            return self._activity
        return ObserverActivity.None_

    def set_activity(self, new_activity: ObserverActivity) -> None:
        if new_activity != self._activity:
            cherryvis.log(f"Activity changed from {self._activity} to {new_activity}", self.id)
        self._activity = new_activity
        self._frame_activity_updated = common.current_frame
