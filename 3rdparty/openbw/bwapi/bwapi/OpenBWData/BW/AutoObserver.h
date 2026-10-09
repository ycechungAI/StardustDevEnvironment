#pragma once

// The automatic observer camera of the OpenBW game window (added for the Stardust test harness).
//
// Each time the game has moved on, it looks at what changed since it last looked (damage, deaths, unloads, new
// bases, spells) and keeps the scenes worth showing: fights (sized by the value of both sides' units near them),
// drops, nukes, storms and other spells, new expansions, and each player's main army. It moves the window's camera to
// the best one without making the picture too busy: a scene is shown for a few seconds of wall time (the game runs as
// fast as it can) and only replaced by a clearly better one, except that the first hit of a real fight and sneak
// attacks (drops, raids on workers or buildings, cloaked units) get a cut at once. The camera pans to nearby scenes,
// jumps to far ones, and follows a scene only when it drifts out of the middle of the view.
//
// Like the observer of a broadcast, it also checks in on the players: every so often, while no big fight is on, it
// tours each player's mining bases (the busiest first) and production buildings, a couple of seconds each, and it
// picks up stray units: a scout (a worker, overlord, observer or a lone small unit) in enemy ground, and a loaded
// transport heading into it (a drop coming, shown before it unloads).
//
// The user takes over by moving the camera (arrow keys, the minimap, dragging with the right button); space switches
// between the automatic camera and the user's, and 'a' brings the automatic camera back (or, when it is on, makes it
// choose again).
//
// What it sees is also kept as events, which the harness writes to events.jsonl (for commentary and tools).
//
// Environment:
//   OPENBW_AUTO_CAMERA=0              no automatic camera
//   OPENBW_AUTO_CAMERA_DWELL=<s>      the shortest time a scene is shown (3.5 by default)
//   OPENBW_AUTO_CAMERA_RESUME=<s>     go back to the automatic camera after <s> seconds without the user moving it
//                                     (0, the default: only with the keys)
//   OPENBW_AUTO_CAMERA_TUNE=a=1,b=2   override the weights in `tuning` by name

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace BW {

template<typename ui_T>
struct auto_observer {
  using clock = std::chrono::steady_clock;

  // The weights. Values are minerals + gas; times are game frames, except those ending in _s (seconds of wall time)
  struct tuning {
    double dwell_s = 3.5;           // the shortest time a scene is shown
    double cut_gap_s = 1.2;         // the shortest time between two cuts to a first hit or sneak attack
    double cut_dwell_s = 1.5;       // how long a first hit or sneak attack is shown before cutting away from it
    double busy_cuts = 4;           // after this many cuts in 10 seconds, every scene is shown for dwell_s
    double linger_s = 1.5;          // how long to stay on a scene once it is over
    double switch_factor = 1.5;     // a new scene must score this many times the current one...
    double switch_margin = 25;      // ... plus this, to replace it (not needed to replace a quiet army or base)
    double rotate_s = 12;           // with nothing happening, show another army or base after this long
    double merge_radius = 320;      // damage this close to a fight is part of it
    double force_radius = 384;      // the units this close to a fight are at stake in it
    double heat_half_life = 64;     // frames for a fight's heat (its recent damage) to halve
    double end_frames = 120;        // a fight is over after this long without damage
    double notable_stakes = 100;    // a fight is worth showing once this much is at stake...
    double notable_losses = 75;     // ... or this much has died in it
    double first_hit_stakes = 250;  // a cut to the first hit of a fight needs this much at stake...
    double first_hit_force = 150;   // ... with each side having this much near it,
    double first_hit_losses = 200;  // ... or this much dead
    double battle_stakes = 1200;    // a fight becomes a battle (an event) with this much at stake...
    double battle_losses = 500;     // ... or this much dead
    double stakes_weight = 0.08;    // a fight's score: heat + stakes * stakes_weight + the bonuses below
    double sneak_bonus = 80;
    double novelty_bonus = 40;      // for a fight not shown yet, in its first 72 frames
    double raid_ratio = 0.35;       // a raid: the defender has less than this times the attacker's force near it...
    double raid_force = 75;         // ... and the attacker at least this much
    double drop_score = 220;
    double nuke_score = 500;
    double storm_score = 180;
    double spell_score = 60;
    double expansion_score = 70;
    double army_weight = 0.02;      // an army's score: its value * army_weight (+ moving_weight while it moves)
    double moving_weight = 0.03;
    double army_cap = 150;
    double army_move_value = 600;   // an army this big moving this far is an event
    double army_move_distance = 640;
    double jump_views = 1.25;       // the camera jumps to scenes further away than this many view widths
    double tour_s = 30;             // with no big fight on, check in on each player's bases this often
    double tour_stop_s = 2.5;       // how long each base or production area is shown in a check
    double tour_max_fight = 40;     // a check doesn't start, and stops, while a fight scores more than this
    double tour_bases = 2;          // mining bases shown per player in a check (those with the most workers)
    double scout_score = 75;        // a scout in enemy ground
    double scout_frames = 240;      // a scout is shown for at most this long...
    double scout_repeat = 2400;     // ... and not again for this long
    double scout_force = 300;       // a unit with more than this of its army near it is attacking, not scouting
    double transport_score = 170;   // a loaded transport heading into enemy ground
  } tune;

  ui_T& ui;

  bool auto_mode = true;
  double resume_s = 0;
  // Shown at the top right of the window
  std::string label;
  // What it saw, for the harness to take
  std::vector<CameraEvent> events;

  auto_observer(ui_T& ui) : ui(ui) {
    if (auto s = std::getenv("OPENBW_AUTO_CAMERA_DWELL"); s && std::atof(s) > 0) tune.dwell_s = std::atof(s);
    if (auto s = std::getenv("OPENBW_AUTO_CAMERA_RESUME"); s && std::atof(s) > 0) resume_s = std::atof(s);
    if (auto s = std::getenv("OPENBW_AUTO_CAMERA_TUNE"); s && *s) set_tuning(s);
    update_label();
  }

  // Called on the UI thread before each draw, while the game waits
  void tick() {
    auto now = clock::now();
    if (ui.camera_moved_by_user) {
      ui.camera_moved_by_user = false;
      last_user_move = now;
      if (auto_mode) set_mode(false, "the user moved the camera");
    }
    if (!auto_mode && resume_s > 0 && seconds(now - last_user_move) >= resume_s) set_mode(true, "the user stopped moving the camera");

    int frame = ui.st.current_frame;
    if (frame != scanned_frame) {
      scan(frame);
      scanned_frame = frame;
    }
    mark_shown();
    if (auto_mode) {
      choose(now);
      move_camera();
    }
    update_label();
  }

  // Keys pressed in the window: space switches between the automatic camera and the user's, 'a' turns the automatic
  // camera on (or makes it choose again)
  void on_key(int key) {
    if (key == ' ') set_mode(!auto_mode, "key");
    else if (key == 'a') {
      if (!auto_mode) set_mode(true, "key");
      else force_choose = true;
    }
  }

private:
  enum { scene_none, scene_fight, scene_poi, scene_army, scene_base };

  struct tracked_unit {
    bool alive = false;
    unsigned gen = 0;
    int type = -1;
    int owner = -1;
    int life = 0;
    int max_life = 1;
    int value = 0;
    int x = 0;
    int y = 0;
    int loaded = 0;
    bool fragile = false;  // a worker or a building
    int attacker = -1;
    int damage_frame = -1000000;
    int scan = 0;
    int scout_frame = -1000000;  // when it was last picked up as a scout or a transport
  };

  struct damage_t {
    int type, owner, attacker, x, y;
    double value;
    bool fragile;
  };

  struct death_t {
    int type, owner, attacker, x, y, value;
    double rest;  // the value of the life it had left
    bool recent;  // damaged by an enemy just before
  };

  struct fight_t {
    int id = 0;
    std::array<int, 2> players = {-1, -1};  // [0] hit first, [1] was hit
    double x = 0, y = 0, weight = 0;
    int first_frame = 0, last_frame = 0, decay_frame = 0;
    double heat = 0;   // recent damage, decaying
    double spell = 0;  // recent spells, decaying
    std::array<double, 2> damage = {0, 0};
    std::array<double, 2> losses = {0, 0};
    std::array<std::vector<std::array<int, 2>>, 2> lost;  // {unit type, count}
    std::array<double, 2> force = {0, 0};
    double stakes = 0, peak_stakes = 0;
    std::string sneak;
    bool notable = false, battle = false, shown = false, storm = false;
  };

  struct poi_t {
    int id = 0;
    std::string kind;  // drop, nuke, storm, spell, expansion, scout, drop_incoming
    std::string name;
    int player = -1;
    double x = 0, y = 0;
    double score = 0;
    bool priority = false;
    int until_frame = 0;
    int key = -1;  // the ghost painting a nuke, the scout, the transport
    bool shown = false;
  };

  struct army_t {
    double value = 0, x = 0, y = 0;
    std::deque<std::array<double, 3>> history;  // {frame, x, y}
    bool moving = false;
    bool reported = false;
    double report_x = 0, report_y = 0;
  };

  struct scene_t {
    int kind = scene_none;
    int id = -1;
    double x = 0, y = 0;
    double score = 0;
    bool priority = false;
    std::string label;
  };

  struct grid_t {
    std::vector<float> army, army_x, army_y, defense;
    std::vector<int> buildings, workers;
  };

  static constexpr int cell = 128;
  static constexpr int max_players = 8;

  clock::time_point last_user_move{};
  clock::time_point scene_start{};
  clock::time_point ended_at{};
  clock::time_point last_cut{};
  std::deque<clock::time_point> cuts;
  bool force_choose = true;
  bool scene_ended = false;
  bool jump_pending = true;
  bool transition = false;
  bool following = false;

  int scanned_frame = -1;
  int scan_n = 0;
  bool initialized = false;
  std::vector<tracked_unit> units;
  std::array<bool, max_players> active{};
  int gw = 0, gh = 0;
  std::array<grid_t, max_players> grids;
  std::array<std::vector<std::array<int, 2>>, max_players> depots;
  std::array<std::vector<std::array<int, 2>>, max_players> production;  // buildings that make army units
  struct cloaked_t { int owner, x, y; const bwgame::unit_t* u; };
  std::vector<cloaked_t> cloaked;
  struct unload_t { int frame, x, y; };
  std::array<std::deque<unload_t>, max_players> unloads;
  struct bullet_seen { int scan = -1, weapon = -1, owner = -1, remaining = 0; };
  std::vector<bullet_seen> bullets;

  std::vector<fight_t> fights;
  std::vector<poi_t> pois;
  std::array<army_t, max_players> armies;
  int next_id = 1;
  scene_t current;
  // A check on the players' bases: the stops still to show
  std::deque<scene_t> tour;
  bool touring = false;
  clock::time_point last_tour{};

  static double seconds(clock::duration d) {
    return std::chrono::duration<double>(d).count();
  }
  static double dist(double ax, double ay, double bx, double by) {
    return std::hypot(ax - bx, ay - by);
  }

  void set_tuning(const char* s) {
    std::pair<const char*, double*> fields[] = {
      {"dwell_s", &tune.dwell_s}, {"cut_gap_s", &tune.cut_gap_s}, {"cut_dwell_s", &tune.cut_dwell_s},
      {"busy_cuts", &tune.busy_cuts}, {"linger_s", &tune.linger_s}, {"switch_factor", &tune.switch_factor},
      {"switch_margin", &tune.switch_margin}, {"rotate_s", &tune.rotate_s}, {"merge_radius", &tune.merge_radius},
      {"force_radius", &tune.force_radius}, {"heat_half_life", &tune.heat_half_life},
      {"end_frames", &tune.end_frames}, {"notable_stakes", &tune.notable_stakes},
      {"notable_losses", &tune.notable_losses}, {"first_hit_stakes", &tune.first_hit_stakes},
      {"first_hit_force", &tune.first_hit_force}, {"first_hit_losses", &tune.first_hit_losses},
      {"battle_stakes", &tune.battle_stakes}, {"battle_losses", &tune.battle_losses},
      {"stakes_weight", &tune.stakes_weight}, {"sneak_bonus", &tune.sneak_bonus},
      {"novelty_bonus", &tune.novelty_bonus}, {"raid_ratio", &tune.raid_ratio}, {"raid_force", &tune.raid_force},
      {"drop_score", &tune.drop_score}, {"nuke_score", &tune.nuke_score}, {"storm_score", &tune.storm_score},
      {"spell_score", &tune.spell_score}, {"expansion_score", &tune.expansion_score},
      {"army_weight", &tune.army_weight}, {"moving_weight", &tune.moving_weight}, {"army_cap", &tune.army_cap},
      {"army_move_value", &tune.army_move_value}, {"army_move_distance", &tune.army_move_distance},
      {"jump_views", &tune.jump_views}, {"tour_s", &tune.tour_s}, {"tour_stop_s", &tune.tour_stop_s},
      {"tour_max_fight", &tune.tour_max_fight}, {"tour_bases", &tune.tour_bases}, {"scout_score", &tune.scout_score},
      {"scout_frames", &tune.scout_frames}, {"scout_repeat", &tune.scout_repeat}, {"scout_force", &tune.scout_force},
      {"transport_score", &tune.transport_score},
    };
    std::string all = s;
    size_t start = 0;
    while (start < all.size()) {
      size_t end = all.find(',', start);
      if (end == std::string::npos) end = all.size();
      auto item = all.substr(start, end - start);
      auto eq = item.find('=');
      bool known = false;
      if (eq != std::string::npos) {
        for (auto& f : fields) {
          if (item.compare(0, eq, f.first) == 0 && std::strlen(f.first) == eq) {
            *f.second = std::atof(item.c_str() + eq + 1);
            known = true;
          }
        }
      }
      if (!known) std::fprintf(stderr, "OPENBW_AUTO_CAMERA_TUNE: unknown setting '%s'\n", item.c_str());
      start = end + 1;
    }
  }

  // ---- Looking at the game ----

  bool ignored(const bwgame::unit_t* u) const {
    auto ut = u->unit_type;
    if (ut->flags & bwgame::unit_type_t::flag_turret) return true;
    switch (ut->id) {
    case bwgame::UnitTypes::Spell_Scanner_Sweep:
    case bwgame::UnitTypes::Special_Map_Revealer:
    case bwgame::UnitTypes::Spell_Dark_Swarm:
    case bwgame::UnitTypes::Spell_Disruption_Web:
    case bwgame::UnitTypes::Protoss_Scarab:
    case bwgame::UnitTypes::Terran_Nuclear_Missile:
    case bwgame::UnitTypes::Terran_Vulture_Spider_Mine:
      return true;
    default:
      return false;
    }
  }

  int unit_value(const bwgame::unit_t* u) const {
    if (ui.u_hallucination(u)) return 0;
    auto ut = u->unit_type;
    int v = ut->mineral_cost + ut->gas_cost;
    if (ui.ut_two_units_in_one_egg(ut)) v /= 2;
    switch (ut->id) {
    case bwgame::UnitTypes::Zerg_Lurker: v += 100; break;
    case bwgame::UnitTypes::Zerg_Guardian:
    case bwgame::UnitTypes::Zerg_Devourer: v += 200; break;
    default: break;
    }
    return v;
  }

  bool is_army(const bwgame::unit_t* u) const {
    auto ut = u->unit_type;
    return !ui.ut_building(ut) && !ui.ut_worker(ut) && ut->supply_required.raw_value > 0;
  }

  static bool is_production(int t) {
    switch ((bwgame::UnitTypes)t) {
    case bwgame::UnitTypes::Protoss_Gateway:
    case bwgame::UnitTypes::Protoss_Robotics_Facility:
    case bwgame::UnitTypes::Protoss_Stargate:
    case bwgame::UnitTypes::Terran_Barracks:
    case bwgame::UnitTypes::Terran_Factory:
    case bwgame::UnitTypes::Terran_Starport:
    case bwgame::UnitTypes::Zerg_Spawning_Pool:
    case bwgame::UnitTypes::Zerg_Hydralisk_Den:
    case bwgame::UnitTypes::Zerg_Spire:
    case bwgame::UnitTypes::Zerg_Greater_Spire:
      return true;
    default:
      return false;
    }
  }

  // A unit that may be scouting when it is alone in enemy ground
  bool may_scout(const bwgame::unit_t* u, int value) const {
    auto ut = u->unit_type;
    if (ui.ut_building(ut)) return false;
    if (ui.ut_worker(ut)) return true;
    switch (ut->id) {
    case bwgame::UnitTypes::Zerg_Overlord:
    case bwgame::UnitTypes::Protoss_Observer:
      return true;
    default:
      return is_army(u) && value <= 150;
    }
  }

  int defense_value(const bwgame::unit_t* u, int value, int loaded) const {
    auto ut = u->unit_type;
    if (!ui.ut_building(ut) || !ui.u_completed(u)) return 0;
    if (ut->id == bwgame::UnitTypes::Terran_Bunker) return loaded ? 100 + 50 * loaded : 0;
    if (ut->ground_weapon || ut->air_weapon) return std::max(value, 150);
    return 0;
  }

  int loaded_count(const bwgame::unit_t* u) const {
    int n = 0;
    for (auto* lu : ui.loaded_units(u)) {
      (void)lu;
      ++n;
    }
    return n;
  }

  bool enemies(int a, int b) const {
    return a >= 0 && b >= 0 && a < max_players && b < max_players && a != b && !ui.st.alliances[a][b];
  }

  int cell_index(int x, int y) const {
    int cx = std::clamp(x / cell, 0, gw - 1);
    int cy = std::clamp(y / cell, 0, gh - 1);
    return cy * gw + cx;
  }

  template<typename F>
  void for_cells_near(double x, double y, double r, F&& f) const {
    int x0 = std::max(0, (int)((x - r) / cell)), x1 = std::min(gw - 1, (int)((x + r) / cell));
    int y0 = std::max(0, (int)((y - r) / cell)), y1 = std::min(gh - 1, (int)((y + r) / cell));
    for (int cy = y0; cy <= y1; ++cy) {
      for (int cx = x0; cx <= x1; ++cx) {
        if (dist(cx * cell + cell / 2, cy * cell + cell / 2, x, y) <= r + cell / 2) f(cy * gw + cx);
      }
    }
  }

  double force_near(int p, double x, double y, double r) const {
    if (p < 0 || p >= max_players || !active[p]) return 0;
    double v = 0;
    auto& g = grids[p];
    for_cells_near(x, y, r, [&](int i) { v += g.army[i] + g.defense[i]; });
    return v;
  }

  bool building_near(int p, double x, double y, double r) const {
    if (p < 0 || p >= max_players || !active[p]) return false;
    bool r_ = false;
    for_cells_near(x, y, r, [&](int i) { if (grids[p].buildings[i]) r_ = true; });
    return r_;
  }

  bool enemy_building_near(int p, double x, double y, double r) const {
    for (int e = 0; e < max_players; ++e) {
      if (enemies(p, e) && building_near(e, x, y, r)) return true;
    }
    return false;
  }

  bool depot_near(int p, double x, double y, double r, double min_r = -1) const {
    if (p < 0 || p >= max_players) return false;
    for (auto& d : depots[p]) {
      double dd = dist(d[0], d[1], x, y);
      if (dd <= r && dd > min_r) return true;
    }
    return false;
  }

  void scan(int frame) {
    auto& st = ui.st;
    ++scan_n;
    gw = std::max(1, ((int)ui.game_st.map_width + cell - 1) / cell);
    gh = std::max(1, ((int)ui.game_st.map_height + cell - 1) / cell);
    for (int p = 0; p < max_players; ++p) {
      active[p] = false;
      depots[p].clear();
      production[p].clear();
    }
    cloaked.clear();
    std::vector<damage_t> damages;
    std::vector<death_t> deaths;
    std::vector<std::array<int, 3>> new_unloads;  // {player, x, y}
    std::vector<std::array<int, 3>> new_depots;   // {player, x, y}
    std::vector<std::array<int, 4>> nukes;        // {player, x, y, ghost}
    std::vector<std::array<int, 3>> strays;       // {player, unit, loaded}: possible scouts and loaded transports

    auto lose = [&](tracked_unit& tu) {
      if (initialized && tu.type != (int)bwgame::UnitTypes::Zerg_Larva) {
        bool recent = enemies(tu.attacker, tu.owner) && frame - tu.damage_frame <= 96;
        deaths.push_back({tu.type, tu.owner, recent ? tu.attacker : -1, tu.x, tu.y, tu.value,
                          (double)tu.value * tu.life / std::max(1, tu.max_life), recent});
      }
      tu.alive = false;
    };

    for (int p = 0; p < max_players; ++p) {
      for (bwgame::unit_t* u : bwgame::ptr(st.player_units[p])) {
        if (!active[p]) {
          active[p] = true;
          auto& g = grids[p];
          size_t n = (size_t)gw * gh;
          g.army.assign(n, 0);
          g.army_x.assign(n, 0);
          g.army_y.assign(n, 0);
          g.defense.assign(n, 0);
          g.buildings.assign(n, 0);
          g.workers.assign(n, 0);
        }
        if (ignored(u)) continue;
        auto ut = u->unit_type;
        int t = (int)ut->id;
        size_t i = u->index;
        if (i >= units.size()) units.resize(i + 1);
        auto& tu = units[i];
        int life = u->hp.integer_part() + u->shield_points.integer_part();
        bool same = tu.alive && tu.gen == u->unit_id_generation && tu.owner == p;
        if (!same) {
          if (tu.alive && tu.scan != scan_n) lose(tu);
          tu = tracked_unit();
          tu.alive = true;
          tu.gen = u->unit_id_generation;
          tu.owner = p;
          if (initialized && frame > 24 && ui.ut_resource_depot(ut)) new_depots.push_back({p, u->position.x, u->position.y});
        } else if (tu.type != t) {
          if (initialized && ui.ut_resource_depot(ut) && !ui.ut_resource_depot(ui.get_unit_type((bwgame::UnitTypes)tu.type))) {
            new_depots.push_back({p, u->position.x, u->position.y});
          }
        } else if (life < tu.life) {
          int attacker = u->last_attacking_player;
          int damage = tu.life - life;
          // A Terran building burning down is not being attacked
          bool burning = ui.ut_building(ut) && !ut->has_shield && life * 3 < tu.max_life && damage <= 12;
          if (enemies(attacker, p) && !burning) {
            damages.push_back({t, p, attacker, u->position.x, u->position.y,
                               (double)tu.value * damage / std::max(1, tu.max_life), tu.fragile});
            tu.attacker = attacker;
            tu.damage_frame = frame;
          }
        }
        if (tu.type != t) {
          tu.type = t;
          tu.value = unit_value(u);
          tu.max_life = ut->hitpoints.integer_part() + (ut->has_shield ? ut->shield_points : 0);
          tu.fragile = ui.ut_worker(ut) || ui.ut_building(ut);
        }
        tu.life = life;
        tu.x = u->position.x;
        tu.y = u->position.y;
        tu.scan = scan_n;

        int loaded = 0;
        if (ut->space_provided && !ui.ut_building(ut)) {
          loaded = loaded_count(u);
          if (same && loaded < tu.loaded && initialized) new_unloads.push_back({p, tu.x, tu.y});
        } else if (t == (int)bwgame::UnitTypes::Terran_Bunker) {
          loaded = loaded_count(u);
        }
        tu.loaded = loaded;

        if (!ui.u_loaded(u) && !ui.u_hallucination(u)) {
          auto& g = grids[p];
          int c = cell_index(tu.x, tu.y);
          if (is_army(u)) {
            g.army[c] += tu.value;
            g.army_x[c] += (float)tu.value * tu.x;
            g.army_y[c] += (float)tu.value * tu.y;
          } else if (int d = defense_value(u, tu.value, loaded)) {
            g.defense[c] += d;
          }
          if (ui.ut_building(ut)) ++g.buildings[c];
          if (ui.ut_worker(ut)) ++g.workers[c];
          if (ui.ut_resource_depot(ut)) depots[p].push_back({tu.x, tu.y});
          if (is_production(t) && ui.u_completed(u)) production[p].push_back({tu.x, tu.y});
          if (loaded > 0 && !ui.ut_building(ut)) strays.push_back({p, (int)i, loaded});
          else if (may_scout(u, tu.value)) strays.push_back({p, (int)i, 0});
        }
        if (ui.u_cloaked(u) || ui.u_requires_detector(u)) cloaked.push_back({p, tu.x, tu.y, u});
        if (t == (int)bwgame::UnitTypes::Terran_Ghost) {
          auto order = u->order_type->id;
          if (order == bwgame::Orders::NukePaint || order == bwgame::Orders::NukeTrack) {
            nukes.push_back({p, u->order_target.pos.x, u->order_target.pos.y, (int)i});
          }
        }
      }
    }
    for (auto& tu : units) {
      if (tu.alive && tu.scan != scan_n) lose(tu);
    }

    if (initialized) {
      for (auto& un : new_unloads) on_unload(frame, un[0], un[1], un[2]);
      for (auto& d : new_depots) on_new_depot(frame, d[0], d[1], d[2]);
      for (auto& d : damages) on_damage(frame, d);
      for (auto& d : deaths) on_death(frame, d);
      for (auto& n : nukes) on_nuke(frame, n[0], n[1], n[2], n[3]);
      for (auto& u : strays) on_stray(frame, u[0], u[1], u[2] > 0);
    }
    scan_bullets(frame);
    update_fights(frame);
    update_armies(frame);
    pois.erase(std::remove_if(pois.begin(), pois.end(), [&](auto& poi) { return frame > poi.until_frame + 240; }), pois.end());
    initialized = true;
  }

  static const char* spell_name(bwgame::WeaponTypes id) {
    switch (id) {
    case bwgame::WeaponTypes::Psionic_Storm: return "Psionic Storm";
    case bwgame::WeaponTypes::EMP_Shockwave: return "EMP Shockwave";
    case bwgame::WeaponTypes::Plague: return "Plague";
    case bwgame::WeaponTypes::Ensnare: return "Ensnare";
    case bwgame::WeaponTypes::Dark_Swarm: return "Dark Swarm";
    case bwgame::WeaponTypes::Stasis_Field: return "Stasis Field";
    case bwgame::WeaponTypes::Disruption_Web: return "Disruption Web";
    case bwgame::WeaponTypes::Maelstrom: return "Maelstrom";
    case bwgame::WeaponTypes::Irradiate: return "Irradiate";
    case bwgame::WeaponTypes::Lockdown: return "Lockdown";
    case bwgame::WeaponTypes::Yamato_Gun: return "Yamato Gun";
    case bwgame::WeaponTypes::Spawn_Broodlings: return "Spawn Broodlings";
    case bwgame::WeaponTypes::Mind_Control: return "Mind Control";
    case bwgame::WeaponTypes::Feedback: return "Feedback";
    default: return nullptr;
    }
  }

  void scan_bullets(int frame) {
    for (bwgame::bullet_t* b : bwgame::ptr(ui.st.active_bullets)) {
      if (!b->weapon_type) continue;
      size_t i = b->index;
      if (i >= bullets.size()) bullets.resize(i + 1);
      auto& seen = bullets[i];
      int weapon = (int)b->weapon_type->id;
      bool fresh = seen.scan != scan_n - 1 || seen.weapon != weapon || seen.owner != b->owner || b->remaining_time > seen.remaining;
      seen.scan = scan_n;
      seen.weapon = weapon;
      seen.owner = b->owner;
      seen.remaining = b->remaining_time;
      if (!fresh || !initialized || b->owner < 0 || b->owner >= max_players) continue;
      auto name = spell_name(b->weapon_type->id);
      if (!name) continue;
      auto pos = b->bullet_target_pos;
      if (pos.x == 0 && pos.y == 0) pos = b->position;
      on_spell(frame, b->owner, pos.x, pos.y, name, b->weapon_type->id == bwgame::WeaponTypes::Psionic_Storm);
    }
  }

  // ---- What happened ----

  std::string race_name(int p) const {
    if (p < 0 || p >= 12) return "";
    switch (ui.st.players[p].race) {
    case bwgame::race_t::zerg: return "Zerg";
    case bwgame::race_t::terran: return "Terran";
    case bwgame::race_t::protoss: return "Protoss";
    default: return "";
    }
  }

  // The units of these players near a place: {player, unit type, count}
  std::vector<std::array<int, 3>> units_near(double x, double y, double r, int p1, int p2 = -1) const {
    std::vector<std::array<int, 3>> r_;
    for (int p : {p1, p2}) {
      if (p < 0 || p >= max_players) continue;
      for (bwgame::unit_t* u : bwgame::ptr(ui.st.player_units[p])) {
        if (ignored(u) || ui.u_loaded(u) || ui.ut_building(u->unit_type)) continue;
        if (dist(u->position.x, u->position.y, x, y) > r) continue;
        int t = (int)u->unit_type->id;
        auto i = std::find_if(r_.begin(), r_.end(), [&](auto& e) { return e[0] == p && e[1] == t; });
        if (i == r_.end()) r_.push_back({p, t, 1});
        else ++(*i)[2];
      }
    }
    return r_;
  }

  CameraEvent& emit(int frame, std::string kind, double x, double y, int player = -1, int target = -1, double score = 0, int id = -1) {
    CameraEvent e;
    e.frame = frame;
    e.kind = std::move(kind);
    e.x = (int)x;
    e.y = (int)y;
    e.player = player;
    e.target = target;
    e.score = std::round(score);
    e.id = id;
    events.push_back(std::move(e));
    return events.back();
  }

  poi_t* find_poi(const std::string& kind, int player, double x, double y, double r, int frame) {
    for (auto& poi : pois) {
      if (poi.kind == kind && poi.player == player && frame <= poi.until_frame && dist(poi.x, poi.y, x, y) <= r) return &poi;
    }
    return nullptr;
  }

  poi_t& add_poi(std::string kind, std::string name, int player, double x, double y, double score, bool priority, int until_frame) {
    poi_t poi;
    poi.id = next_id++;
    poi.kind = std::move(kind);
    poi.name = std::move(name);
    poi.player = player;
    poi.x = x;
    poi.y = y;
    poi.score = score;
    poi.priority = priority;
    poi.until_frame = until_frame;
    pois.push_back(std::move(poi));
    return pois.back();
  }

  void on_unload(int frame, int p, int x, int y) {
    auto& recent = unloads[p];
    recent.push_back({frame, x, y});
    while (!recent.empty() && frame - recent.front().frame > 240) recent.pop_front();
    if (!enemy_building_near(p, x, y, 480)) return;
    if (auto poi = find_poi("drop", p, x, y, 320, frame)) {
      poi->until_frame = frame + 168;
      return;
    }
    auto& poi = add_poi("drop", race_name(p) + " drop", p, x, y, tune.drop_score, true, frame + 168);
    auto& e = emit(frame, "drop", x, y, p, -1, tune.drop_score, poi.id);
    e.units = units_near(x, y, 192, p);
  }

  // A unit away from home in enemy ground: a scout, or a loaded transport about to drop. Shown while it stays there,
  // for scout_frames at most, and not again for scout_repeat (a transport: until it unloads or leaves)
  void on_stray(int frame, int p, int i, bool transport) {
    auto& tu = units[i];
    if (building_near(p, tu.x, tu.y, 320)) return;
    if (!enemy_building_near(p, tu.x, tu.y, transport ? 960 : 640)) return;
    if (!transport && force_near(p, tu.x, tu.y, 384) > tune.scout_force) return;
    const char* kind = transport ? "drop_incoming" : "scout";
    for (auto& poi : pois) {
      if (poi.kind != kind || poi.key != i || poi.player != p || frame > poi.until_frame + 24) continue;
      poi.x = tu.x;
      poi.y = tu.y;
      int limit = tu.scout_frame + (int)(transport ? tune.scout_frames * 2 : tune.scout_frames);
      poi.until_frame = std::min(frame + 24, limit);
      return;
    }
    if (frame - tu.scout_frame < tune.scout_repeat) return;
    tu.scout_frame = frame;
    double score = transport ? tune.transport_score : tune.scout_score;
    auto& poi = add_poi(kind, race_name(p) + (transport ? " drop incoming" : " scout"), p, tu.x, tu.y, score, transport,
                        frame + 24);
    poi.key = i;
    auto& e = emit(frame, kind, tu.x, tu.y, p, -1, score, poi.id);
    e.units = transport ? units_near(tu.x, tu.y, 64, p) : std::vector<std::array<int, 3>>{{p, tu.type, 1}};
  }

  void on_new_depot(int frame, int p, int x, int y) {
    if (depot_near(p, x, y, 384, 32)) return;
    auto& poi = add_poi("expansion", race_name(p) + " expansion", p, x, y, tune.expansion_score, false, frame + 240);
    emit(frame, "expansion", x, y, p, -1, tune.expansion_score, poi.id);
  }

  void on_nuke(int frame, int p, int x, int y, int ghost) {
    for (auto& poi : pois) {
      if (poi.kind == "nuke" && poi.key == ghost && frame <= poi.until_frame + 24) {
        poi.until_frame = frame + 48;
        return;
      }
    }
    auto& poi = add_poi("nuke", race_name(p) + " nuke", p, x, y, tune.nuke_score, true, frame + 48);
    poi.key = ghost;
    emit(frame, "nuke", x, y, p, -1, tune.nuke_score, poi.id);
  }

  void on_spell(int frame, int p, int x, int y, const char* name, bool storm) {
    fight_t* nearest = nullptr;
    double best = 400;
    for (auto& f : fights) {
      if (f.players[0] != p && f.players[1] != p) continue;
      double d = dist(f.x, f.y, x, y);
      if (d <= best) {
        best = d;
        nearest = &f;
      }
    }
    double score = storm ? tune.storm_score : tune.spell_score;
    int id = -1;
    if (nearest) {
      decay(*nearest, frame);
      nearest->spell += score;
      if (storm) nearest->storm = true;
      id = nearest->id;
    } else {
      auto& poi = add_poi(storm ? "storm" : "spell", name, p, x, y, score, storm, frame + 96);
      id = poi.id;
    }
    auto& e = emit(frame, storm ? "storm" : "spell", x, y, p, -1, score, id);
    e.texts.push_back({"spell", name});
  }

  void decay(fight_t& f, int frame) {
    if (frame <= f.decay_frame) return;
    double k = std::pow(0.5, (frame - f.decay_frame) / tune.heat_half_life);
    f.heat *= k;
    f.spell *= k;
    f.weight *= k;
    f.decay_frame = frame;
  }

  fight_t* find_fight(int a, int b, double x, double y, double r) {
    fight_t* nearest = nullptr;
    double best = r;
    for (auto& f : fights) {
      bool pair = (f.players[0] == a && f.players[1] == b) || (f.players[0] == b && f.players[1] == a);
      if (!pair) continue;
      double d = dist(f.x, f.y, x, y);
      if (d <= best) {
        best = d;
        nearest = &f;
      }
    }
    return nearest;
  }

  // What kind of sneak attack this is, if any
  std::string classify(int frame, int attacker, int defender, int x, int y, bool fragile) const {
    double a = force_near(attacker, x, y, tune.force_radius);
    double d = force_near(defender, x, y, tune.force_radius);
    for (auto& un : unloads[attacker]) {
      if (frame - un.frame <= 240 && dist(un.x, un.y, x, y) <= 480 && (fragile || d <= a)) return "drop";
    }
    for (auto& c : cloaked) {
      if (c.owner == attacker && dist(c.x, c.y, x, y) <= 320 && ui.unit_is_undetected(c.u, defender)) return "cloaked";
    }
    if (fragile && a >= tune.raid_force && d < std::max(100.0, tune.raid_ratio * a) && depot_near(defender, x, y, 480)) return "raid";
    return "";
  }

  fight_t& add_damage(int frame, int attacker, int defender, int x, int y, double value) {
    auto f = find_fight(attacker, defender, x, y, tune.merge_radius);
    if (!f) {
      fight_t nf;
      nf.id = next_id++;
      nf.players = {attacker, defender};
      nf.x = x;
      nf.y = y;
      nf.first_frame = nf.last_frame = nf.decay_frame = frame;
      fights.push_back(std::move(nf));
      f = &fights.back();
    }
    decay(*f, frame);
    double w = std::max(value, 1.0);
    f->x = (f->x * f->weight + x * w) / (f->weight + w);
    f->y = (f->y * f->weight + y * w) / (f->weight + w);
    f->weight += w;
    f->heat += value;
    f->damage[attacker == f->players[0] ? 0 : 1] += value;
    f->last_frame = frame;
    return *f;
  }

  void on_damage(int frame, const damage_t& d) {
    auto& f = add_damage(frame, d.attacker, d.owner, d.x, d.y, d.value);
    if (f.sneak.empty() && frame - f.first_frame < 240) f.sneak = classify(frame, d.attacker, d.owner, d.x, d.y, d.fragile);
  }

  void on_death(int frame, const death_t& d) {
    fight_t* f = nullptr;
    if (d.recent) {
      f = &add_damage(frame, d.attacker, d.owner, d.x, d.y, d.rest);
    } else {
      // Killed with no damage seen before (in one shot): count it if it died in a fight
      double best = tune.merge_radius;
      for (auto& nf : fights) {
        if (nf.players[0] != d.owner && nf.players[1] != d.owner) continue;
        double dd = dist(nf.x, nf.y, d.x, d.y);
        if (dd <= best && frame - nf.last_frame <= tune.end_frames) {
          best = dd;
          f = &nf;
        }
      }
      if (!f) return;
      int attacker = f->players[0] == d.owner ? f->players[1] : f->players[0];
      f = &add_damage(frame, attacker, d.owner, d.x, d.y, d.rest);
    }
    int side = f->players[0] == d.owner ? 0 : 1;
    f->losses[side] += d.value;
    auto& lost = f->lost[side];
    auto i = std::find_if(lost.begin(), lost.end(), [&](auto& e) { return e[0] == d.type; });
    if (i == lost.end()) lost.push_back({d.type, 1});
    else ++(*i)[1];
  }

  std::vector<std::array<int, 3>> lost_list(const fight_t& f) const {
    std::vector<std::array<int, 3>> r;
    for (int side = 0; side < 2; ++side) {
      for (auto& e : f.lost[side]) r.push_back({f.players[side], e[0], e[1]});
    }
    return r;
  }

  void merge_fights(int frame) {
    for (size_t i = 0; i < fights.size(); ++i) {
      for (size_t j = i + 1; j < fights.size(); ++j) {
        auto& a = fights[i];
        auto& b = fights[j];
        bool pair = (a.players[0] == b.players[0] && a.players[1] == b.players[1]) ||
                    (a.players[0] == b.players[1] && a.players[1] == b.players[0]);
        if (!pair || dist(a.x, a.y, b.x, b.y) > tune.merge_radius * 0.6) continue;
        decay(a, frame);
        decay(b, frame);
        double w = a.weight + b.weight;
        if (w > 0) {
          a.x = (a.x * a.weight + b.x * b.weight) / w;
          a.y = (a.y * a.weight + b.y * b.weight) / w;
        }
        a.weight = w;
        a.heat += b.heat;
        a.spell += b.spell;
        bool flip = a.players[0] != b.players[0];
        for (int side = 0; side < 2; ++side) {
          int bs = flip ? 1 - side : side;
          a.damage[side] += b.damage[bs];
          a.losses[side] += b.losses[bs];
          for (auto& e : b.lost[bs]) {
            auto k = std::find_if(a.lost[side].begin(), a.lost[side].end(), [&](auto& x) { return x[0] == e[0]; });
            if (k == a.lost[side].end()) a.lost[side].push_back(e);
            else (*k)[1] += e[1];
          }
        }
        a.first_frame = std::min(a.first_frame, b.first_frame);
        a.last_frame = std::max(a.last_frame, b.last_frame);
        a.peak_stakes = std::max(a.peak_stakes, b.peak_stakes);
        if (a.sneak.empty()) a.sneak = b.sneak;
        a.notable |= b.notable;
        a.battle |= b.battle;
        a.shown |= b.shown;
        a.storm |= b.storm;
        if (current.kind == scene_fight && current.id == b.id) current.id = a.id;
        fights.erase(fights.begin() + j);
        --j;
      }
    }
  }

  void update_fights(int frame) {
    merge_fights(frame);
    for (auto& f : fights) {
      decay(f, frame);
      f.force[0] = force_near(f.players[0], f.x, f.y, tune.force_radius);
      f.force[1] = force_near(f.players[1], f.x, f.y, tune.force_radius);
      double lo = std::min(f.force[0], f.force[1]), hi = std::max(f.force[0], f.force[1]);
      f.stakes = lo * 1.5 + hi * 0.5;
      f.peak_stakes = std::max(f.peak_stakes, f.stakes);
      double losses = f.losses[0] + f.losses[1];
      if (!f.notable && (f.stakes >= tune.notable_stakes || losses >= tune.notable_losses || !f.sneak.empty())) {
        f.notable = true;
        auto& e = emit(frame, f.sneak.empty() ? "first_hit" : "sneak_attack", f.x, f.y, f.players[0], f.players[1], f.stakes, f.id);
        if (!f.sneak.empty()) e.texts.push_back({"sneak", f.sneak});
        e.values.push_back({"player_force", std::round(f.force[0])});
        e.values.push_back({"target_force", std::round(f.force[1])});
        e.units = units_near(f.x, f.y, tune.force_radius, f.players[0], f.players[1]);
      }
      if (f.notable && !f.battle && (f.stakes >= tune.battle_stakes || losses >= tune.battle_losses)) {
        f.battle = true;
        auto& e = emit(frame, "battle", f.x, f.y, f.players[0], f.players[1], f.stakes, f.id);
        e.values.push_back({"player_force", std::round(f.force[0])});
        e.values.push_back({"target_force", std::round(f.force[1])});
        e.values.push_back({"player_losses", std::round(f.losses[0])});
        e.values.push_back({"target_losses", std::round(f.losses[1])});
        e.units = units_near(f.x, f.y, tune.force_radius, f.players[0], f.players[1]);
      }
    }
    for (auto& f : fights) {
      if (frame - f.last_frame <= tune.end_frames || !f.notable) continue;
      // Over: the side that lost much less won it
      int winner = -1, loser = -1;
      if (f.losses[0] + f.losses[1] >= 50) {
        if (f.losses[0] < f.losses[1] * 0.75) winner = 0;
        else if (f.losses[1] < f.losses[0] * 0.75) winner = 1;
      }
      int p = winner >= 0 ? winner : 0;
      loser = 1 - p;
      auto& e = emit(frame, "battle_end", f.x, f.y, f.players[p], f.players[loser], f.peak_stakes, f.id);
      e.texts.push_back({"result", winner >= 0 ? "won" : "even"});
      if (!f.sneak.empty()) e.texts.push_back({"sneak", f.sneak});
      e.values.push_back({"duration_seconds", std::round((f.last_frame - f.first_frame) * 0.042)});
      e.values.push_back({"player_losses", std::round(f.losses[p])});
      e.values.push_back({"target_losses", std::round(f.losses[loser])});
      e.lost = lost_list(f);
    }
    fights.erase(std::remove_if(fights.begin(), fights.end(), [&](auto& f) {
      return frame - f.last_frame > tune.end_frames;
    }), fights.end());
  }

  void update_armies(int frame) {
    std::vector<double> sums;
    for (int p = 0; p < max_players; ++p) {
      auto& a = armies[p];
      if (!active[p]) {
        a = army_t();
        continue;
      }
      // The main army: the 3x3 cells holding the most value (summed with an integral image)
      auto& g = grids[p];
      sums.assign((size_t)(gw + 1) * (gh + 1), 0);
      for (int y = 0; y < gh; ++y) {
        for (int x = 0; x < gw; ++x) {
          sums[(y + 1) * (gw + 1) + x + 1] = g.army[y * gw + x] + sums[y * (gw + 1) + x + 1] + sums[(y + 1) * (gw + 1) + x] - sums[y * (gw + 1) + x];
        }
      }
      double best = 0;
      int bx = 0, by = 0;
      for (int y = 0; y < gh; ++y) {
        for (int x = 0; x < gw; ++x) {
          int x1 = std::min(gw, x + 3), y1 = std::min(gh, y + 3);
          double v = sums[y1 * (gw + 1) + x1] - sums[y * (gw + 1) + x1] - sums[y1 * (gw + 1) + x] + sums[y * (gw + 1) + x];
          if (v > best) {
            best = v;
            bx = x;
            by = y;
          }
        }
      }
      a.value = best;
      if (best <= 0) {
        a.history.clear();
        a.moving = false;
        a.reported = false;
        continue;
      }
      double sx = 0, sy = 0;
      for (int y = by; y < std::min(gh, by + 3); ++y) {
        for (int x = bx; x < std::min(gw, bx + 3); ++x) {
          sx += g.army_x[y * gw + x];
          sy += g.army_y[y * gw + x];
        }
      }
      a.x = sx / best;
      a.y = sy / best;
      if (a.history.empty() || frame - a.history.back()[0] >= 24) a.history.push_back({(double)frame, a.x, a.y});
      while (a.history.size() > 1 && frame - a.history[1][0] >= 72) a.history.pop_front();
      a.moving = best >= 300 && frame - a.history.front()[0] >= 48 && dist(a.history.front()[1], a.history.front()[2], a.x, a.y) >= 160;

      if (best >= tune.army_move_value) {
        if (!a.reported) {
          a.reported = true;
          a.report_x = a.x;
          a.report_y = a.y;
        } else if (a.moving && dist(a.report_x, a.report_y, a.x, a.y) >= tune.army_move_distance) {
          auto& e = emit(frame, "army_move", a.x, a.y, p, -1, best);
          e.values.push_back({"from_x", std::round(a.report_x)});
          e.values.push_back({"from_y", std::round(a.report_y)});
          e.units = units_near(a.x, a.y, 256, p);
          a.report_x = a.x;
          a.report_y = a.y;
        }
      } else if (best < tune.army_move_value * 0.5) {
        a.reported = false;
      }
    }
  }

  // ---- Choosing what to show ----

  double fight_score(const fight_t& f, int frame) const {
    double s = f.heat + f.stakes * tune.stakes_weight + f.spell;
    if (!f.sneak.empty()) s += tune.sneak_bonus;
    if (!f.shown && frame - f.first_frame < 72) s += tune.novelty_bonus;
    return s;
  }

  bool fight_priority(const fight_t& f) const {
    if (f.shown) return false;
    if (!f.sneak.empty() || f.storm) return true;
    if (std::min(f.force[0], f.force[1]) >= tune.first_hit_force && f.stakes >= tune.first_hit_stakes) return true;
    return f.losses[0] + f.losses[1] >= tune.first_hit_losses;
  }

  std::string fight_label(const fight_t& f) const {
    auto race = race_name(f.players[0]);
    if (f.sneak == "drop") return race + " drop";
    if (f.sneak == "raid") return race + " raid";
    if (f.sneak == "cloaked") return race + " cloaked attack";
    return f.battle ? "Battle" : "Fight";
  }

  bool live(const fight_t& f, int frame) const {
    return f.notable && frame - f.last_frame <= tune.end_frames;
  }

  std::vector<scene_t> candidates(int frame) const {
    std::vector<scene_t> r;
    for (auto& f : fights) {
      if (!live(f, frame)) continue;
      r.push_back({scene_fight, f.id, f.x, f.y, fight_score(f, frame), fight_priority(f), fight_label(f)});
    }
    for (auto& poi : pois) {
      if (frame > poi.until_frame) continue;
      r.push_back({scene_poi, poi.id, poi.x, poi.y, poi.score, poi.priority && !poi.shown, poi.name});
    }
    for (int p = 0; p < max_players; ++p) {
      if (auto s = army_scene(p); s.kind != scene_none && armies[p].value >= 150) r.push_back(s);
    }
    return r;
  }

  scene_t army_scene(int p) const {
    auto& a = armies[p];
    if (a.value < 100) return {};
    double s = a.value * tune.army_weight + (a.moving ? a.value * tune.moving_weight : 0);
    return {scene_army, p, a.x, a.y, std::min(tune.army_cap, s), false, race_name(p) + (a.moving ? " army moving" : " army")};
  }

  // With nothing happening: the biggest army (or, to show another, the next player's), else the next player's base
  scene_t fallback(bool next = false) const {
    int from = next ? current.id : -1;
    scene_t best;
    for (int i = 1; i <= max_players; ++i) {
      int p = (from + i + max_players) % max_players;
      auto s = army_scene(p);
      if (s.kind == scene_none || (next && current.kind == scene_army && p == current.id)) continue;
      if (next) return s;
      if (best.kind == scene_none || armies[p].value > armies[best.id].value) best = s;
    }
    if (best.kind != scene_none) return best;
    for (int i = 1; i <= max_players; ++i) {
      int p = (from + i + max_players) % max_players;
      if (depots[p].empty() || (next && current.kind == scene_base && p == current.id)) continue;
      return {scene_base, p, (double)depots[p][0][0], (double)depots[p][0][1], 1, false, race_name(p) + " base"};
    }
    return {};
  }

  int workers_near(int p, double x, double y, double r) const {
    int n = 0;
    for_cells_near(x, y, r, [&](int i) { n += grids[p].workers[i]; });
    return n;
  }

  // A check on every player: their busiest mining bases, then where most of their production buildings stand
  std::deque<scene_t> make_tour() const {
    std::deque<scene_t> stops;
    for (int p = 0; p < max_players; ++p) {
      if (!active[p] || depots[p].empty()) continue;
      std::vector<std::pair<int, std::array<int, 2>>> bases;
      for (auto& d : depots[p]) bases.push_back({workers_near(p, d[0], d[1], 320), d});
      std::stable_sort(bases.begin(), bases.end(), [](auto& a, auto& b) { return a.first > b.first; });
      std::vector<std::array<double, 2>> shown;
      for (auto& [workers, d] : bases) {
        if ((double)shown.size() >= tune.tour_bases) break;
        if (workers == 0 && !shown.empty()) break;
        shown.push_back({(double)d[0], (double)d[1]});
        stops.push_back({scene_base, p, (double)d[0], (double)d[1], 1, false,
                         race_name(p) + " mining (" + std::to_string(workers) + " workers)"});
      }
      // The production building with the most others near it, and the middle of them
      int most = 0;
      double px = 0, py = 0;
      for (auto& b : production[p]) {
        int n = 0;
        double sx = 0, sy = 0;
        for (auto& o : production[p]) {
          if (dist(b[0], b[1], o[0], o[1]) > 384) continue;
          ++n;
          sx += o[0];
          sy += o[1];
        }
        if (n > most) {
          most = n;
          px = sx / n;
          py = sy / n;
        }
      }
      bool in_view = std::any_of(shown.begin(), shown.end(), [&](auto& b) { return dist(b[0], b[1], px, py) < 200; });
      if (most > 0 && !in_view) {
        stops.push_back({scene_base, p, px, py, 1, false,
                         race_name(p) + " production (" + std::to_string(most) + (most == 1 ? " building)" : " buildings)")});
      }
    }
    return stops;
  }

  // Brings the current scene up to date; false once it is over
  bool refresh(scene_t& s, int frame) const {
    switch (s.kind) {
    case scene_fight:
      for (auto& f : fights) {
        if (f.id != s.id) continue;
        if (!live(f, frame)) return false;
        s.x = f.x;
        s.y = f.y;
        s.score = fight_score(f, frame);
        s.label = fight_label(f);
        return true;
      }
      return false;
    case scene_poi:
      for (auto& poi : pois) {
        if (poi.id != s.id) continue;
        if (frame > poi.until_frame) return false;
        s.x = poi.x;
        s.y = poi.y;
        s.score = poi.score;
        return true;
      }
      return false;
    case scene_army: {
      auto a = army_scene(s.id);
      if (a.kind == scene_none) return false;
      s.x = a.x;
      s.y = a.y;
      s.score = a.score;
      s.label = a.label;
      return true;
    }
    case scene_base:
      return true;
    default:
      return false;
    }
  }

  void set_shown(const scene_t& s) {
    if (s.kind == scene_fight) {
      for (auto& f : fights) if (f.id == s.id) f.shown = true;
    } else if (s.kind == scene_poi) {
      for (auto& poi : pois) if (poi.id == s.id) poi.shown = true;
    }
  }

  // Whatever is in the view counts as shown, so there is no cut to it
  void mark_shown() {
    double x0 = ui.screen_pos.x, y0 = ui.screen_pos.y;
    double x1 = x0 + (double)ui.view_width, y1 = y0 + (double)ui.view_height;
    auto in_view = [&](double x, double y) { return x >= x0 && x < x1 && y >= y0 && y < y1; };
    for (auto& f : fights) {
      if (f.notable && in_view(f.x, f.y)) f.shown = true;
    }
    for (auto& poi : pois) {
      if (in_view(poi.x, poi.y)) poi.shown = true;
    }
  }

  void switch_to(const scene_t& s, clock::time_point now, const char* reason, bool cut) {
    int frame = ui.st.current_frame;
    auto& e = emit(frame, "camera", s.x, s.y, -1, -1, s.score, s.id);
    e.texts.push_back({"scene", s.label});
    e.texts.push_back({"reason", reason});
    if (touring) last_tour = now;  // a check cut short starts over tour_s later
    touring = false;
    current = s;
    scene_start = now;
    scene_ended = false;
    transition = true;
    following = false;
    if (cut) {
      last_cut = now;
      cuts.push_back(now);
    }
    set_shown(s);
  }

  void choose(clock::time_point now) {
    int frame = ui.st.current_frame;
    while (!cuts.empty() && seconds(now - cuts.front()) > 10) cuts.pop_front();
    bool have = current.kind != scene_none && refresh(current, frame);
    if (!have && current.kind != scene_none && !scene_ended) {
      scene_ended = true;
      ended_at = now;
    }
    auto list = candidates(frame);

    // A fight starting where the camera already is (at a drop, a nuke, a spell or an army) takes over the scene
    if (have && (current.kind == scene_poi || current.kind == scene_army)) {
      for (auto& c : list) {
        if (c.kind != scene_fight || dist(c.x, c.y, current.x, current.y) > (current.kind == scene_army ? 400 : 320)) continue;
        current.kind = scene_fight;
        current.id = c.id;
        current.x = c.x;
        current.y = c.y;
        current.score = c.score;
        current.label = c.label;
        set_shown(current);
        break;
      }
    }

    const scene_t* best = nullptr;
    const scene_t* best_priority = nullptr;
    for (auto& c : list) {
      if (c.kind == current.kind && c.id == current.id) continue;
      if (!best || c.score > best->score) best = &c;
      if (c.priority && (!best_priority || c.score > best_priority->score)) best_priority = &c;
    }
    double dwell = seconds(now - scene_start);

    if (force_choose || current.kind == scene_none) {
      auto s = best ? *best : fallback();
      if (s.kind != scene_none) {
        force_choose = false;
        jump_pending = true;
        switch_to(s, now, "start", false);
      }
    } else if (best_priority && seconds(now - last_cut) >= tune.cut_gap_s &&
               (!current.priority || dwell >= tune.cut_dwell_s) &&
               ((double)cuts.size() < tune.busy_cuts || dwell >= tune.dwell_s)) {
      switch_to(*best_priority, now, best_priority->kind == scene_fight ? "first hit" : "event", true);
    } else if (touring) {
      if (best && best->kind == scene_fight && best->score > tune.tour_max_fight) {
        switch_to(*best, now, "action", false);
      } else if (dwell >= tune.tour_stop_s) {
        if (!tour.empty()) {
          auto s = tour.front();
          tour.pop_front();
          switch_to(s, now, "base check", false);
          touring = true;
        } else {
          touring = false;
          last_tour = now;
          auto s = best ? *best : fallback();
          if (s.kind != scene_none) switch_to(s, now, "base check over", false);
        }
      }
    } else if (seconds(now - last_tour) >= tune.tour_s && (!have || dwell >= tune.dwell_s) && !current.priority &&
               std::none_of(list.begin(), list.end(), [&](auto& c) {
                 return (c.kind == scene_fight && c.score > tune.tour_max_fight) || (c.kind == scene_poi && c.priority);
               })) {
      tour = make_tour();
      last_tour = now;
      if (!tour.empty()) {
        auto s = tour.front();
        tour.pop_front();
        switch_to(s, now, "base check", false);
        touring = true;
      }
    } else if (!have) {
      if (seconds(now - ended_at) >= tune.linger_s && dwell >= tune.cut_dwell_s) {
        auto s = best ? *best : fallback();
        if (s.kind != scene_none && !(s.kind == current.kind && s.id == current.id)) switch_to(s, now, "scene over", false);
      }
    } else {
      bool quiet = current.kind == scene_army || current.kind == scene_base;
      double needed = current.score * tune.switch_factor + (quiet ? 0 : tune.switch_margin);
      if (best && dwell >= tune.dwell_s && best->score > needed) {
        switch_to(*best, now, "better scene", false);
      } else if (quiet && dwell >= tune.rotate_s) {
        auto s = fallback(true);
        if (s.kind != scene_none) switch_to(s, now, "nothing happening", false);
        else scene_start = now;
      }
    }
  }

  void move_camera() {
    if (current.kind == scene_none) return;
    double vw = (double)ui.view_width, vh = (double)ui.view_height;
    double mw = ui.game_st.map_width, mh = ui.game_st.map_height;
    double cx = ui.screen_pos.x + vw / 2, cy = ui.screen_pos.y + vh / 2;
    // Where the middle of the view can go
    double tx = mw > vw ? std::clamp(current.x, vw / 2, mw - vw / 2) : mw / 2;
    double ty = mh > vh ? std::clamp(current.y, vh / 2, mh - vh / 2) : mh / 2;
    double dx = tx - cx, dy = ty - cy;
    double d = std::hypot(dx, dy);
    double step;
    if (jump_pending || d > tune.jump_views * vw) {
      jump_pending = false;
      transition = false;
      step = d;
    } else if (transition) {
      if (d < 24) transition = false;
      step = std::clamp(d * 0.2, 4.0, 60.0);
    } else {
      // Follow the scene only when it leaves the middle half of the view, until it is near the middle again
      if (std::abs(dx) > vw * 0.25 || std::abs(dy) > vh * 0.25) following = true;
      if (d < vw * 0.08) following = false;
      if (!following) return;
      step = std::clamp(d * 0.15, 2.0, 24.0);
    }
    if (d < 1) return;
    double k = std::min(1.0, step / d);
    ui.screen_pos = bwgame::xy((int)std::lround(cx + dx * k - vw / 2), (int)std::lround(cy + dy * k - vh / 2));
  }

  void set_mode(bool on, const char* reason) {
    if (on == auto_mode) return;
    auto_mode = on;
    if (on) force_choose = true;
    auto& e = emit(ui.st.current_frame, "camera_mode", ui.screen_pos.x + (double)ui.view_width / 2,
                   ui.screen_pos.y + (double)ui.view_height / 2);
    e.texts.push_back({"mode", on ? "auto" : "manual"});
    e.texts.push_back({"reason", reason});
  }

  void update_label() {
    if (auto_mode) label = "\x07" "AUTO \x04" + (current.kind == scene_none ? std::string("waiting") : current.label);
    else label = "\x03" "MANUAL \x05(space: auto)";
  }
};

}
