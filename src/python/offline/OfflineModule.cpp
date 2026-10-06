// A binding module built as a regular extension, so it can be imported from plain Python (pytest, notebooks)
// without running a game. CMake compiles this once per module, defining OFFLINE_MODULE_NAME (e.g. bwapi) and
// OFFLINE_MODULE_INIT (e.g. init_bwapi_module). bwapi.Broodwar is None in this mode.

#include "PythonModules.h"

PYBIND11_MODULE(OFFLINE_MODULE_NAME, m)
{
    OFFLINE_MODULE_INIT(m);
}
