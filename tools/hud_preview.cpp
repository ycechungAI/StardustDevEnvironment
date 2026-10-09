// Renders the live window's HUD panel (3rdparty/openbw/openbw/ui/hud.h) with sample numbers to a PPM image, to check
// its layout without running a game. Build: cmake --build build --target hud_preview; run: build/hud_preview out.ppm
#include "ui/hud.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
	const char* path = argc > 1 ? argv[1] : "hud_preview.ppm";
	int width = argc > 2 ? std::atoi(argv[2]) : 800;

	bwgame::hud::summary s;
	s.frame = 17321;
	bwgame::hud::player_summary us;
	us.name = "Stardust";
	us.race = "Protoss";
	us.color = 1;
	us.local = true;
	us.minerals = 1235;
	us.gas = 482;
	us.supply_used = 132;
	us.supply_max = 158;
	us.workers = 48;
	us.army_units = 31;
	us.army_supply = 84;
	us.army_minerals = 3825;
	us.army_gas = 1650;
	us.composition = {{"Zealot", 8}, {"Dragoon", 14}, {"High Templar", 2}, {"Archon", 2}, {"Observer", 2},
	                  {"Shuttle", 1}, {"Reaver", 2}};
	bwgame::hud::player_summary them;
	them.name = "Steamhammer2025";
	them.race = "Zerg";
	them.color = 0;
	them.minerals = 86;
	them.gas = 1204;
	them.supply_used = 151;
	them.supply_max = 200;
	them.workers = 55;
	them.army_units = 64;
	them.army_supply = 96;
	them.army_minerals = 4300;
	them.army_gas = 1900;
	them.composition = {{"Zergling", 24}, {"Hydralisk", 18}, {"Lurker", 6}, {"Mutalisk", 7}, {"Scourge", 4},
	                    {"Defiler", 2}, {"Ultralisk", 1}, {"Queen", 1}, {"Guardian", 2}, {"Devourer", 1},
	                    {"Inf Terran", 1}, {"Broodling", 3}, {"Overlord", 1}};
	s.players = {us, them};

	int height = bwgame::hud::panel_height;
	std::vector<uint32_t> pixels((size_t)width * height);
	bwgame::hud::canvas c{pixels.data(), (size_t)width, width, height};
	bwgame::hud::draw(c, s);

	FILE* f = std::fopen(path, "wb");
	if (!f) return 1;
	std::fprintf(f, "P6\n%d %d\n255\n", width, height);
	for (uint32_t p : pixels) {
		unsigned char rgb[3] = {(unsigned char)(p & 0xff), (unsigned char)((p >> 8) & 0xff), (unsigned char)((p >> 16) & 0xff)};
		std::fwrite(rgb, 1, 3, f);
	}
	std::fclose(f);
	std::printf("%s: %dx%d\n", path, width, height);
	return 0;
}
