"""Port of Units/MyCannon.h and Units/MyUnit/MyCannon.cpp."""

from __future__ import annotations

import bwapi
from bwapi import UnitCommandTypes
from stardust import common
from stardust.units.my_unit import MyUnit
from stardust.units.unit import Unit


class MyCannon(MyUnit):
    __slots__ = ()

    def unstick(self) -> bool:
        return False

    def is_ready(self) -> bool:
        if not super().is_ready() or self.issued_order_this_frame:
            return False

        game = bwapi.Broodwar
        frame = common.current_frame

        # We aren't ready while on cooldown, adjusted for latency
        if self.cooldown_until > frame + game.getRemainingLatencyFrames() + 1:
            return False

        # We aren't ready if we have just sent the attack command
        assert self.bwapi_unit is not None
        if (self.bwapi_unit.getLastCommand().type == UnitCommandTypes.Attack_Unit
                and self.last_command_frame >= frame - game.getLatencyFrames()):
            return False

        return True

    def attack_unit(self, target: Unit, units_and_targets: list[tuple[MyUnit, Unit | None]] | None = None,
                    cluster_attacking: bool = True, enemy_aoe_radius: int = 0) -> None:
        # Abort the attack if the target isn't visible
        if target.bwapi_unit is None or not target.bwapi_unit.isVisible():
            return

        # We always force the attack to reset the order process timer (is_ready ensures we don't resend too often)
        self.attack(target.bwapi_unit, True)
