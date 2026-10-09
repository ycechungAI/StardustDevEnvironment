#!/usr/bin/env bash
# Sets up and builds the project: Python dependencies, the opponent bots that are only recipes, then the test harness.
#
# Usage: ./build.sh            (JOBS=8 ./build.sh to build with 8 jobs instead of 4)
#
# Safe to re-run: bots already in bots/ are left alone, and CMake only rebuilds what changed.
set -euo pipefail

cd "$(dirname "$0")"
JOBS="${JOBS:-4}"

step() { printf '\n==> %s\n' "$*"; }
need() { command -v "$1" >/dev/null 2>&1 || { echo "Missing $1: $2" >&2; exit 1; }; }

step "Checking tools"
need git "install git"
need cmake "install CMake 3.25+ (macOS: brew install cmake; Linux: your package manager)"
need c++ "install a C++17 compiler (macOS: xcode-select --install; Linux: g++ or clang)"
if ! command -v uv >/dev/null 2>&1; then
    echo "uv not found; installing it from astral.sh"
    curl -LsSf https://astral.sh/uv/install.sh | sh
    export PATH="$HOME/.local/bin:$PATH"
fi

step "Installing Python dependencies (uv sync)"
uv sync

step "Downloading opponent bots"
missing=()
for recipe in bots/recipes/*/; do
    name="$(basename "$recipe")"
    [ -f "$recipe/recipe.json" ] && [ ! -d "bots/$name" ] && missing+=("$name")
done
if [ ${#missing[@]} -gt 0 ]; then
    .venv/bin/python tools/fetch_bot.py "${missing[@]}"
else
    echo "All recipe bots are already in bots/"
fi

step "Configuring (cmake -S . -B build -DCMAKE_BUILD_TYPE=Release)"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

step "Building the tests target with $JOBS jobs"
cmake --build build -j "$JOBS" --target tests

step "Done"
for mpq in STARDAT.MPQ BROODAT.MPQ Patch_rt.mpq; do
    [ -f "build/test/$mpq" ] || echo "Note: copy $mpq from StarCraft 1.16.1 into build/test/ before playing games."
done
