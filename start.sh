#!/usr/bin/env bash
# start.sh — interactive menu for running Stardust (StarCraft: Brood War bot dev environment).
# Gives numbered options for the common tools so you don't have to remember the flags.
set -u
cd "$(dirname "$0")"

PY=".venv/bin/python"

OPPONENTS=(
    Stone BunkerBoxer BananaBrain Stardust2025 ClaudeOpus55
    Steamhammer2025 Microwave McRaveZ CreativeZerg CreativeTerran
    WorkerRush PylonPuller ZZZKBot SparkTerran SparkZerg
    UAlbertaBotTerran UAlbertaBotZerg UAlbertaBotProtoss
    Locutus Iron Dragon SAIDA WillyT Steamhammer
)
# watch.py can also field the Python port itself
WATCH_BOTS=(StardustPy "${OPPONENTS[@]}")

# ask_int "prompt" min max default -> prints the validated number
ask_int() {
    local prompt="$1" min="$2" max="$3" default="$4" ans
    while true; do
        read -rp "$prompt [$default]: " ans
        ans="${ans:-$default}"
        if [[ "$ans" =~ ^[0-9]+$ ]] && [ "$ans" -ge "$min" ] && [ "$ans" -le "$max" ]; then
            echo "$ans"
            return
        fi
        echo "enter a number between $min and $max" >&2
    done
}

# ask_choice "prompt" "opt1|opt2" default -> prints the validated choice (lowercased)
ask_choice() {
    local prompt="$1" opts="$2" default="$3" ans
    while true; do
        read -rp "$prompt [$default]: " ans
        ans="${ans:-$default}"
        ans="$(echo "$ans" | tr '[:upper:]' '[:lower:]')"
        if [[ "|$opts|" == *"|$ans|"* ]]; then
            echo "$ans"
            return
        fi
        echo "choose one of: ${opts//|/, }" >&2
    done
}

# pick_bot "prompt" array_name... -> prints the chosen bot name
# array elements may be "" to mark an already-picked (disabled) entry
bot_race() {
    case "$1" in
        Stone|BunkerBoxer|CreativeTerran|WorkerRush|SparkTerran|UAlbertaBotTerran|SAIDA|Dragon|WillyT|Iron)
            echo "Terran" ;;
        Steamhammer|Steamhammer2025|Microwave|McRaveZ|CreativeZerg|ZZZKBot|SparkZerg|UAlbertaBotZerg)
            echo "Zerg" ;;
        StardustPy) echo "Python port" ;;
        *) echo "Protoss" ;;
    esac
}

pick_bot() {
    local prompt="$1" arr_name="$2"
    local -n arr="$arr_name"
    local i ans
    while true; do
        # menu goes to stderr: stdout is captured as the return value
        echo "$prompt" >&2
        for i in "${!arr[@]}"; do
            if [ -n "${arr[$i]}" ]; then
                printf "  %2d) %-22s %s\n" "$((i + 1))" "${arr[$i]}" "$(bot_race "${arr[$i]}")" >&2
            fi
        done
        read -rp "Which AI do you want to play? Enter its number: " ans
        if [[ "$ans" =~ ^[0-9]+$ ]] && [ "$ans" -ge 1 ] && [ "$ans" -le "${#arr[@]}" ] \
            && [ -n "${arr[$((ans - 1))]}" ]; then
            echo "${arr[$((ans - 1))]}"
            return
        fi
        echo "invalid choice, try again" >&2
    done
}

run_cmd() {
    echo "+ $*"
    "$@"
}

option1_4pool() {
    run_cmd "$PY" tools/run_games.py Steamhammer.4PoolHard
}

option2_custom_games() {
    local bots=( "${WATCH_BOTS[@]}" )
    local ai1 ai2 games ui wins j
    ai1="$(pick_bot "Which AI do you want to play as?" bots)"
    for j in "${!bots[@]}"; do
        [ "${bots[$j]}" = "$ai1" ] && bots[$j]=""
    done
    ai2="$(pick_bot "Which AI should it play against?" bots)"
    games="$(ask_int "How many games" 1 100 12)"
    ui="$(ask_choice "Show the game windows? (Y/N)" "y|n" "n")"
    local cmd=("$PY" tools/run_games.py)
    [ "$ai1" != "StardustPy" ] && cmd+=(--bot "$ai1")
    cmd+=(--opponent "$ai2" --games "$games")
    if [ "$ui" = "y" ]; then
        wins="$(ask_int "How many windows? (1, 2, 4 or 6)" 1 6 4)"
        cmd+=(--ui "$wins")
    fi
    run_cmd "${cmd[@]}"
}

option3_build() {
    local cores threads
    cores="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    echo "detected $cores cpu cores"
    threads="$(ask_int "Build threads? (1-8)" 1 8 4)"
    run_cmd cmake --build build-ui -j "$threads"
}

option4_bench() {
    run_cmd "$PY" tools/bench_ui.py
}

option5_watch() {
    local bots=( "${WATCH_BOTS[@]}" )
    local n i name speed iterations chosen=()
    n="$(ask_int "How many AIs? (2-4)" 2 4 4)"
    for ((i = 1; i <= n; i++)); do
        name="$(pick_bot "Choose AI #$i:" bots)"
        chosen+=("$name")
        # disable it so it can't be picked again
        for j in "${!bots[@]}"; do
            [ "${bots[$j]}" = "$name" ] && bots[$j]=""
        done
    done
    # round robin: 2 AIs -> 1 game, 3 -> 3 games, 4 -> 6 games, each in its own window automatically
    local pair_games=$((n * (n - 1) / 2))
    echo "round robin: $pair_games game(s), one window each"
    iterations="$(ask_int "How many iterations?" 1 100 1)"
    speed="$(ask_int "Game speed? (0 = unlimited, 1 slowest - 6 fastest)" 0 6 2)"
    for ((i = 1; i <= iterations; i++)); do
        [ "$iterations" -gt 1 ] && echo "--- iteration $i of $iterations ---"
        run_cmd "$PY" tools/watch.py "${chosen[@]}" --speed "$speed"
    done
}

option6_leaderboard() {
    # Menu# matches the numbered AI list in option 2 (1=StardustPy, 2=Stone, ...)
    local numbers
    numbers="$(for i in "${!WATCH_BOTS[@]}"; do printf "%s:%d," "${WATCH_BOTS[$i]}" "$((i + 1))"; done)"
    "$PY" tools/elo.py | awk -v map="$numbers" '
        BEGIN { n = split(map, pairs, ","); for (k = 1; k <= n; k++) { split(pairs[k], kv, ":"); if (kv[1] != "") num[kv[1]] = kv[2] } }
        /^ *#  Player/ { sub(/^ *#/, " # Menu#"); print; next }
        $1 ~ /^[0-9]+$/ && NF >= 7 {
            name = $2
            menu = (name in num) ? num[name] : "--"
            sub(/^[ ]*[0-9]+/, "&" sprintf("%6s", menu))
            print
            next
        }
        { print }
    '
}

option7_tests() {
    run_cmd uv run pytest
}

while true; do
    cat <<'MENU'

  Stardust — pick an option:
    1) run games vs Steamhammer 4-pool hard
    2) run games vs an AI you choose
    3) build / compile (build-ui)
    4) bench UI
    5) watch multi-AI round robin
    6) leaderboard (elo)
    7) other tests (pytest)
    0) quit
MENU
    read -rp "> " choice
    case "$choice" in
        1) option1_4pool ;;
        2) option2_custom_games ;;
        3) option3_build ;;
        4) option4_bench ;;
        5) option5_watch ;;
        6) option6_leaderboard ;;
        7) option7_tests ;;
        0|q|quit|exit) exit 0 ;;
        *) echo "unknown option: $choice" ;;
    esac
    echo
    read -rp "press enter to continue..." _
done
