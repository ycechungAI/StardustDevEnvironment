"""Port of Strategist/Plays/Macro/HiddenBase.{h,cpp}: takes a hidden base with a reserved builder while our army is on
the attack."""

from __future__ import annotations

from typing import TYPE_CHECKING

import bwapi
from stardust import common
from stardust.builder import builder
from stardust.instrumentation import cherryvis
from stardust.map import game_map
from stardust.strategist.play import Play, UnitCallback
from stardust.units import units
from stardust.workers import workers

if TYPE_CHECKING:
    from stardust.map.base import Base
    from stardust.units.my_worker import MyWorker


class HiddenBase(Play):
    def __init__(self) -> None:
        super().__init__("HiddenBase")
        self.base: Base | None = None
        self._builder: MyWorker | None = None

    def update(self) -> None:
        import stardust.strategist.strategist as strategist

        # Wait until we know where the hidden base is
        if self.base is None:
            self.base = game_map.get_hidden_base()
            if self.base is None:
                return
        base = self.base

        # Stop the play when:
        # - We are past frame 15000
        # - The builder died
        # - We have taken our natural
        # - The enemy has scouted the base
        if common.current_frame > 15000:
            self.status.complete = True
            return

        if self._builder is not None and not self._builder.exists():
            self.status.complete = True
            return

        natural = game_map.get_my_natural()
        if (natural is not None and natural.owner == bwapi.Broodwar.self() and natural.resource_depot is not None
                and natural.resource_depot.completed):
            self.status.complete = True
            return

        if units.enemy_in_radius(base.get_position(), 640):
            self.status.complete = True
            return

        # Reserve a builder and send it to the base when:
        # - Our main is saturated
        # - Our main army is on the attack
        # - The path to the hidden base is safe
        if self._builder is None:
            if workers.available_mineral_assignments(game_map.get_my_main()) > 0:
                return

            main_army_play = strategist.get_main_army_play()
            if main_army_play is None:
                return
            if main_army_play.is_defensive():
                return

            # TODO: Check path

            self._builder, _ = workers.get_closest_reassignable_worker(base.get_position(), False)
            if self._builder is None:
                return

            cherryvis.log("Reserving for hidden base play", self._builder.id)

            builder.add_reserved_builder(self._builder)
            workers.reserve_worker(self._builder)

        # Ensure the builder moves towards the hidden base when it isn't building anything
        if not builder.has_pending_building(self._builder):
            self._builder.move_to(base.get_position())

    def disband(self, removed_unit_callback: UnitCallback, movable_unit_callback: UnitCallback) -> None:
        if self.base is not None:
            builder.cancel_base(self.base)

        if self._builder is not None:
            cherryvis.log("Releasing from hidden base play", self._builder.id)
            builder.release_reserved_builder(self._builder)
            workers.release_worker(self._builder)

        super().disband(removed_unit_callback, movable_unit_callback)
