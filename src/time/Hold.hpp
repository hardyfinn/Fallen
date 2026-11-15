// HOLD: a gate and envelope whose length is a percentage of the gap between events.
#pragma once
#include <cmath>
#include "Clock.hpp"

namespace fallen {

struct Hold {
	struct Params {
		float length = 50.f;   // percent, 1..200
		float attack = 0.001f; // seconds
		float shape = 0.f;     // 0 = hard gate, 1 = exponential decay over the gate
	};
	double since = 0.0;
	bool seen = false;
	double gateLen = 0.0, t = 0.0;
	double lastGap = 0.25;
	float level = 0.f;
	float startLevel = 0.f;
	enum Stage { IDLE, ATTACK, BODY, RELEASE } stage = IDLE;
	static constexpr float REL = 0.002f;

	void reset() { since = 0.0; seen = false; stage = IDLE; level = 0.f; t = 0.0; lastGap = 0.25; }

	// intervalSec < 0: use the measured gap since the previous trigger.
	void trigger(const Params& p, double intervalSec) {
		double g;
		if (intervalSec > 0.0) g = intervalSec;
		else g = seen ? clampd(since, 0.001, 10.0) : 0.25;
		lastGap = g;
		seen = true;
		since = 0.0;
		gateLen = std::max(g * clampf(p.length, 1.f, 200.f) * 0.01, 0.001);
		t = 0.0;
		startLevel = level;
		stage = ATTACK;
	}

	bool gate() const { return stage == ATTACK || stage == BODY; }

	float process(double dt, const Params& p) {
		since += dt;
		switch (stage) {
			case ATTACK: {
				t += dt;
				float a = std::max(p.attack, 1e-4f);
				level = startLevel + (1.f - startLevel) * (float)std::min(t / a, 1.0);
				if (t >= a) stage = BODY;
				if (t >= gateLen) stage = RELEASE;
				break;
			}
			case BODY: {
				t += dt;
				double a = std::max(p.attack, 1e-4f);
				double span = std::max(gateLen - a, 1e-4);
				double x = clampd((t - a) / span, 0.0, 1.0);
				level = (float)std::pow(0.001, (double)clampf(p.shape, 0.f, 1.f) * x);
				if (t >= gateLen) stage = RELEASE;
				break;
			}
			case RELEASE:
				level -= (float)(dt / REL) * std::max(level, 0.05f);
				if (level <= 1e-4f) { level = 0.f; stage = IDLE; }
				break;
			default: level = 0.f; break;
		}
		return level;
	}
};

} // namespace fallen
