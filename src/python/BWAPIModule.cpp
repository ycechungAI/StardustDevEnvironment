#include "BWAPIBindings.h"
#include "PythonModules.h"

void bind_bwapi_generated(py::module_ &m);

namespace
{
    template<class T>
    BWAPIInterfaceClass<T> existing(py::module_ &m, const char *name)
    {
        return py::reinterpret_borrow<BWAPIInterfaceClass<T>>(m.attr(name));
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
