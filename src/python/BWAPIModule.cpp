#include "BWAPIBindings.h"
#include "PythonModules.h"

#include <pybind11/numpy.h>

void bind_bwapi_generated(py::module_ &m);

namespace
{
    template<class T>
    BWAPIInterfaceClass<T> existing(py::module_ &m, const char *name)
    {
        return py::reinterpret_borrow<BWAPIInterfaceClass<T>>(m.attr(name));
    }

    // A whole-map grid of a per-tile (or per-walk-tile) query as a numpy bool array indexed [x, y], so Python can read
    // the map in one call instead of one call per tile
    template<class F>
    py::array_t<bool> grid(int width, int height, F query)
    {
        py::array_t<bool> result({width, height});
        auto out = result.mutable_unchecked<2>();
        for (int x = 0; x < width; x++)
        {
            for (int y = 0; y < height; y++)
            {
                out(x, y) = query(x, y);
            }
        }
        return result;
    }

    template<class T>
    void def_id_repr(py::module_ &m, const char *name)
    {
        std::string typeName = name;
        existing<T>(m, name).def("__repr__", [typeName](const T &obj)
        {
            return "<" + typeName + " #" + std::to_string(obj.getID()) + ">";
        });
    }
}

void init_bwapi_module(py::module_ &m)
{
    m.doc() = "BWAPI bindings (OpenBW BWAPI 4.x). Names follow the C++ API; see https://bwapi.github.io/";

    bind_bwapi_generated(m);

    // Interface<Game>::registerEvent is a template on the base class, so it is bound by hand.
    existing<BWAPI::Game>(m, "Game").def(
            "registerEvent",
            [](BWAPI::Game &game,
               const std::function<void(BWAPI::Game *)> &action,
               const std::function<bool(BWAPI::Game *)> &condition,
               int timesToRun,
               int framesToCheck)
            {
                game.registerEvent(action, condition, timesToRun, framesToCheck);
            },
            py::arg("action"),
            py::arg("condition") = py::none(),
            py::arg("timesToRun") = -1,
            py::arg("framesToCheck") = 0);

    // Not in BWAPI: whole-map grids, for numpy
    existing<BWAPI::Game>(m, "Game")
            .def("getVisibilityGrid", [](BWAPI::Game &game)
            {
                return grid(game.mapWidth(), game.mapHeight(), [&](int x, int y) { return game.isVisible(x, y); });
            }, "isVisible for every tile, as a numpy bool array indexed [x, y]")
            .def("getCreepGrid", [](BWAPI::Game &game)
            {
                return grid(game.mapWidth(), game.mapHeight(), [&](int x, int y) { return game.hasCreep(x, y); });
            }, "hasCreep for every tile, as a numpy bool array indexed [x, y]")
            .def("getWalkabilityGrid", [](BWAPI::Game &game)
            {
                return grid(game.mapWidth() * 4, game.mapHeight() * 4,
                            [&](int x, int y) { return game.isWalkable(x, y); });
            }, "isWalkable for every walk tile, as a numpy bool array indexed [x, y]");

    existing<BWAPI::UnitInterface>(m, "Unit").def("__repr__", [](const BWAPI::UnitInterface &unit)
    {
        std::ostringstream os;
        os << "<Unit #" << unit.getID() << " " << unit.getType() << " @" << unit.getPosition() << ">";
        return os.str();
    });
    existing<BWAPI::PlayerInterface>(m, "Player").def("__repr__", [](const BWAPI::PlayerInterface &player)
    {
        return "<Player #" + std::to_string(player.getID()) + " " + player.getName() + ">";
    });
    def_id_repr<BWAPI::ForceInterface>(m, "Force");
    def_id_repr<BWAPI::BulletInterface>(m, "Bullet");
    def_id_repr<BWAPI::RegionInterface>(m, "Region");

    publish_broodwar(m);
}

void publish_broodwar(py::module_ &bwapiModule)
{
    bwapiModule.attr("Broodwar") = BWAPI::BroodwarPtr
                                   ? py::cast(BWAPI::BroodwarPtr, py::return_value_policy::reference)
                                   : py::none();
}
