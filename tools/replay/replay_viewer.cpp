// Plays a replay in a window with OpenBW's UI, for tools/replays.py. The playback loop follows OpenBW's own replay viewer
// (openbw/ui/gfxtest.cpp): states are saved every 10 seconds of game time so the replay can be wound back.
//
//   replay_viewer <replay.rep> [--data <directory with the MPQs>] [--frame <frame to start at>] [--player <0-7>]
//
// Keys: space or p pause; a and z faster and slower; left and right arrows (or , and .) 10 seconds back or forward;
// [ and ] a minute back or forward; the bar at the bottom seeks too. Close the window to finish.

#include "ui/ui.h"
#include "ui/common.h"
#include "bwgame.h"
#include "actions.h"
#include "replay.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>

namespace bwgame::ui
{
    void log_str(a_string str)
    {
        fwrite(str.data(), str.size(), 1, stderr);
    }

    void fatal_error_str(a_string str)
    {
        fprintf(stderr, "fatal error: %s\n", str.c_str());
        std::exit(1);
    }
}

using namespace bwgame;

namespace
{
    struct saved_state
    {
        state st;
        action_state action_st;
        std::array<apm_t, 12> apm;
    };

    struct viewer
    {
        ui_functions ui;
        a_map<int, std::unique_ptr<saved_state>> saved_states;
        std::chrono::high_resolution_clock clock;
        std::chrono::high_resolution_clock::time_point last_tick;

        explicit viewer(game_player player) : ui(std::move(player)) {}

        void next_frame()
        {
            int save_interval = 10 * 1000 / 42;
            if (ui.st.current_frame % save_interval == 0 && !saved_states.count(ui.st.current_frame))
            {
                auto v = std::make_unique<saved_state>();
                v->st = copy_state(ui.st);
                v->action_st = copy_state(ui.action_st, ui.st, v->st);
                v->apm = ui.apm;
                saved_states[ui.st.current_frame] = std::move(v);
            }
            ui.replay_functions::next_frame();
            for (auto &v : ui.apm) v.update(ui.st.current_frame);
        }

        void seek(int frames)
        {
            int target = ui.replay_frame + frames;
            ui.replay_frame = std::max(0, std::min(target, ui.replay_st.end_frame));
        }

        void update()
        {
            auto now = clock.now();
            auto tick_speed = std::chrono::milliseconds((fp8::integer(42) / ui.game_speed).integer_part());

            if (!ui.is_done() || ui.st.current_frame != ui.replay_frame)
            {
                if (ui.st.current_frame != ui.replay_frame)
                {
                    // Seeking: restore the last saved state before the target, then play forward to it
                    auto i = saved_states.lower_bound(ui.replay_frame);
                    if (i != saved_states.begin()) --i;
                    if (i != saved_states.end())
                    {
                        auto &v = i->second;
                        if (ui.st.current_frame > ui.replay_frame || v->st.current_frame > ui.st.current_frame)
                        {
                            ui.st = copy_state(v->st);
                            ui.action_st = copy_state(v->action_st, v->st, ui.st);
                            ui.apm = v->apm;
                        }
                    }
                    for (size_t n = 0; n != 512 && ui.st.current_frame < ui.replay_frame; ++n)
                    {
                        next_frame();
                        if (n % 16 == 15 && clock.now() - now >= std::chrono::milliseconds(50)) break;
                    }
                    last_tick = now;
                }
                else if (ui.is_paused)
                {
                    last_tick = now;
                }
                else
                {
                    auto tick_t = now - last_tick;
                    if (tick_t >= tick_speed * 16)
                    {
                        last_tick = now - tick_speed * 16;
                        tick_t = tick_speed * 16;
                    }
                    auto tick_n = tick_speed.count() == 0 ? 128 : tick_t / tick_speed;
                    for (auto n = tick_n; n;)
                    {
                        --n;
                        last_tick += tick_speed;
                        if (!ui.is_done()) next_frame();
                        else break;
                        if (n % 4 == 3 && clock.now() - now >= std::chrono::milliseconds(50)) break;
                    }
                    ui.replay_frame = ui.st.current_frame;
                }
            }
            ui.update();
        }
    };
}

int main(int argc, char **argv)
{
    std::string replay;
    std::string data = ".";
    int startFrame = 0;
    int player = -1;
    for (int i = 1; i < argc; i++)
    {
        std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) data = argv[++i];
        else if (arg == "--frame" && i + 1 < argc) startFrame = std::atoi(argv[++i]);
        else if (arg == "--player" && i + 1 < argc) player = std::atoi(argv[++i]);
        else replay = arg;
    }
    if (replay.empty())
    {
        fprintf(stderr, "usage: replay_viewer <replay.rep> [--data <directory>] [--frame <frame>] [--player <0-7>]\n");
        return 2;
    }

    size_t width = 1280;
    size_t height = 800;

    auto load_data_file = data_loading::data_files_directory(data.c_str());
    viewer v{game_player(load_data_file)};
    auto &ui = v.ui;
    ui.exit_on_close = false;
    ui.load_all_image_data(load_data_file);
    ui.load_data_file = [&](a_vector<uint8_t> &out, a_string filename)
    {
        load_data_file(out, std::move(filename));
    };
    ui.init();
    ui.load_replay_file(replay.c_str());

    auto slash = replay.find_last_of('/');
    std::string title = slash == std::string::npos ? replay : replay.substr(slash + 1);
    ui.wnd.create(title.c_str(), 0, 0, (int) width, (int) height);
    ui.resize(width, height);

    // Start looking at the chosen player's base, or the middle of the map
    xy centre = {(int) ui.game_st.map_width / 2, (int) ui.game_st.map_height / 2};
    if (player >= 0 && player < 8 && ui.game_st.start_locations[player] != xy()) centre = ui.game_st.start_locations[player];
    ui.screen_pos = {centre.x - (int) width / 2, centre.y - (int) height / 2};
    ui.set_image_data();

    int tenSeconds = 10 * 1000 / 42;
    ui.on_key_down = [&](int sym)
    {
        if (sym == ',' || sym == 0x40000050) v.seek(-tenSeconds);      // SDLK_LEFT
        else if (sym == '.' || sym == 0x4000004f) v.seek(tenSeconds);  // SDLK_RIGHT
        else if (sym == '[') v.seek(-6 * tenSeconds);
        else if (sym == ']') v.seek(6 * tenSeconds);
    };

    // Saving the initial state lets the bar seek back to the start
    v.next_frame();
    ui.replay_frame = std::max(0, std::min(startFrame, ui.replay_st.end_frame));
    v.last_tick = v.clock.now();

    fprintf(stderr, "Playing %s (%d frames). Space pauses, a/z change speed, arrows seek 10 s, [ ] seek a minute; "
                    "close the window when done.\n", title.c_str(), ui.replay_st.end_frame);
    while (!ui.window_closed)
    {
        v.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return 0;
}
