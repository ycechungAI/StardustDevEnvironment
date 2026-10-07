// Python bindings for BWEM (map analysis), covering the API Stardust uses. Method names follow BWEM's C++ API.
//
// All BWEM objects are owned by the BWEM map singleton and are only borrowed by Python. They compare and hash
// by identity, so they can be used as dict keys and set members.

#include "BWAPIBindings.h"
#include "PythonModules.h"

#include <bwem.h>

namespace
{
    using namespace BWEM;

    template<class T>
    py::list referenceList(const std::vector<T> &items)
    {
        py::list result;
        for (const auto &item : items) result.append(py::cast(&item, py::return_value_policy::reference));
        return result;
    }

    template<class T>
    py::list pointerList(const std::vector<T *> &items)
    {
        py::list result;
        for (auto *item : items) result.append(py::cast(item, py::return_value_policy::reference));
        return result;
    }

    template<class T>
    py::list uniquePointerList(const std::vector<std::unique_ptr<T>> &items)
    {
        py::list result;
        for (const auto &item : items) result.append(py::cast(item.get(), py::return_value_policy::reference));
        return result;
    }

    Map &theMap() { return Map::Instance(); }
}

void init_bwem_module(py::module_ &m)
{
    m.doc() = "BWEM map analysis (areas, chokepoints, bases). Call ResetInstance() then Instance().Initialize() at game start.";

    py::module_::import("bwapi");

    auto area = declare_interface<Area>(m, "Area");
    auto chokePoint = declare_interface<ChokePoint>(m, "ChokePoint");
    auto base = declare_interface<Base>(m, "Base");
    auto neutral = declare_interface<Neutral>(m, "Neutral");
    BWAPIInterfaceClass<Ressource> ressource(m, "Ressource", neutral);
    BWAPIInterfaceClass<Mineral> mineral(m, "Mineral", ressource);
    BWAPIInterfaceClass<Geyser> geyser(m, "Geyser", ressource);
    BWAPIInterfaceClass<StaticBuilding> staticBuilding(m, "StaticBuilding", neutral);
    auto miniTile = declare_interface<MiniTile>(m, "MiniTile");
    auto tile = declare_interface<Tile>(m, "Tile");
    auto map = declare_interface<Map>(m, "Map");

    py::enum_<ChokePoint::node>(chokePoint, "node")
            .value("end1", ChokePoint::end1)
            .value("middle", ChokePoint::middle)
            .value("end2", ChokePoint::end2)
            .export_values();

    area.def("Id", &Area::Id)
            .def("GroupId", &Area::GroupId)
            .def("TopLeft", &Area::TopLeft)
            .def("BottomRight", &Area::BottomRight)
            .def("BoundingBoxSize", &Area::BoundingBoxSize)
            .def("Top", &Area::Top)
            .def("MaxAltitude", &Area::MaxAltitude)
            .def("MiniTiles", &Area::MiniTiles)
            .def("LowGroundPercentage", &Area::LowGroundPercentage)
            .def("HighGroundPercentage", &Area::HighGroundPercentage)
            .def("VeryHighGroundPercentage", &Area::VeryHighGroundPercentage)
            .def("ChokePoints", [](const Area &a) { return pointerList(a.ChokePoints()); })
            .def("ChokePoints", [](const Area &a, const Area *other) { return referenceList(a.ChokePoints(other)); },
                 py::arg("other"))
            .def("ChokePointsByArea", [](const Area &a)
            {
                py::dict result;
                for (const auto &[other, chokes] : a.ChokePointsByArea())
                {
                    result[py::cast(other, py::return_value_policy::reference)] = referenceList(*chokes);
                }
                return result;
            })
            .def("AccessibleNeighbours", [](const Area &a) { return pointerList(a.AccessibleNeighbours()); })
            .def("AccessibleFrom", &Area::AccessibleFrom, py::arg("other"))
            .def("Minerals", [](const Area &a) { return pointerList(a.Minerals()); })
            .def("Geysers", [](const Area &a) { return pointerList(a.Geysers()); })
            .def("Bases", [](const Area &a) { return referenceList(a.Bases()); })
            .def("__repr__", [](const Area &a) { return "<bwem.Area #" + std::to_string(a.Id()) + ">"; });

    chokePoint.def("GetAreas", [](const ChokePoint &cp)
            {
                return py::make_tuple(py::cast(cp.GetAreas().first, py::return_value_policy::reference),
                                      py::cast(cp.GetAreas().second, py::return_value_policy::reference));
            })
            .def("Center", &ChokePoint::Center)
            .def("Pos", &ChokePoint::Pos, py::arg("n"))
            .def("PosInArea", &ChokePoint::PosInArea, py::arg("n"), py::arg("area"))
            .def("Geometry", [](const ChokePoint &cp)
            {
                return std::vector<BWAPI::WalkPosition>(cp.Geometry().begin(), cp.Geometry().end());
            })
            .def("Blocked", &ChokePoint::Blocked)
            .def("BlockingNeutral", &ChokePoint::BlockingNeutral, py::return_value_policy::reference)
            .def("DistanceFrom", &ChokePoint::DistanceFrom, py::arg("other"))
            .def("AccessibleFrom", &ChokePoint::AccessibleFrom, py::arg("other"))
            .def("Index", &ChokePoint::Index)
            .def("__repr__", [](const ChokePoint &cp)
            {
                std::ostringstream os;
                os << "<bwem.ChokePoint #" << cp.Index() << " @" << cp.Center() << ">";
                return os.str();
            });

    base.def("GetArea", &Base::GetArea, py::return_value_policy::reference)
            .def("Location", &Base::Location)
            .def("Center", &Base::Center)
            .def("Minerals", [](const Base &b) { return pointerList(b.Minerals()); })
            .def("Geysers", [](const Base &b) { return pointerList(b.Geysers()); })
            .def("BlockingMinerals", [](const Base &b) { return pointerList(b.BlockingMinerals()); })
            .def("Starting", &Base::Starting)
            .def("__repr__", [](const Base &b)
            {
                std::ostringstream os;
                os << "<bwem.Base @" << b.Location() << ">";
                return os.str();
            });

    neutral.def("Unit", &Neutral::Unit, py::return_value_policy::reference)
            .def("Type", &Neutral::Type)
            .def("Pos", &Neutral::Pos)
            .def("TopLeft", &Neutral::TopLeft)
            .def("BottomRight", &Neutral::BottomRight)
            .def("Size", &Neutral::Size)
            .def("BlockedAreas", [](const Neutral &n) { return pointerList(n.BlockedAreas()); })
            .def("NextStacked", &Neutral::NextStacked, py::return_value_policy::reference);
    ressource.def("InitialAmount", &Ressource::InitialAmount)
            .def("Amount", &Ressource::Amount);

    miniTile.def("Walkable", &MiniTile::Walkable)
            .def("Altitude", &MiniTile::Altitude)
            .def("Sea", &MiniTile::Sea)
            .def("Lake", &MiniTile::Lake)
            .def("Terrain", &MiniTile::Terrain)
            .def("AreaId", &MiniTile::AreaId)
            .def("Blocked", &MiniTile::Blocked);

    tile.def("Buildable", &Tile::Buildable)
            .def("GroundHeight", &Tile::GroundHeight)
            .def("Doodad", &Tile::Doodad)
            .def("AreaId", &Tile::AreaId)
            .def("MinAltitude", &Tile::MinAltitude)
            .def("GetNeutral", &Tile::GetNeutral, py::return_value_policy::reference);

    map.def("Initialize", [](Map &bwemMap)
            {
                if (!BWAPI::BroodwarPtr) throw std::runtime_error("BWEM can only be initialized during a game");
                bwemMap.Initialize(BWAPI::BroodwarPtr);
            })
            .def("EnableAutomaticPathAnalysis", &Map::EnableAutomaticPathAnalysis)
            .def("AutomaticPathUpdate", &Map::AutomaticPathUpdate)
            .def("FindBasesForStartingLocations", &Map::FindBasesForStartingLocations)
            .def("Size", &Map::Size)
            .def("WalkSize", &Map::WalkSize)
            .def("Center", &Map::Center)
            .def("MaxAltitude", &Map::MaxAltitude)
            .def("BaseCount", &Map::BaseCount)
            .def("ChokePointCount", &Map::ChokePointCount)
            .def("Valid", [](const Map &bwemMap, BWAPI::TilePosition p) { return bwemMap.Valid(p); }, py::arg("p"))
            .def("Valid", [](const Map &bwemMap, BWAPI::WalkPosition p) { return bwemMap.Valid(p); }, py::arg("p"))
            .def("Valid", [](const Map &bwemMap, BWAPI::Position p) { return bwemMap.Valid(p); }, py::arg("p"))
            .def("GetTile", [](const Map &bwemMap, BWAPI::TilePosition p) -> const Tile &
            {
                if (!bwemMap.Valid(p)) throw py::index_error("invalid tile position");
                return bwemMap.GetTile(p, utils::check_t::no_check);
            }, py::arg("p"), py::return_value_policy::reference)
            .def("GetMiniTile", [](const Map &bwemMap, BWAPI::WalkPosition p) -> const MiniTile &
            {
                if (!bwemMap.Valid(p)) throw py::index_error("invalid walk position");
                return bwemMap.GetMiniTile(p, utils::check_t::no_check);
            }, py::arg("p"), py::return_value_policy::reference)
            .def("StartingLocations", &Map::StartingLocations, py::return_value_policy::copy)
            .def("Minerals", [](const Map &bwemMap) { return uniquePointerList(bwemMap.Minerals()); })
            .def("Geysers", [](const Map &bwemMap) { return uniquePointerList(bwemMap.Geysers()); })
            .def("StaticBuildings", [](const Map &bwemMap) { return uniquePointerList(bwemMap.StaticBuildings()); })
            .def("GetMineral", &Map::GetMineral, py::arg("unit"), py::return_value_policy::reference)
            .def("GetGeyser", &Map::GetGeyser, py::arg("unit"), py::return_value_policy::reference)
            .def("OnMineralDestroyed", &Map::OnMineralDestroyed, py::arg("unit"))
            .def("OnStaticBuildingDestroyed", &Map::OnStaticBuildingDestroyed, py::arg("unit"))
            .def("Areas", [](const Map &bwemMap) { return referenceList(bwemMap.Areas()); })
            .def("GetArea", [](const Map &bwemMap, Area::id id) { return bwemMap.GetArea(id); },
                 py::arg("id"), py::return_value_policy::reference)
            // Out-of-map positions give None rather than reading out of bounds
            .def("GetArea", [](const Map &bwemMap, BWAPI::WalkPosition w) -> const Area *
            {
                return bwemMap.Valid(w) ? bwemMap.GetArea(w) : nullptr;
            }, py::arg("w"), py::return_value_policy::reference)
            .def("GetArea", [](const Map &bwemMap, BWAPI::TilePosition t) -> const Area *
            {
                return bwemMap.Valid(t) ? bwemMap.GetArea(t) : nullptr;
            }, py::arg("t"), py::return_value_policy::reference)
            .def("GetNearestArea", [](const Map &bwemMap, BWAPI::WalkPosition w) -> const Area *
            {
                return bwemMap.Valid(w) ? bwemMap.GetNearestArea(w) : nullptr;
            }, py::arg("w"), py::return_value_policy::reference)
            .def("GetNearestArea", [](const Map &bwemMap, BWAPI::TilePosition t) -> const Area *
            {
                return bwemMap.Valid(t) ? bwemMap.GetNearestArea(t) : nullptr;
            }, py::arg("t"), py::return_value_policy::reference)
            .def("GetPath", [](const Map &bwemMap, BWAPI::Position a, BWAPI::Position b)
            {
                int length = -1;
                const auto &path = bwemMap.GetPath(a, b, &length);
                return py::make_tuple(pointerList(path), length);
            }, py::arg("a"), py::arg("b"),
                 "Chokepoints on the shortest ground path from a to b, and its length in pixels (-1 if unreachable)");

    m.def("Instance", &theMap, py::return_value_policy::reference);
    m.def("ResetInstance", &Map::ResetInstance);
}
