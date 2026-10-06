# Porting Stardust to Python

Goal: a faithful Python port of the [Stardust](https://github.com/bmnielsen/Stardust) bot's logic, running on the
Python host in this repo. Local development only (no tournament packaging).

Upstream reference: commit `22d93d7a55d0a0494384474a456fd7ee26baee97`. Keep a checkout at `.reference/Stardust`
(gitignored) while porting:

```bash
git clone https://github.com/bmnielsen/Stardust.git .reference/Stardust && git -C .reference/Stardust checkout 22d93d7
```

Stardust is © Bruce Mackenzie Nielsen under the Stardust License (see `LICENSE-STARDUST`). Note its condition: works
containing substantial portions may not be entered in public StarCraft tournaments without the author's permission.

## What stays native (C++, pybind11)

| Piece | Why | Python module |
|---|---|---|
| BWAPI | the game interface | `bwapi` (generated) |
| BWEM (map analysis, MIT) | ~6k lines of geometry; runs at startup and answers area/choke queries constantly | `bwem` |
| FAP (combat sim, MIT, Stardust-modified) | simulates up to 288 frames per cluster per frame | `fap` |

Everything else is Python. Hot data structures use numpy (threat/collision grids). Profile before moving anything else
to C++.

## Conventions

- Package `python/stardust/` mirrors `src/`: one module per C++ file pair, snake_case names. Split implementation
  files (`Unit_Info.cpp`, `MyUnit_Move.cpp`, `Map_Walkability.cpp`, ...) merge into their class's module.
- C++ namespaces (`Map`, `Units`, `Workers`, ...) become modules with module-level state, reset by `initialize()`.
  `Map` becomes `stardust/map/game_map.py` so it doesn't shadow the `map` builtin. Import namespace modules as
  modules (`from stardust.units import units`) and call `units.all_mine()`, mirroring `Units::allMine()`.
- `XImpl` classes held by `shared_ptr` (`typedef std::shared_ptr<UnitImpl> Unit`) become plain classes named after the
  typedef: `Unit`, `MyUnit`, `MyWorker`, ... `nullptr` becomes `None`. Raw BWAPI objects are always `bwapi.Unit` etc.
- Functions, methods and variables are snake_case; classes CamelCase; constants UPPER_CASE.
- `currentFrame` is `common.current_frame` (`from stardust import common`); read it as an attribute, never import the
  value.
- Containers: `std::set`/`unordered_set` of objects → `set`; `std::vector` → `list`; `std::deque` → `collections.deque`;
  `std::map` → `dict`. **Where C++ relies on sorted iteration of a `std::map`/`std::set` (first element, ordered
  loop), sort explicitly.**
- BWAPI positions are immutable in Python: `pos.x += 1` becomes `pos = Position(pos.x + 1, pos.y)`.
- Compile-time flags (`LOGGING_ENABLED`, `INSTRUMENTATION_ENABLED`, `DEBUG_*`) are constants in `stardust/config.py`.
  Verbose-only debug output (CSV dumps, per-frame heatmaps) may be omitted; note omissions in the module docstring.
- Every module is fully typed and must pass `uv run mypy` (strict). This is the main safety net for translation slips,
  since most of the bot can only be exercised in a real game.
- Translate behaviour, not just syntax: integer division (`//`), `>>`/`<<` on ints, and `int` truncation toward zero
  (`int(a / b)`, not `a // b`, when operands can be negative) must match C++.

## Phases

1. Native infrastructure: vendor BWEM and FAP; `bwem` and `fap` bindings; bind `BWAPI::Event` (`getEvents`).
2. Foundations: `config`, `common`, instrumentation (`log`, `cherryvis`, `timer`), `util/*`.
3. Map: `game_map`, `base`, `choke`, `no_go_areas`, `path_finding/*`, map-specific overrides.
4. Units & players: `units/*`, `bullets`, `players/*`.
5. Workers (initially with `MineralLockingOptimization`; port `MiningOptimizationV2` afterwards).
6. Builder & building placement (incl. walls and blocks).
7. Producer.
8. General: squads, unit clusters, combat sim, tactics, formations.
9. Strategist: plays, strategy engines (PvP, PvT, PvZ, PvU, map-specific).
10. `opponent`, `stardust_ai_module`, wire into `stardust.create_bot()`; first full game.

## Status

Helpers: `stardust/cpp.py` reproduces C++ integer division, rounding and 32-bit float semantics; use it.

### native

- [x] vendor `3rdparty/BWEM`, `3rdparty/FAP` from upstream (FAP patched to take the map size, so it works offline)
- [x] `bwem` bindings (`src/python/BWEMModule.cpp`, stub `python/bwem.pyi`)
- [x] `fap` bindings (`src/python/FAPModule.cpp`, stub `python/fap.pyi`): owns the simulate/score loop of CombatSim.cpp `execute`
- [x] `bwapi.Event` / `Game.getEvents()`
- [x] switch `3rdparty/openbw`, `3rdparty/BWAPILIB` and `test/3rdparty/opponents` to Stardust's fork (unit speed fixes, `getBWID`, `getVisibleUnits`, ...; adds Iron and McRave opponents)
- [x] bind `ExactPosition` / `ExactPositionDifference` and `Unit.getExactPosition()` (hand-written in `BWAPIBindings.h`; gather-path simulation is only used by Stardust's offline training harness, so it isn't bound)

### builder

- [x] `python/stardust/builder/block.py` (273) ← `Builder/Block.cpp`, `Builder/Block.h`
- [x] `python/stardust/builder/blocks/block_10x3.py` (35) ← `Builder/Blocks/10x3.h`
- [x] `python/stardust/builder/blocks/block_10x6.py` (47) ← `Builder/Blocks/10x6.h`
- [x] `python/stardust/builder/blocks/block_12x5.py` (64) ← `Builder/Blocks/12x5.h`
- [x] `python/stardust/builder/blocks/block_12x8.py` (152) ← `Builder/Blocks/12x8.h`
- [x] `python/stardust/builder/blocks/block_13x6.py` (93) ← `Builder/Blocks/13x6.h`
- [x] `python/stardust/builder/blocks/block_14x3.py` (39) ← `Builder/Blocks/14x3.h`
- [x] `python/stardust/builder/blocks/block_14x6.py` (57) ← `Builder/Blocks/14x6.h`
- [x] `python/stardust/builder/blocks/block_16x5.py` (79) ← `Builder/Blocks/16x5.h`
- [x] `python/stardust/builder/blocks/block_16x8.py` (131) ← `Builder/Blocks/16x8.h`
- [x] `python/stardust/builder/blocks/block_17x6.py` (109) ← `Builder/Blocks/17x6.h`
- [x] `python/stardust/builder/blocks/block_18x3.py` (43) ← `Builder/Blocks/18x3.h`
- [x] `python/stardust/builder/blocks/block_18x6.py` (65) ← `Builder/Blocks/18x6.h`
- [x] `python/stardust/builder/blocks/block_2x2.py` (27) ← `Builder/Blocks/2x2.h`
- [x] `python/stardust/builder/blocks/block_2x4.py` (28) ← `Builder/Blocks/2x4.h`
- [x] `python/stardust/builder/blocks/block_4x2.py` (28) ← `Builder/Blocks/4x2.h`
- [x] `python/stardust/builder/blocks/block_4x4.py` (30) ← `Builder/Blocks/4x4.h`
- [x] `python/stardust/builder/blocks/block_4x5.py` (32) ← `Builder/Blocks/4x5.h`
- [x] `python/stardust/builder/blocks/block_4x8.py` (40) ← `Builder/Blocks/4x8.h`
- [x] `python/stardust/builder/blocks/block_5x2.py` (29) ← `Builder/Blocks/5x2.h`
- [x] `python/stardust/builder/blocks/block_5x4.py` (32) ← `Builder/Blocks/5x4.h`
- [x] `python/stardust/builder/blocks/block_6x3.py` (31) ← `Builder/Blocks/6x3.h`
- [x] `python/stardust/builder/blocks/block_8x2.py` (31) ← `Builder/Blocks/8x2.h`
- [x] `python/stardust/builder/blocks/block_8x5.py` (58) ← `Builder/Blocks/8x5.h`
- [x] `python/stardust/builder/blocks/block_8x8.py` (49) ← `Builder/Blocks/8x8.h`
- [x] `python/stardust/builder/blocks/start_above_and_below_left.py` (142) ← `Builder/Blocks/StartAboveAndBelowLeft.h`
- [x] `python/stardust/builder/blocks/start_bottom_horizontal.py` (134) ← `Builder/Blocks/StartBottomHorizontal.h`
- [x] `python/stardust/builder/blocks/start_bottom_left_horizontal.py` (128) ← `Builder/Blocks/StartBottomLeftHorizontal.h`
- [x] `python/stardust/builder/blocks/start_compact_left.py` (126) ← `Builder/Blocks/StartCompactLeft.h`
- [x] `python/stardust/builder/blocks/start_compact_left_horizontal.py` (68) ← `Builder/Blocks/StartCompactLeftHorizontal.h`
- [x] `python/stardust/builder/blocks/start_compact_right.py` (126) ← `Builder/Blocks/StartCompactRight.h`
- [x] `python/stardust/builder/blocks/start_compact_right_vertical.py` (132) ← `Builder/Blocks/StartCompactRightVertical.h`
- [x] `python/stardust/builder/blocks/start_normal_left.py` (70) ← `Builder/Blocks/StartNormalLeft.h`
- [x] `python/stardust/builder/blocks/start_normal_right.py` (70) ← `Builder/Blocks/StartNormalRight.h`
- [x] `python/stardust/builder/blocks/start_top_left_horizontal.py` (130) ← `Builder/Blocks/StartTopLeftHorizontal.h`
- [x] `python/stardust/builder/builder.py` (553) ← `Builder/Builder.cpp`, `Builder/Builder.h`
- [x] `python/stardust/builder/building.py` (159) ← `Builder/Building.cpp`, `Builder/Building.h`
- [x] `python/stardust/builder/building_placement.py` (3609) ← `Builder/BuildingPlacement.cpp`, `Builder/BuildingPlacement.h`, `Builder/BuildingPlacement_Walls.cpp`
- [x] `python/stardust/builder/forge_gateway_wall.py` (142) ← `Builder/ForgeGatewayWall.h`

### (root)

- [x] `python/stardust/bullets.py` (678) ← `Bullets.cpp`, `Bullets.h`
- [x] `python/stardust/common.py` (15) ← `Common.h`
- [x] n/a: C++ host (`src/python/PythonAIModule.cpp`) ← `Dll.cpp`

### general

- [x] `python/stardust/general/combat_sim_result.py` (160) ← `General/CombatSimResult.cpp`, `General/CombatSimResult.h`
- [x] `python/stardust/general/enemy_army.py` (37) ← `General/EnemyArmy.cpp`, `General/EnemyArmy.h`
- [x] `python/stardust/general/general.py` (325) ← `General/General.cpp`, `General/General.h`
- [x] `python/stardust/general/squad.py` (511) ← `General/Squad.cpp`, `General/Squad.h`
- [x] `python/stardust/general/squads/attack_base_squad.py` (539) ← `General/Squads/AttackBaseSquad.cpp`, `General/Squads/AttackBaseSquad.h`
- [x] `python/stardust/general/squads/corsair_squad.py` (853) ← `General/Squads/CorsairSquad.cpp`, `General/Squads/CorsairSquad.h`
- [x] `python/stardust/general/squads/defend_base_squad.py` (86) ← `General/Squads/DefendBaseSquad.cpp`, `General/Squads/DefendBaseSquad.h`
- [x] `python/stardust/general/squads/defend_wall_squad.py` (100) ← `General/Squads/DefendWallSquad.cpp`, `General/Squads/DefendWallSquad.h`
- [x] `python/stardust/general/squads/early_game_defend_main_base_squad.py` (603) ← `General/Squads/EarlyGameDefendMainBaseSquad.cpp`, `General/Squads/EarlyGameDefendMainBaseSquad.h`
- [x] `python/stardust/general/squads/mop_up_squad.py` (204) ← `General/Squads/MopUpSquad.cpp`, `General/Squads/MopUpSquad.h`
- [x] `python/stardust/general/squads/worker_defense_squad.py` (248) ← `General/Squads/WorkerDefenseSquad.cpp`, `General/Squads/WorkerDefenseSquad.h`
- [x] `python/stardust/general/unit_cluster/arbiters.py` (268) ← `General/UnitCluster/Arbiters.cpp`
- [x] `python/stardust/general/unit_cluster/combat_sim.py` (739) ← `General/UnitCluster/CombatSim.cpp`
- [x] `python/stardust/general/unit_cluster/detectors.py` (362) ← `General/UnitCluster/Detectors.cpp`
- [x] `python/stardust/general/unit_cluster/formations/arc.py` (130) ← `General/UnitCluster/Formations/Arc.cpp`
- [x] `python/stardust/general/unit_cluster/formations/ball.py` (176) ← `General/UnitCluster/Formations/Ball.cpp`
- [x] `python/stardust/general/unit_cluster/tactics/attack.py` (136) ← `General/UnitCluster/Tactics/Attack.cpp`
- [x] `python/stardust/general/unit_cluster/tactics/contain_static.py` (287) ← `General/UnitCluster/Tactics/ContainStatic.cpp`
- [x] `python/stardust/general/unit_cluster/tactics/flee.py` (88) ← `General/UnitCluster/Tactics/Flee.cpp`
- [x] `python/stardust/general/unit_cluster/tactics/hold_choke.py` (384) ← `General/UnitCluster/Tactics/HoldChoke.cpp`
- [x] `python/stardust/general/unit_cluster/tactics/move.py` (99) ← `General/UnitCluster/Tactics/Move.cpp`
- [x] `python/stardust/general/unit_cluster/tactics/regroup.py` (507) ← `General/UnitCluster/Tactics/Regroup.cpp`
- [x] `python/stardust/general/unit_cluster/tactics/stand_ground.py` (61) ← `General/UnitCluster/Tactics/StandGround.cpp`
- [x] `python/stardust/general/unit_cluster/targeting.py` (792) ← `General/UnitCluster/Targeting.cpp`
- [x] `python/stardust/general/unit_cluster/unit_cluster.py` (471) ← `General/UnitCluster.h`, `General/UnitCluster/UnitCluster.cpp`

### instrumentation

- [x] `python/stardust/instrumentation/cherryvis.py` (821) ← `Instrumentation/CherryVis.cpp`, `Instrumentation/CherryVis.h`
- [x] `python/stardust/config.py` (11) ← `Instrumentation/DebugFlag_CombatSim.h`
- [x] `python/stardust/config.py` (5) ← `Instrumentation/DebugFlag_GridUpdates.h`
- [x] `python/stardust/config.py` (18) ← `Instrumentation/DebugFlag_MiningOptimization.h`
- [x] `python/stardust/config.py` (6) ← `Instrumentation/DebugFlag_UnitOrders.h`
- [x] `python/stardust/config.py` (12) ← `Instrumentation/DebugFlag_WorkerMiningOptimization.h`
- [x] `python/stardust/instrumentation/log.py` (314) ← `Instrumentation/Log.cpp`, `Instrumentation/Log.h`
- [x] `python/stardust/instrumentation/log_formatting_util.py` (45) ← `Instrumentation/LogFormattingUtil.h`
- [x] `python/stardust/instrumentation/timer.py` (99) ← `Instrumentation/Timer.cpp`, `Instrumentation/Timer.h`

### map

- [x] `python/stardust/map/base.py` (403) ← `Map/Base.cpp`, `Map/Base.h`
- [x] `python/stardust/map/choke.py` (1071) ← `Map/Choke.cpp`, `Map/Choke.h`
- [x] `python/stardust/map/game_map.py` (2535) ← `Map/Map.cpp`, `Map/Map.h`, `Map/Map_Walkability.cpp`
- [x] `python/stardust/map/map_specific_override.py` (87) ← `Map/MapSpecificOverride.h`
- [x] `python/stardust/map/map_specific_overrides/alchemist.py` (131) ← `Map/MapSpecificOverrides/Alchemist.cpp`, `Map/MapSpecificOverrides/Alchemist.h`
- [x] `python/stardust/map/map_specific_overrides/arcadia.py` (103) ← `Map/MapSpecificOverrides/Arcadia.cpp`, `Map/MapSpecificOverrides/Arcadia.h`
- [x] `python/stardust/map/map_specific_overrides/colosseum.py` (185) ← `Map/MapSpecificOverrides/Colosseum.cpp`, `Map/MapSpecificOverrides/Colosseum.h`
- [x] `python/stardust/map/map_specific_overrides/crossing_field.py` (36) ← `Map/MapSpecificOverrides/CrossingField.cpp`, `Map/MapSpecificOverrides/CrossingField.h`
- [x] `python/stardust/map/map_specific_overrides/destination.py` (16) ← `Map/MapSpecificOverrides/Destination.cpp`, `Map/MapSpecificOverrides/Destination.h`
- [x] `python/stardust/map/map_specific_overrides/fortress.py` (74) ← `Map/MapSpecificOverrides/Fortress.cpp`, `Map/MapSpecificOverrides/Fortress.h`
- [x] `python/stardust/map/map_specific_overrides/gods_garden.py` (46) ← `Map/MapSpecificOverrides/GodsGarden.cpp`, `Map/MapSpecificOverrides/GodsGarden.h`
- [x] `python/stardust/map/map_specific_overrides/judgment_day.py` (60) ← `Map/MapSpecificOverrides/JudgmentDay.cpp`, `Map/MapSpecificOverrides/JudgmentDay.h`
- [x] `python/stardust/map/map_specific_overrides/katrina.py` (47) ← `Map/MapSpecificOverrides/Katrina.cpp`, `Map/MapSpecificOverrides/Katrina.h`
- [x] `python/stardust/map/map_specific_overrides/match_point.py` (36) ← `Map/MapSpecificOverrides/MatchPoint.cpp`, `Map/MapSpecificOverrides/MatchPoint.h`
- [x] `python/stardust/map/map_specific_overrides/neo_sylphid.py` (19) ← `Map/MapSpecificOverrides/NeoSylphid.h`
- [x] `python/stardust/map/map_specific_overrides/outsider.py` (85) ← `Map/MapSpecificOverrides/Outsider.cpp`, `Map/MapSpecificOverrides/Outsider.h`
- [x] `python/stardust/map/map_specific_overrides/plasma.py` (403) ← `Map/MapSpecificOverrides/Plasma.cpp`, `Map/MapSpecificOverrides/Plasma.h`
- [x] `python/stardust/map/map_specific_overrides/roadkill.py` (19) ← `Map/MapSpecificOverrides/Roadkill.h`
- [x] `python/stardust/map/no_go_areas.py` (386) ← `Map/NoGoAreas.cpp`, `Map/NoGoAreas.h`
- [x] `python/stardust/map/path_finding/navigation_grid.py` (636) ← `Map/PathFinding/NavigationGrid.cpp`, `Map/PathFinding/NavigationGrid.h`
- [x] `python/stardust/map/path_finding/path_finding.py` (803) ← `Map/PathFinding/PathFinding.cpp`, `Map/PathFinding/PathFinding.h`, `Map/PathFinding/PathFinding_BWEM.cpp`, `Map/PathFinding/PathFinding_Grids.cpp`, `Map/PathFinding/PathFinding_Search.cpp`
- [x] `python/stardust/map/starting_location.py` (21) ← `Map/StartingLocation.h`

### (root)

- [x] `python/stardust/opponent.py` (472) ← `Opponent.cpp`, `Opponent.h`

### players

- [x] `python/stardust/players/grid.py` (504) ← `Players/Grid.cpp`, `Players/Grid.h`
- [x] `python/stardust/players/players.py` (235) ← `Players/Players.cpp`, `Players/Players.h`
- [x] `python/stardust/players/upgrade_tracker.py` (420) ← `Players/UpgradeTracker.cpp`, `Players/UpgradeTracker.h`

### producer

- [x] `python/stardust/producer/producer.py` (2376) ← `Producer/Producer.cpp`, `Producer/Producer.h`
- [x] `python/stardust/producer/production_goal.py` (20) ← `Producer/ProductionGoal.cpp`, `Producer/ProductionGoal.h`
- [x] `python/stardust/producer/production_goals/unit_production_goal.py` (127) ← `Producer/ProductionGoals/UnitProductionGoal.h`
- [x] `python/stardust/producer/production_goals/upgrade_production_goal.py` (50) ← `Producer/ProductionGoals/UpgradeProductionGoal.cpp`, `Producer/ProductionGoals/UpgradeProductionGoal.h`
- [x] `python/stardust/producer/production_location.py` (9) ← `Producer/ProductionLocation.h`

### (root)

- [x] `python/stardust/stardust_ai_module.py` (522) ← `StardustAIModule.cpp`, `StardustAIModule.h`

### strategist

- [x] `python/stardust/strategist/opponent_economic_model.py` (1503) ← `Strategist/OpponentEconomicModel.cpp`, `Strategist/OpponentEconomicModel.h`
- [x] `python/stardust/strategist/play.py` (152) ← `Strategist/Play.cpp`, `Strategist/Play.h`
- [x] `python/stardust/strategist/plays/defensive/anti_cannon_rush.py` (381) ← `Strategist/Plays/Defensive/AntiCannonRush.cpp`, `Strategist/Plays/Defensive/AntiCannonRush.h`
- [x] `python/stardust/strategist/plays/defensive/defend_base.py` (445) ← `Strategist/Plays/Defensive/DefendBase.cpp`, `Strategist/Plays/Defensive/DefendBase.h`
- [x] `python/stardust/strategist/plays/macro/cull_army.py` (105) ← `Strategist/Plays/Macro/CullArmy.cpp`, `Strategist/Plays/Macro/CullArmy.h`
- [x] `python/stardust/strategist/plays/macro/hidden_base.py` (112) ← `Strategist/Plays/Macro/HiddenBase.cpp`, `Strategist/Plays/Macro/HiddenBase.h`
- [x] `python/stardust/strategist/plays/macro/saturate_bases.py` (126) ← `Strategist/Plays/Macro/SaturateBases.cpp`, `Strategist/Plays/Macro/SaturateBases.h`
- [x] `python/stardust/strategist/plays/macro/take_expansion.py` (331) ← `Strategist/Plays/Macro/TakeExpansion.cpp`, `Strategist/Plays/Macro/TakeExpansion.h`
- [x] `python/stardust/strategist/plays/macro/take_island_expansion.py` (384) ← `Strategist/Plays/Macro/TakeIslandExpansion.cpp`, `Strategist/Plays/Macro/TakeIslandExpansion.h`
- [x] `python/stardust/strategist/plays/main_army/attack_enemy_base.py` (30) ← `Strategist/Plays/MainArmy/AttackEnemyBase.cpp`, `Strategist/Plays/MainArmy/AttackEnemyBase.h`
- [x] `python/stardust/strategist/plays/main_army/defend_my_main.py` (334) ← `Strategist/Plays/MainArmy/DefendMyMain.cpp`, `Strategist/Plays/MainArmy/DefendMyMain.h`
- [x] `python/stardust/strategist/plays/main_army/forge_fast_expand.py` (1034) ← `Strategist/Plays/MainArmy/ForgeFastExpand.cpp`, `Strategist/Plays/MainArmy/ForgeFastExpand.h`
- [x] `python/stardust/strategist/plays/main_army/main_army_play.py` (61) ← `Strategist/Plays/MainArmy/MainArmyPlay.cpp`, `Strategist/Plays/MainArmy/MainArmyPlay.h`
- [x] `python/stardust/strategist/plays/main_army/mop_up.py` (25) ← `Strategist/Plays/MainArmy/MopUp.cpp`, `Strategist/Plays/MainArmy/MopUp.h`
- [x] `python/stardust/strategist/plays/offensive/attack_expansion.py` (127) ← `Strategist/Plays/Offensive/AttackExpansion.cpp`, `Strategist/Plays/Offensive/AttackExpansion.h`
- [x] `python/stardust/strategist/plays/offensive/attack_island_expansion.py` (79) ← `Strategist/Plays/Offensive/AttackIslandExpansion.cpp`, `Strategist/Plays/Offensive/AttackIslandExpansion.h`
- [x] `python/stardust/strategist/plays/scouting/early_game_worker_scout.py` (968) ← `Strategist/Plays/Scouting/EarlyGameWorkerScout.cpp`, `Strategist/Plays/Scouting/EarlyGameWorkerScout.h`
- [x] `python/stardust/strategist/plays/scouting/eject_enemy_scout.py` (176) ← `Strategist/Plays/Scouting/EjectEnemyScout.cpp`, `Strategist/Plays/Scouting/EjectEnemyScout.h`
- [x] `python/stardust/strategist/plays/scouting/scout_enemy_expos.py` (337) ← `Strategist/Plays/Scouting/ScoutEnemyExpos.cpp`, `Strategist/Plays/Scouting/ScoutEnemyExpos.h`
- [x] `python/stardust/strategist/plays/special_teams/carrier_harass.py` (166) ← `Strategist/Plays/SpecialTeams/CarrierHarass.cpp`, `Strategist/Plays/SpecialTeams/CarrierHarass.h`
- [x] `python/stardust/strategist/plays/special_teams/corsairs.py` (49) ← `Strategist/Plays/SpecialTeams/Corsairs.cpp`, `Strategist/Plays/SpecialTeams/Corsairs.h`
- [x] `python/stardust/strategist/plays/special_teams/dark_templar_harass.py` (370) ← `Strategist/Plays/SpecialTeams/DarkTemplarHarass.cpp`, `Strategist/Plays/SpecialTeams/DarkTemplarHarass.h`
- [x] `python/stardust/strategist/plays/special_teams/elevator.py` (672) ← `Strategist/Plays/SpecialTeams/Elevator.cpp`, `Strategist/Plays/SpecialTeams/Elevator.h`
- [x] `python/stardust/strategist/plays/special_teams/elevator_rush.py` (390) ← `Strategist/Plays/SpecialTeams/ElevatorRush.cpp`, `Strategist/Plays/SpecialTeams/ElevatorRush.h`
- [x] `python/stardust/strategist/plays/special_teams/shuttle_harass.py` (542) ← `Strategist/Plays/SpecialTeams/ShuttleHarass.cpp`, `Strategist/Plays/SpecialTeams/ShuttleHarass.h`
- [x] `python/stardust/strategist/strategies.py` (100) ← `Strategist/Strategies.cpp`, `Strategist/Strategies.h`
- [x] `python/stardust/strategist/strategist.py` (804) ← `Strategist/Strategist.cpp`, `Strategist/Strategist.h`
- [x] `python/stardust/strategist/strategy_engine.py` (137) ← `Strategist/StrategyEngine.h`
- [x] `python/stardust/strategist/strategy_engines/common/attack_plays.py` (231) ← `Strategist/StrategyEngines/Common/AttackPlays.cpp`
- [x] `python/stardust/strategist/strategy_engines/common/defensive_cannons.py` (137) ← `Strategist/StrategyEngines/Common/DefensiveCannons.cpp`
- [x] `python/stardust/strategist/strategy_engines/common/expansions.py` (520) ← `Strategist/StrategyEngines/Common/Expansions.cpp`
- [x] `python/stardust/strategist/strategy_engines/common/strategy_engine.py` (565) ← `Strategist/StrategyEngines/Common/StrategyEngine.cpp`
- [x] `python/stardust/strategist/strategy_engines/common/upgrades.py` (222) ← `Strategist/StrategyEngines/Common/Upgrades.cpp`
- [x] `python/stardust/strategist/strategy_engines/map_specific/plasma_enemy_strategy_recognizer.py` (75) ← `Strategist/StrategyEngines/MapSpecific/PlasmaEnemyStrategyRecognizer.cpp`
- [x] `python/stardust/strategist/strategy_engines/map_specific/plasma_strategy_engine.py` (243) ← `Strategist/StrategyEngines/MapSpecific/PlasmaStrategyEngine.cpp`, `Strategist/StrategyEngines/MapSpecific/PlasmaStrategyEngine.h`
- [x] `python/stardust/strategist/strategy_engines/pv_p/pv_p.py` (1174) ← `Strategist/StrategyEngines/PvP/PvP.cpp`, `Strategist/StrategyEngines/PvP.h`
- [x] `python/stardust/strategist/strategy_engines/pv_p/pv_p_enemy_strategy_recognizer.py` (588) ← `Strategist/StrategyEngines/PvP/PvPEnemyStrategyRecognizer.cpp`
- [x] `python/stardust/strategist/strategy_engines/pv_p/pv_p_strategy_selection.py` (440) ← `Strategist/StrategyEngines/PvP/PvPStrategySelection.cpp`
- [x] `python/stardust/strategist/strategy_engines/pv_t/pv_t.py` (743) ← `Strategist/StrategyEngines/PvT/PvT.cpp`, `Strategist/StrategyEngines/PvT.h`
- [x] `python/stardust/strategist/strategy_engines/pv_t/pv_t_enemy_strategy_recognizer.py` (436) ← `Strategist/StrategyEngines/PvT/PvTEnemyStrategyRecognizer.cpp`
- [x] `python/stardust/strategist/strategy_engines/pv_t/pv_t_strategy_selection.py` (276) ← `Strategist/StrategyEngines/PvT/PvTStrategySelection.cpp`
- [x] `python/stardust/strategist/strategy_engines/pv_u/pv_u.py` (60) ← `Strategist/StrategyEngines/PvU/PvU.cpp`, `Strategist/StrategyEngines/PvU.h`
- [x] `python/stardust/strategist/strategy_engines/pv_z/pv_z.py` (956) ← `Strategist/StrategyEngines/PvZ/PvZ.cpp`, `Strategist/StrategyEngines/PvZ.h`
- [x] `python/stardust/strategist/strategy_engines/pv_z/pv_z_enemy_strategy_recognizer.py` (412) ← `Strategist/StrategyEngines/PvZ/PvZEnemyStrategyRecognizer.cpp`
- [x] `python/stardust/strategist/strategy_engines/pv_z/pv_z_strategy_selection.py` (279) ← `Strategist/StrategyEngines/PvZ/PvZStrategySelection.cpp`

### units

- [x] `python/stardust/units/enemy_bunker.py` (79) ← `Units/EnemyBunker.cpp`, `Units/EnemyBunker.h`
- [x] `python/stardust/units/my_cannon.py` (50) ← `Units/MyCannon.h`, `Units/MyUnit/MyCannon.cpp`
- [x] `python/stardust/units/my_carrier.py` (29) ← `Units/MyCarrier.h`, `Units/MyUnit/MyCarrier.cpp`
- [x] `python/stardust/units/my_corsair.py` (233) ← `Units/MyCorsair.h`, `Units/MyUnit/MyCorsair.cpp`
- [x] `python/stardust/units/my_dragoon.py` (338) ← `Units/MyDragoon.h`, `Units/MyUnit/MyDragoon.cpp`
- [x] `python/stardust/units/my_observer.py` (74) ← `Units/MyObserver.h`
- [x] `python/stardust/units/my_unit.py` (1467) ← `Units/MyUnit.h`, `Units/MyUnit/MyUnit.cpp`, `Units/MyUnit/MyUnit_Move.cpp`, `Units/MyUnit/MyUnit_Orders.cpp`
- [x] `python/stardust/units/my_worker.py` (626) ← `Units/MyUnit/MyWorker.cpp`, `Units/MyWorker.h`
- [x] `python/stardust/units/resource.py` (183) ← `Units/Resource.cpp`, `Units/Resource.h`
- [x] `python/stardust/units/unit.py` (1353) ← `Units/Unit.h`, `Units/Unit/Unit_Info.cpp`, `Units/Unit/Unit_Update.cpp`
- [x] `python/stardust/units/units.py` (1644) ← `Units/Units.cpp`, `Units/Units.h`

### util

- [x] `python/stardust/util/boids.py` (482) ← `Util/Boids.cpp`, `Util/Boids.h`
- [x] `python/stardust/util/csv_tools.py` (70) ← `Util/CsvTools.cpp`, `Util/CsvTools.h`
- [x] `python/stardust/util/file_tools.py` (66) ← `Util/FileTools.cpp`, `Util/FileTools.h`
- [x] `python/stardust/util/geo.py` (625) ← `Util/Geo.cpp`, `Util/Geo.h`
- [x] n/a (Python classes aren't implicitly copied) ← `Util/Noncopyable.h`
- [x] `python/stardust/util/order_process_timer.py` (245) ← `Util/OrderProcessTimer.cpp`, `Util/OrderProcessTimer.h`
- [ ] `python/stardust/util/tile_position.py` (65) ← `Util/TilePosition.h` — only used by MiningOptimizationV2; port with it
- [x] `python/stardust/util/unit_util.py` (364) ← `Util/UnitUtil.cpp`, `Util/UnitUtil.h`
- [x] `python/stardust/util/upgrade_or_tech_type.py` (124) ← `Util/UpgradeOrTechType.cpp`, `Util/UpgradeOrTechType.h`

### workers

- [x] `python/stardust/workers/mineral_locking_optimization/mineral_locking_optimization.py` (137) ← `Workers/MineralLockingOptimization/MineralLockingOptimization.cpp`, `Workers/MineralLockingOptimization/MineralLockingOptimization.h`
- [ ] `python/stardust/workers/mining_optimization_v2/data_model/cannon_placement.py` (62) ← `Workers/MiningOptimizationV2/DataModel/CannonPlacement.h`
- [ ] `python/stardust/workers/mining_optimization_v2/data_model/deserialized_path_cache.py` (133) ← `Workers/MiningOptimizationV2/DataModel/DeserializedPathCache.h`
- [ ] `python/stardust/workers/mining_optimization_v2/data_model/gather_arrival_data.py` (147) ← `Workers/MiningOptimizationV2/DataModel/GatherArrivalData.cpp`, `Workers/MiningOptimizationV2/DataModel/GatherArrivalData.h`
- [ ] `python/stardust/workers/mining_optimization_v2/data_model/map_data.py` (131) ← `Workers/MiningOptimizationV2/DataModel/MapData.h`
- [ ] `python/stardust/workers/mining_optimization_v2/data_model/path.py` (89) ← `Workers/MiningOptimizationV2/DataModel/Path.h`
- [ ] `python/stardust/workers/mining_optimization_v2/data_model/position_and_velocity.py` (122) ← `Workers/MiningOptimizationV2/DataModel/PositionAndVelocity.h`
- [ ] `python/stardust/workers/mining_optimization_v2/data_model/position_delta_and_velocity.py` (110) ← `Workers/MiningOptimizationV2/DataModel/PositionDeltaAndVelocity.h`
- [ ] `python/stardust/workers/mining_optimization_v2/data_model/return_arrival_data.py` (167) ← `Workers/MiningOptimizationV2/DataModel/ReturnArrivalData.cpp`, `Workers/MiningOptimizationV2/DataModel/ReturnArrivalData.h`
- [ ] `python/stardust/workers/mining_optimization_v2/data_model/serialization.py` (213) ← `Workers/MiningOptimizationV2/DataModel/Serialization.cpp`, `Workers/MiningOptimizationV2/DataModel/Serialization.h`
- [ ] `python/stardust/workers/mining_optimization_v2/data_model/serialized_path.py` (273) ← `Workers/MiningOptimizationV2/DataModel/SerializedPath.cpp`, `Workers/MiningOptimizationV2/DataModel/SerializedPath.h`
- [ ] `python/stardust/workers/mining_optimization_v2/mining_optimization.py` (542) ← `Workers/MiningOptimizationV2/MiningOptimization.cpp`, `Workers/MiningOptimizationV2/MiningOptimization.h`
- [ ] `python/stardust/workers/mining_optimization_v2/mining_optimization_configuration.py` (33) ← `Workers/MiningOptimizationV2/MiningOptimizationConfiguration.h`
- [ ] `python/stardust/workers/mining_optimization_v2/path_optimizer.py` (69) ← `Workers/MiningOptimizationV2/PathOptimizer.h`
- [ ] `python/stardust/workers/mining_optimization_v2/path_statistics.py` (61) ← `Workers/MiningOptimizationV2/PathStatistics.h`
- [ ] `python/stardust/workers/mining_optimization_v2/solver/solver.py` (554) ← `Workers/MiningOptimizationV2/Solver/Solver.h`, `Workers/MiningOptimizationV2/Solver/Solver_Common.cpp`, `Workers/MiningOptimizationV2/Solver/Solver_Gather.cpp`, `Workers/MiningOptimizationV2/Solver/Solver_Return.cpp`
- [ ] `python/stardust/workers/mining_optimization_v2/solver/solver_result.py` (192) ← `Workers/MiningOptimizationV2/Solver/SolverResult.cpp`, `Workers/MiningOptimizationV2/Solver/SolverResult.h`
- [ ] `python/stardust/workers/mining_optimization_v2/takeover/other_patches_occupied_forecast.py` (123) ← `Workers/MiningOptimizationV2/Takeover/OtherPatchesOccupiedForecast.cpp`, `Workers/MiningOptimizationV2/Takeover/OtherPatchesOccupiedForecast.h`
- [ ] `python/stardust/workers/mining_optimization_v2/takeover/patch_occupied_forecast.py` (497) ← `Workers/MiningOptimizationV2/Takeover/PatchOccupiedForecast.cpp`, `Workers/MiningOptimizationV2/Takeover/PatchOccupiedForecast.h`
- [ ] `python/stardust/workers/mining_optimization_v2/worker_path_optimizer.py` (1105) ← `Workers/MiningOptimizationV2/WorkerPathOptimizer.h`, `Workers/MiningOptimizationV2/WorkerPathOptimizer_Common.cpp`, `Workers/MiningOptimizationV2/WorkerPathOptimizer_Gather.cpp`, `Workers/MiningOptimizationV2/WorkerPathOptimizer_Return.cpp`
- [ ] `python/stardust/workers/order_process_timer_optimization/worker_order_timer.py` (263) ← `Workers/OrderProcessTimerOptimization/WorkerOrderTimer.cpp`, `Workers/OrderProcessTimerOptimization/WorkerOrderTimer.h`
- [x] `python/stardust/workers/worker_gather_optimizer.py` (33) ← `Workers/WorkerGatherOptimizer.h`
- [ ] `python/stardust/workers/worker_mining_instrumentation.py` (917) ← `Workers/WorkerMiningInstrumentation.cpp`, `Workers/WorkerMiningInstrumentation.h`
- [x] `python/stardust/workers/workers.py` (1422) ← `Workers/Workers.cpp`, `Workers/Workers.h`
