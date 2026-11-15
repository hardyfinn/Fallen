// LANES: 6 rows x 32 steps, each row with its own loop length and scroll, thinned by INTENSITY.
#pragma once
#include <cstdint>
#include <cmath>
#include "Clock.hpp"

namespace fallen {

struct LanesPattern {
	uint32_t cells[6] = {0, 0, 0, 0, 0, 0};
	int8_t pitch[32] = {};
	bool get(int r, int s) const { return (cells[r] >> s) & 1u; }
	void set(int r, int s, bool v) { if (v) cells[r] |= (1u << s); else cells[r] &= ~(1u << s); }
};

struct Lanes {
	static constexpr int ROWS = 6, STEPS = 32;
	LanesPattern pats[8];
	int cur = 0, pending = -1;
	int pos[ROWS] = {};
	int rot[ROWS] = {};
	bool fresh = true;

	struct Params {
		int len[ROWS] = {16, 16, 16, 16, 16, 16};
		int scroll[ROWS] = {};
		bool mute[ROWS] = {};
		float intensity = 1.f;
		int pattern = 0;
		bool switchNow = false;
	};

	static int bitrev5(int s) {
		int r = 0;
		for (int i = 0; i < 5; i++) if (s & (1 << i)) r |= 1 << (4 - i);
		return r;
	}

	void reset() {
		for (int r = 0; r < ROWS; r++) { pos[r] = 0; rot[r] = 0; }
		fresh = true;
		if (pending >= 0) { cur = pending; pending = -1; }
	}

	// Does original cell idx of row r survive INTENSITY?
	bool passes(int r, int idx, int len, float intensity) const {
		const LanesPattern& P = pats[cur];
		int active = 0, better = 0, key = bitrev5(idx);
		for (int s = 0; s < len; s++) {
			if (!P.get(r, s)) continue;
			active++;
			if (bitrev5(s) < key) better++;
		}
		int keep = (int)std::ceil(clampf(intensity, 0.f, 1.f) * active - 1e-6f);
		return better < keep;
	}

	// Which original cell plays at loop position s of row r.
	int cellAt(int r, int s, int len) const { return ((s - rot[r]) % len + len) % len; }

	struct Out { bool ev[ROWS] = {}; float pitch = 0.f; bool pitchEv = false; };

	// One clock: play the current position of every row, then advance.
	Out clock(const Params& p) {
		Out o;
		if (p.pattern != cur) {
			if (p.switchNow) cur = clampi(p.pattern, 0, 7);
			else pending = clampi(p.pattern, 0, 7);
		}
		fresh = false;
		for (int r = 0; r < ROWS; r++) {
			int len = clampi(p.len[r], 1, STEPS);
			if (pos[r] >= len) pos[r] = 0;
			int idx = cellAt(r, pos[r], len);
			bool on = pats[cur].get(r, idx) && !p.mute[r] && passes(r, idx, len, p.intensity);
			o.ev[r] = on;
			if (r == ROWS - 1 && on) { o.pitch = pats[cur].pitch[idx] / 12.f; o.pitchEv = true; }
			pos[r]++;
			if (pos[r] >= len) {
				pos[r] = 0;
				rot[r] = ((rot[r] + p.scroll[r]) % len + len) % len;
				if (r == 0 && pending >= 0) { cur = pending; pending = -1; }
			}
		}
		return o;
	}
};

} // namespace fallen
