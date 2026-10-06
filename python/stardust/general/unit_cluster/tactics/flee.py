"""Port of General/UnitCluster/Tactics/Flee.cpp: retreats the cluster towards our main.

Stardust's arc-forming flee logic is disabled ("we can't really do this safely"), so only the move is ported.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

from stardust.map import game_map

if TYPE_CHECKING:
    from stardust.general.unit_cluster.unit_cluster import UnitCluster
    from stardust.units.unit import Unit


def flee(cluster: UnitCluster, enemy_units: set[Unit]) -> None:
    my_main = game_map.get_my_main()
    assert my_main is not None
    cluster.move(my_main.get_position())
