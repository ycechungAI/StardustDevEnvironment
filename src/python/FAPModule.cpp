// Python bindings for FAP, the combat simulator Stardust uses to decide whether to attack or retreat.
//
// Python builds the unit list (see stardust/general/unit_cluster/combat_sim.py); the simulation loop and scoring
// run here, since they iterate up to 288 frames per cluster per game frame. Mirrors the `makeUnit`, `score` and
// `execute` helpers in Stardust's src/General/UnitCluster/CombatSim.cpp.

#include "BWAPIBindings.h"
#include "PythonModules.h"

#include <fap.h>

#include <chrono>
#include <memory>

namespace
{
    // Unit value tables indexed by unit type id, set once per game by Python
    std::vector<int> baseScore;
    std::vector<int> scaledScore;

    int unitValue(const FAP::FAPUnit<> &unit)
    {
        return baseScore[unit.unitType] +
               (scaledScore[unit.unitType] * (unit.health * 3 + unit.shields)) / (unit.maxHealth * 3 + unit.maxShields);
    }

    int score(const std::vector<FAP::FAPUnit<>> *units)
    {
        int result = 0;
        for (auto &unit : *units) result += unitValue(unit);
        return result;
    }

    // The choke a fight happens through. Built once per choke; FAP keeps a reference to the tile sides.
    struct ChokeGeometry
    {
        std::vector<signed char> tileSide;
        BWAPI::Position end1Center, end2Center, end1Exit, end2Exit;
    };

    struct SimUnit
    {
        BWAPI::UnitType unitType;
        BWAPI::Position position, targetPosition;
        int health, shields;
        bool flying;
        float speed;
        int armor, groundCooldown, groundDamage, groundMaxRange, airCooldown, airDamage, airMaxRange, elevation;
        int attackerCount, attackCooldownRemaining;
        bool stimmed, undetected;
        int id, target, collisionValue, collisionValueChoke;
    };

    auto makeUnit(const SimUnit &u)
    {
        return FAP::makeUnit<>()
                .setUnitType(u.unitType)
                .setPosition(u.position)
                .setTargetPosition(u.targetPosition)
                .setHealth(u.health)
                .setShields(u.shields)
                .setFlying(u.flying)
                .setSpeed(u.speed)
                .setArmor(u.armor)
                .setGroundCooldown(u.groundCooldown)
                .setGroundDamage(u.groundDamage)
                .setGroundMaxRange(u.groundMaxRange)
                .setAirCooldown(u.airCooldown)
                .setAirDamage(u.airDamage)
                .setAirMaxRange(u.airMaxRange)
                .setElevation(u.elevation)
                .setAttackerCount(u.attackerCount)
                .setAttackCooldownRemaining(u.attackCooldownRemaining)
                .setSpeedUpgrade(false)
                .setRangeUpgrade(false)
                .setShieldUpgrades(0)
                .setStimmed(u.stimmed)
                .setUndetected(u.undetected)
                .setID(u.id)
                .setTarget(u.target)
                .setCollisionValues(u.collisionValue, u.collisionValueChoke)
                .setData({});
    }

    class CombatSimulator
    {
    public:
        CombatSimulator(int mapWidth, int mapHeight)
                : mapWidth(mapWidth)
                , mapHeight(mapHeight)
                , collisionPlayer1(mapWidth * mapHeight * 4, 0)
                , collisionPlayer2(mapWidth * mapHeight * 4, 0)
        {
            reset(nullptr);
        }

        void reset(std::shared_ptr<ChokeGeometry> geometry)
        {
            std::fill(collisionPlayer1.begin(), collisionPlayer1.end(), 0);
            std::fill(collisionPlayer2.begin(), collisionPlayer2.end(), 0);
            sim = std::make_unique<FAP::FastAPproximation<>>(collisionPlayer1, collisionPlayer2, mapWidth, mapHeight);
            choke = std::move(geometry);
            if (choke)
            {
                sim->setChokeGeometry(choke->tileSide, choke->end1Center, choke->end2Center, choke->end1Exit, choke->end2Exit);
            }
        }

        void addUnit(bool player1, const SimUnit &unit)
        {
            // FAP indexes its collision grid by position without bounds checks
            if (unit.position.x < 0 || unit.position.y < 0 || unit.position.x >= mapWidth * 32 || unit.position.y >= mapHeight * 32)
            {
                throw py::value_error("unit position is outside the map");
            }
            if (choke && static_cast<int>(choke->tileSide.size()) < mapWidth * mapHeight * 4)
            {
                throw py::value_error("choke tile_side must have map_width * map_height * 4 entries");
            }

            if (choke)
            {
                if (player1) sim->addPlayer1<true>(makeUnit(unit));
                else sim->addPlayer2<true>(makeUnit(unit));
            }
            else
            {
                if (player1) sim->addPlayer1<false>(makeUnit(unit));
                else sim->addPlayer2<false>(makeUnit(unit));
            }
        }

        // Simulates frame by frame until maxIterations or the time limit; returns
        // (initial player 1 score, initial player 2 score, final player 1 score, final player 2 score, iterations)
        py::tuple run(int maxIterations, int timeLimitMicroseconds)
        {
            int initial1 = score(sim->getState().first);
            int initial2 = score(sim->getState().second);

            auto startTime = std::chrono::high_resolution_clock::now();
            int i = 0;
            while (true)
            {
                if (choke) sim->simulate<true, true>(1);
                else sim->simulate<true, false>(1);

                i++;
                if (i >= maxIterations) break;

                auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                        std::chrono::high_resolution_clock::now() - startTime).count();
                if (elapsed > timeLimitMicroseconds) break;
            }

            return py::make_tuple(initial1, initial2, score(sim->getState().first), score(sim->getState().second), i);
        }

        // (id, unit type, x, y, health, shields, attack cooldown remaining, target id) for each unit of each player
        py::tuple state()
        {
            auto describe = [](const std::vector<FAP::FAPUnit<>> *units)
            {
                py::list result;
                for (auto &u : *units)
                {
                    result.append(py::make_tuple(u.id, u.unitType, u.x, u.y, u.health, u.shields, u.attackCooldownRemaining, u.target));
                }
                return result;
            };
            return py::make_tuple(describe(sim->getState().first), describe(sim->getState().second));
        }

    private:
        int mapWidth;
        int mapHeight;
        std::vector<unsigned char> collisionPlayer1;
        std::vector<unsigned char> collisionPlayer2;
        std::unique_ptr<FAP::FastAPproximation<>> sim;
        std::shared_ptr<ChokeGeometry> choke;
    };
}

void init_fap_module(py::module_ &m)
{
    m.doc() = "FAP combat simulation, as modified and driven by Stardust";

    py::module_::import("bwapi");

    m.def("set_unit_scores", [](std::vector<int> base, std::vector<int> scaled)
    {
        if (base.size() != scaled.size()) throw py::value_error("score tables must have the same length");
        baseScore = std::move(base);
        scaledScore = std::move(scaled);
    }, py::arg("base"), py::arg("scaled"), "Per-unit-type score tables (indexed by UnitType id) used to value units");

    py::class_<ChokeGeometry, std::shared_ptr<ChokeGeometry>>(m, "ChokeGeometry")
            .def(py::init([](std::vector<signed char> tileSide,
                             BWAPI::Position end1Center,
                             BWAPI::Position end2Center,
                             BWAPI::Position end1Exit,
                             BWAPI::Position end2Exit)
                          {
                              return std::make_shared<ChokeGeometry>(
                                      ChokeGeometry{std::move(tileSide), end1Center, end2Center, end1Exit, end2Exit});
                          }),
                 py::arg("tile_side"), py::arg("end1_center"), py::arg("end2_center"), py::arg("end1_exit"), py::arg("end2_exit"));

    py::class_<CombatSimulator>(m, "CombatSimulator")
            .def(py::init<int, int>(), py::arg("map_width"), py::arg("map_height"))
            .def("reset", &CombatSimulator::reset, py::arg("choke") = nullptr,
                 "Clear all units; simulate through the given choke if not None")
            .def("add_unit", [](CombatSimulator &sim, bool player1, BWAPI::UnitType unitType, BWAPI::Position position,
                                BWAPI::Position targetPosition, int health, int shields, bool flying, float speed, int armor,
                                int groundCooldown, int groundDamage, int groundMaxRange, int airCooldown, int airDamage,
                                int airMaxRange, int elevation, int attackerCount, int attackCooldownRemaining, bool stimmed,
                                bool undetected, int id, int target, int collisionValue, int collisionValueChoke)
                 {
                     if (baseScore.empty()) throw std::runtime_error("call fap.set_unit_scores before simulating");
                     sim.addUnit(player1, SimUnit{unitType, position, targetPosition, health, shields, flying, speed, armor,
                                                  groundCooldown, groundDamage, groundMaxRange, airCooldown, airDamage,
                                                  airMaxRange, elevation, attackerCount, attackCooldownRemaining, stimmed,
                                                  undetected, id, target, collisionValue, collisionValueChoke});
                 },
                 py::arg("player1"), py::arg("unit_type"), py::arg("position"), py::arg("target_position"),
                 py::arg("health"), py::arg("shields"), py::arg("flying"), py::arg("speed"), py::arg("armor"),
                 py::arg("ground_cooldown"), py::arg("ground_damage"), py::arg("ground_max_range"),
                 py::arg("air_cooldown"), py::arg("air_damage"), py::arg("air_max_range"), py::arg("elevation"),
                 py::arg("attacker_count"), py::arg("attack_cooldown_remaining"), py::arg("stimmed"),
                 py::arg("undetected"), py::arg("id"), py::arg("target"), py::arg("collision_value"),
                 py::arg("collision_value_choke"))
            .def("run", &CombatSimulator::run, py::arg("max_iterations"), py::arg("time_limit_us"),
                 "Simulate; returns (initial p1 score, initial p2 score, final p1 score, final p2 score, iterations)")
            .def("state", &CombatSimulator::state,
                 "(p1 units, p2 units), each a list of (id, unit_type, x, y, health, shields, cooldown, target)");
}
