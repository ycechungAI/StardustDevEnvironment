#pragma once

// Entry points shared by the embedded modules (inside the bot) and the offline extension modules.

#include <pybind11/pybind11.h>

// Populates the `bwapi` module.
void init_bwapi_module(pybind11::module_ &m);

// Populates the `instrumentation` module (Log + CherryVis).
void init_instrumentation_module(pybind11::module_ &m);

// Populates the `bwem` module (BWEM map analysis).
void init_bwem_module(pybind11::module_ &m);

// Populates the `fap` module (FAP combat simulation).
void init_fap_module(pybind11::module_ &m);

// Points bwapi.Broodwar at the current game (or None when there is no game).
void publish_broodwar(pybind11::module_ &bwapiModule);
