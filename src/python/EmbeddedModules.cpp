// Registers the binding modules as built-in modules of the interpreter embedded in the bot.
// Must be compiled directly into the bot library (not a static library) so the registrations are kept.

#include <pybind11/embed.h>

#include "PythonModules.h"

PYBIND11_EMBEDDED_MODULE(bwapi, m)
{
    init_bwapi_module(m);
}

PYBIND11_EMBEDDED_MODULE(instrumentation, m)
{
    init_instrumentation_module(m);
}

PYBIND11_EMBEDDED_MODULE(bwem, m)
{
    init_bwem_module(m);
}

PYBIND11_EMBEDDED_MODULE(fap, m)
{
    init_fap_module(m);
}
