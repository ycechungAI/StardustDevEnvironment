#include "BWAPIBindings.h"
#include "PythonModules.h"

#include "CherryVis.h"
#include "Log.h"

void init_instrumentation_module(py::module_ &m)
{
    m.doc() = "Stardust instrumentation: file logging and CherryVis replay annotations";

    // Unit arguments need the bwapi types registered
    py::module_::import("bwapi");

    // Outside a game (e.g. offline tests) there is no frame counter or output directory, so these
    // functions print or do nothing rather than touching BWAPI.
    auto inGame = []() { return BWAPI::BroodwarPtr != nullptr; };

    auto log = m.def_submodule("Log", "Game log written to bwapi-data/write/");
    log.def("initialize", &Log::initialize);
    log.def("setOutputToConsole", &Log::SetOutputToConsole, py::arg("outputToConsole"));
    log.def("write", [inGame](const std::string &message)
    {
        if (inGame()) Log::Get() << message;
        else py::print(message);
    }, py::arg("message"));
    log.def("logFileName", []() { return Log::LogFileName(); });

    auto cvis = m.def_submodule("CherryVis", "CherryVis replay annotations written to bwapi-data/write/cvis/");
    cvis.def("initialize", [inGame]() { if (inGame()) CherryVis::initialize(); });
    cvis.def("setBoardValue", [inGame](const std::string &key, const std::string &value)
    {
        if (inGame()) CherryVis::setBoardValue(key, value);
    }, py::arg("key"), py::arg("value"));
    cvis.def("setBoardListValue", [inGame](const std::string &key, std::vector<std::string> values)
    {
        if (inGame()) CherryVis::setBoardListValue(key, values);
    }, py::arg("key"), py::arg("values"));
    cvis.def("unitFirstSeen", [inGame](BWAPI::Unit unit)
    {
        if (inGame()) CherryVis::unitFirstSeen(unit);
    }, py::arg("unit"));
    cvis.def("log", [inGame](const std::string &message, int unitId)
    {
        if (inGame()) CherryVis::log(unitId) << message;
    }, py::arg("message"), py::arg("unitId") = -1);
    cvis.def("log", [inGame](const std::string &message, BWAPI::Unit unit)
    {
        if (inGame()) CherryVis::log(unit) << message;
    }, py::arg("message"), py::arg("unit"));
    cvis.def("addHeatmap", [inGame](const std::string &key, const std::vector<long> &data, int sizeX, int sizeY)
    {
        if (static_cast<int>(data.size()) != sizeX * sizeY)
        {
            throw py::value_error("heatmap data has " + std::to_string(data.size()) + " values; expected sizeX * sizeY = "
                                  + std::to_string(sizeX * sizeY));
        }
        if (inGame()) CherryVis::addHeatmap(key, data, sizeX, sizeY);
    }, py::arg("key"), py::arg("data"), py::arg("sizeX"), py::arg("sizeY"));
    cvis.def("frameEnd", [inGame](int frame) { if (inGame()) CherryVis::frameEnd(frame); }, py::arg("frame"));
    cvis.def("gameEnd", [inGame]() { if (inGame()) CherryVis::gameEnd(); });
}
