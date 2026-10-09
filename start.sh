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
        echo "enter a number between $min and $max"
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
        echo "choose one of: ${opts//|/, }"
    done
}

# pick_bot "prompt" array_name... -> prints the chosen bot name
# array elements may be "" to mark an already-picked (disabled) entry
pick_bot() {
    local prompt="$1" arr_name="$2"
    local -n arr="$arr_name"
    local i ans
    while true; do
        echo "$prompt"
        for i in "${!arr[@]}"; do
            if [ -n "${arr[$i]}" ]; then
                printf "  %2d) %s\n" "$((i + 1))" "${arr[$i]}"
            fi
        done
        printf "   0) other (type a name)\n"
        read -rp "number: " ans
        if [[ "$ans" =~ ^[0-9]+$ ]]; then
            if [ "$ans" -eq 0 ]; then
                read -rp "bot name: " ans
                [ -n "$ans" ] && { echo "$ans"; return; }
            elif [ "$ans" -ge 1 ] && [ "$ans" -le "${#arr[@]}" ] && [ -n "${arr[$((ans - 1))]}" ]; then
                echo "${arr[$((ans - 1))]}"
                return
            fi
        fi
        echo "invalid choice, try again"
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
    local opponents=("${OPPONENTS[@]}")
    local opponent games ui wins
    opponent="$(pick_bot "opponent?" opponents)"
    games="$(ask_int "how many games" 1 100 12)"
    ui="$(ask_choice "show UI? (Y/N)" "y|n" "n")"
    if [ "$ui" = "y" ]; then
        wins="$(ask_int "windows (1, 2, 4 or 6)" 1 6 4)"
        run_cmd "$PY" tools/run_games.py --opponent "$opponent" --games "$games" --ui "$wins"
    else
        run_cmd "$PY" tools/run_games.py --opponent "$opponent" --games "$games"
    fi
}

option3_build() {
    local cores threads
    cores="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
    echo "detected $cores cpu cores"
    threads="$(ask_int "how many build threads (1-8)" 1 8 4)"
    run_cmd cmake --build build-ui -j "$threads"
}

option4_bench() {
    run_cmd "$PY" tools/bench_ui.py
}

option5_watch() {
    local bots=( "${WATCH_BOTS[@]}" )
    local n i name speed chosen=()
    n="$(ask_int "how many AIs (2-4)" 2 4 4)"
    for ((i = 1; i <= n; i++)); do
        name="$(pick_bot "AI #$i?" bots)"
        chosen+=("$name")
        # disable it so it can't be picked again
        for j in "${!bots[@]}"; do
            [ "${bots[$j]}" = "$name" ] && bots[$j]=""
        done
    done
    speed="$(ask_int "game speed (0 = unlimited, 1 slowest - 6 fastest)" 0 6 2)"
    run_cmd "$PY" tools/watch.py "${chosen[@]}" --speed "$speed"
}

option6_leaderboard() {
    run_cmd "$PY" tools/elo.py
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
