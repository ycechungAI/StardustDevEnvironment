#ifndef BWGAME_UI_HUD_H
#define BWGAME_UI_HUD_H

// The live window's heads-up display: a panel below the game view with a toolbar (game time, and each player's
// minerals, gas, supply and workers) and each player's army (size and composition).
//
// Drawing only: ui_functions::hud_summary() collects the numbers from the game state. Kept free of SDL and the game
// state so it can be rendered offline (see tools/hud_preview.cpp).

#include "hud_font.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace bwgame {
namespace hud {

// Height of the panel, in pixels, for the given number of composition rows
constexpr int toolbar_height = 22;
constexpr int line_height = 16;
constexpr int composition_rows = 6;
constexpr int panel_height = toolbar_height + 6 + line_height + composition_rows * line_height + 6;

struct player_summary {
	std::string name;
	const char* race = "";
	int color = 0; // player color index (red, blue, teal, ...)
	bool local = false; // the player whose process shows the window

	int minerals = 0;
	int gas = 0;
	int supply_used = 0;
	int supply_max = 0;
	int workers = 0;

	int army_units = 0;
	int army_supply = 0;
	int army_minerals = 0;
	int army_gas = 0;
	std::vector<std::pair<const char*, int>> composition; // unit name and count, in display order
};

struct summary {
	int frame = 0;
	std::vector<player_summary> players;
};

// 0xAABBGGRR, as in the window's RGBA surface
constexpr uint32_t rgb(int r, int g, int b) {
	return 0xff000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

namespace colors {
constexpr uint32_t panel = rgb(18, 20, 24);
constexpr uint32_t toolbar = rgb(34, 37, 44);
constexpr uint32_t divider = rgb(60, 64, 74);
constexpr uint32_t text = rgb(228, 230, 235);
constexpr uint32_t label = rgb(140, 146, 158);
constexpr uint32_t minerals = rgb(110, 190, 255);
constexpr uint32_t gas = rgb(110, 225, 130);
constexpr uint32_t supply = rgb(240, 200, 90);
constexpr uint32_t timer = rgb(255, 255, 255);
} // namespace colors

// Brood War's player colors, by color index
inline uint32_t player_color(int index) {
	static const uint32_t table[] = {
			rgb(244, 4, 4), rgb(12, 72, 204), rgb(44, 180, 148), rgb(136, 64, 156),
			rgb(248, 140, 20), rgb(112, 48, 20), rgb(204, 224, 208), rgb(252, 252, 56),
	};
	if (index < 0 || index >= (int)(sizeof(table) / sizeof(table[0]))) return colors::text;
	return table[index];
}

struct canvas {
	uint32_t* data;
	size_t pitch; // in pixels
	int width;
	int height;

	void fill(int x, int y, int w, int h, uint32_t color) {
		int x0 = std::max(x, 0), y0 = std::max(y, 0);
		int x1 = std::min(x + w, width), y1 = std::min(y + h, height);
		for (int py = y0; py < y1; ++py) {
			std::fill(data + py * pitch + x0, data + py * pitch + std::max(x0, x1), color);
		}
	}

	static uint32_t blend(uint32_t under, uint32_t over, int alpha) {
		auto channel = [&](int shift) {
			int a = (under >> shift) & 0xff, b = (over >> shift) & 0xff;
			return (uint32_t)((a * (15 - alpha) + b * alpha) / 15) << shift;
		};
		return 0xff000000u | channel(16) | channel(8) | channel(0);
	}

	// Draws ASCII text with its top-left corner at (x, y); returns the x after the last character
	int text(int x, int y, const std::string& s, uint32_t color) {
		for (char c : s) {
			if (c >= hud_font::first_char && c <= hud_font::last_char && c != ' ') {
				const uint8_t* glyph = hud_font::glyphs[c - hud_font::first_char];
				for (int gy = 0; gy < hud_font::height; ++gy) {
					int py = y + gy;
					if (py < 0 || py >= height) continue;
					for (int gx = 0; gx < hud_font::width; ++gx) {
						int px = x + gx;
						if (px < 0 || px >= width) continue;
						int i = gy * hud_font::width + gx;
						int alpha = (i % 2 ? glyph[i / 2] : glyph[i / 2] >> 4) & 0xf;
						if (alpha) data[py * pitch + px] = blend(data[py * pitch + px], color, alpha);
					}
				}
			}
			x += hud_font::width;
		}
		return x;
	}
};

inline int text_width(const std::string& s) {
	return (int)s.size() * hud_font::width;
}

inline std::string format(const char* fmt, int a, int b = 0) {
	char buffer[64];
	std::snprintf(buffer, sizeof(buffer), fmt, a, b);
	return buffer;
}

// Game time as Brood War shows it on the Fastest speed: 42 ms per frame
inline std::string game_time(int frame) {
	int seconds = frame * 42 / 1000;
	char buffer[32];
	if (seconds >= 3600) {
		std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", seconds / 3600, seconds / 60 % 60, seconds % 60);
	} else {
		std::snprintf(buffer, sizeof(buffer), "%d:%02d", seconds / 60, seconds % 60);
	}
	return buffer;
}

// The player's name, marked with * for the player whose process shows the window
inline std::string player_label(const player_summary& p) {
	std::string label = p.name.empty() ? std::string(p.race) : p.name;
	if (p.local) label += "*";
	return label;
}

// A colored square, then the label: the player's color as on the minimap
inline int draw_player_label(canvas& c, int x, int y, const player_summary& p, size_t max_chars) {
	int box = hud_font::height - 6;
	c.fill(x, y + 3, box, box, player_color(p.color));
	auto label = player_label(p);
	if (label.size() > max_chars) label = label.substr(0, max_chars > 1 ? max_chars - 1 : 0) + "~";
	return c.text(x + box + 5, y, label, colors::text);
}

// Draws the panel into the canvas, which is the area below the game view
inline void draw(canvas& c, const summary& s) {
	c.fill(0, 0, c.width, c.height, colors::panel);
	c.fill(0, 0, c.width, toolbar_height, colors::toolbar);
	c.fill(0, toolbar_height, c.width, 1, colors::divider);

	// Toolbar: game time, then one section per player
	int text_y = (toolbar_height - hud_font::height) / 2;
	int x = 8;
	x = c.text(x, text_y, game_time(s.frame), colors::timer);
	x = c.text(x + 6, text_y, format("f%d", s.frame), colors::label);
	x += 12;

	int players = (int)s.players.size();
	if (players == 0) return;
	int section_width = (c.width - x) / players;
	for (int i = 0; i < players; ++i) {
		auto& p = s.players[i];
		int sx = x + i * section_width;
		c.fill(sx - 7, 3, 1, toolbar_height - 6, colors::divider);
		// Stats first, at the right of the section; the label gets what is left
		std::string minerals = format("%d", p.minerals);
		std::string gas = format("%d", p.gas);
		std::string supply = format("%d/%d", p.supply_used, p.supply_max);
		std::string workers = format("%dw", p.workers);
		int stats_width = text_width("  M " + minerals) + text_width(" G " + gas) + text_width(" S " + supply) +
				text_width(" " + workers) + 8;
		bool show_workers = section_width - stats_width >= 9 * hud_font::width;
		if (!show_workers) stats_width -= text_width(" " + workers);
		int label_chars = std::max(1, (section_width - stats_width - (hud_font::height - 1)) / hud_font::width);
		draw_player_label(c, sx, text_y, p, label_chars);

		int stats_x = sx + section_width - stats_width + text_width("  ");
		stats_x = c.text(stats_x, text_y, "M ", colors::label);
		stats_x = c.text(stats_x, text_y, minerals, colors::minerals);
		stats_x = c.text(stats_x, text_y, " G ", colors::label);
		stats_x = c.text(stats_x, text_y, gas, colors::gas);
		stats_x = c.text(stats_x, text_y, " S ", colors::label);
		stats_x = c.text(stats_x, text_y, supply, colors::supply);
		if (show_workers) c.text(stats_x, text_y, " " + workers, colors::label);
	}

	// Below: one column per player with their army's size and composition
	int column_width = c.width / players;
	int top = toolbar_height + 6;
	for (int i = 0; i < players; ++i) {
		auto& p = s.players[i];
		int cx = i * column_width + 8;
		int inner_width = column_width - 16;
		if (i > 0) c.fill(i * column_width, toolbar_height + 1, 1, c.height - toolbar_height - 1, colors::divider);

		std::string size = format("%d units  %d supply", p.army_units, p.army_supply);
		std::string value = format("cost %d/%d", p.army_minerals, p.army_gas);
		int header_x = cx;
		header_x = c.text(header_x, top, std::string(p.race) + " army ", colors::label);
		header_x = c.text(header_x, top, size, colors::text);
		if (header_x + text_width("  " + value) <= cx + inner_width) {
			c.text(cx + inner_width - text_width(value), top, value, colors::label);
		}
		c.fill(cx, top + line_height - 1, inner_width, 1, colors::divider);

		if (p.composition.empty()) {
			c.text(cx, top + line_height + 2, "no army", colors::label);
			continue;
		}
		// Composition: "count name" entries, filled column by column; unit names are at most 13 characters
		int entry_columns = std::max(1, inner_width / (17 * hud_font::width));
		int entry_width = inner_width / entry_columns;
		int capacity = entry_columns * composition_rows;
		int shown = (int)p.composition.size() > capacity ? capacity - 1 : (int)p.composition.size();
		for (int e = 0; e < shown; ++e) {
			int ex = cx + (e / composition_rows) * entry_width;
			int ey = top + line_height + 2 + (e % composition_rows) * line_height;
			auto& entry = p.composition[e];
			std::string count = format("%3d", entry.second);
			int nx = c.text(ex, ey, count, colors::text);
			std::string name = entry.first;
			size_t max_chars = (size_t)std::max(1, (entry_width - text_width(count) - 8) / hud_font::width - 1);
			if (name.size() > max_chars) name = name.substr(0, max_chars - 1) + "~";
			c.text(nx + 7, ey, name, colors::label);
		}
		if (shown < (int)p.composition.size()) {
			int rest = 0;
			for (size_t e = shown; e < p.composition.size(); ++e) rest += p.composition[e].second;
			int ex = cx + (shown / composition_rows) * entry_width;
			int ey = top + line_height + 2 + (shown % composition_rows) * line_height;
			c.text(ex, ey, format("%3d more", rest), colors::label);
		}
	}
}

} // namespace hud
} // namespace bwgame

#endif
