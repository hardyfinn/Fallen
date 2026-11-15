// Shared timing helpers: trigger/gate outputs, external clock measurement, Bjorklund.
#pragma once
#include <algorithm>
#include <cmath>
#include "../dsp/Common.hpp"

namespace fallen {

constexpr double MIN_INTERVAL = 0.001;  // 1 ms: shortest event spacing anywhere
constexpr float TRIG_LEN = 0.001f;

// 1 ms trigger. A retrigger during a pulse goes low for one sample first, so the next module sees two edges.
struct TrigOut {
	float t = 0.f;
	bool gap = false;
	void fire() {
		if (t > 0.f) gap = true;
		t = TRIG_LEN;
	}
	float process(float dt) {
		if (gap) { gap = false; return 0.f; }
		if (t > 0.f) { t -= dt; return 10.f; }
		return 0.f;
	}
	void reset() { t = 0.f; gap = false; }
};

// Gate of a set length. A new gate while high drops low for one sample first (envelopes retrigger).
struct GateOut {
	double t = 0.0;
	bool gap = false;
	void open(double len) {
		if (t > 0.0) gap = true;
		t = std::max(len, (double)TRIG_LEN);
	}
	void close() { t = 0.0; }
	float process(double dt) {
		if (gap) { gap = false; return 0.f; }
		if (t > 0.0) { t -= dt; return 10.f; }
		return 0.f;
	}
	bool high() const { return t > 0.0; }
	void reset() { t = 0.0; gap = false; }
};

// Period of an external clock from rising edges: median of the last 3 periods.
struct ClockMeter {
	double since = 0.0;
	double p[3] = {0, 0, 0};
	int n = 0, edges = 0;
	Trigger trig;
	// Returns true on a rising edge.
	bool process(float v, double dt) {
		since += dt;
		if (!trig.process(v)) return false;
		if (edges > 0 && since > 1e-4) {
			p[n % 3] = since;
			n++;
		}
		edges++;
		since = 0.0;
		return true;
	}
	bool valid() const { return n >= 1; }
	double period() const {
		if (n <= 0) return 0.0;
		if (n == 1) return p[0];
		if (n == 2) return 0.5 * (p[0] + p[1]);
		double a = p[0], b = p[1], c = p[2];
		return std::max(std::min(a, b), std::min(std::max(a, b), c));
	}
	void reset() { since = 0.0; n = 0; edges = 0; trig = Trigger(); }
};

// Euclidean pattern of `hits` in `len` steps (Bresenham form, the same necklace as Bjorklund),
// step 0 on, then rotated right by `rot`. Writes len values into out (no allocation).
inline void euclid(bool* out, int len, int hits, int rot = 0) {
	if (len < 1) return;
	hits = clampi(hits, 0, len);
	int k = ((rot % len) + len) % len;
	for (int i = 0; i < len; i++) {
		bool on = hits > 0 && ((i * hits) % len) < hits;
		out[(i + k) % len] = on;
	}
}

} // namespace fallen
