"""Port of Strategist/Strategies.{h,cpp}: checks of the current strategy engine's strategies."""

from __future__ import annotations

from enum import Enum


def is_our_strategy(strategy: Enum) -> bool:
    """Whether the strategy engine is for the strategy's matchup and is currently playing it."""
    import stardust.strategist.strategist as strategist
    from stardust.strategist.strategy_engines.pv_p.pv_p import PvP
    from stardust.strategist.strategy_engines.pv_t.pv_t import PvT
    from stardust.strategist.strategy_engines.pv_u.pv_u import PvU
    from stardust.strategist.strategy_engines.pv_z.pv_z import PvZ

    engine = strategist.get_strategy_engine()
    if isinstance(strategy, PvP.OurStrategy):
        return isinstance(engine, PvP) and engine.our_strategy == strategy
    if isinstance(strategy, PvT.OurStrategy):
        return isinstance(engine, PvT) and engine.our_strategy == strategy
    if isinstance(strategy, PvZ.OurStrategy):
        return isinstance(engine, PvZ) and engine.our_strategy == strategy
    if isinstance(strategy, PvU.OurStrategy):
        return isinstance(engine, PvU) and engine.our_strategy == strategy
    return False


def is_enemy_strategy(strategy: Enum) -> bool:
    """Whether the strategy engine is for the strategy's matchup and has recognized it as the enemy's strategy."""
    import stardust.strategist.strategist as strategist
    from stardust.strategist.strategy_engines.pv_p.pv_p import PvP
    from stardust.strategist.strategy_engines.pv_t.pv_t import PvT
    from stardust.strategist.strategy_engines.pv_z.pv_z import PvZ

    engine = strategist.get_strategy_engine()
    if isinstance(strategy, PvP.ProtossStrategy):
        return isinstance(engine, PvP) and engine.enemy_strategy == strategy
    if isinstance(strategy, PvT.TerranStrategy):
        return isinstance(engine, PvT) and engine.enemy_strategy == strategy
    if isinstance(strategy, PvZ.ZergStrategy):
        return isinstance(engine, PvZ) and engine.enemy_strategy == strategy
    return False
