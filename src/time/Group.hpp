// GROUP: bursts of N events whose spacing is compressed or expanded, fired every PERIOD.
#pragma once
#include <cmath>
#include "Clock.hpp"

namespace fallen {

struct Group {
	struct Params {
		int count = 5;
		float spaceMs = 60.f;
		float curve = -0.5f;    // -1..1
		float periodMs = 1000.f;
		float length = 50.f;    // percent
		float rate = 0.f;       // 0..0.25 of a triangle cycle per group
		float depth = 0.f;      // octaves
		bool external = false;  // TRIG IN patched: groups only on FIRE
	};
	struct Out {
		bool event = false, first = false, last = false;
		double gate = 0.0;
		int index = 0, count = 1;
		bool busy = false;
	};

	bool active = false;
	int k = 0, cnt = 1;
	double gaps[16] = {};
	double untilEvent = 0.0;
	double untilGroup = 0.0;  // time to next automatic group start
	double space = 0.06;
	double driftPhase = 0.0;
	int lastIndex = 0;

	void reset() {
		active = false; k = 0; untilEvent = 0.0; untilGroup = 0.0; driftPhase = 0.0; lastIndex = 0;
	}

	static float tri(double ph) {
		double f = ph - std::floor(ph);
		return (float)(f < 0.5 ? 4.0 * f - 1.0 : 3.0 - 4.0 * f);
	}

	// Start a group now. Returns the time until the next automatic group.
	double start(const Params& p) {
		cnt = clampi(p.count, 1, 16);
		space = std::max(p.spaceMs * 0.001 * std::pow(2.0, p.depth * tri(driftPhase + 0.25)), MIN_INTERVAL);
		driftPhase += p.rate;
		double span = 0.0;
		for (int i = 1; i < cnt; i++) {
			double t = cnt > 2 ? (double)(i - 1) / (cnt - 2) : 0.0;
			gaps[i] = std::max(space * std::pow(2.0, p.curve * 3.0 * t), MIN_INTERVAL);
			span += gaps[i];
		}
		active = true;
		k = 0;
		untilEvent = 0.0;
		double per = std::max(p.periodMs * 0.001, MIN_INTERVAL);
		return span >= per ? span + space : per;
	}

	Out process(double dt, const Params& p, bool fire) {
		Out o;
		if (fire) untilGroup = start(p);
		else if (!p.external && !active && untilGroup <= 0.0) untilGroup += start(p);
		if (active && untilEvent <= 0.0) {
			o.event = true;
			o.first = (k == 0);
			o.last = (k == cnt - 1);
			o.index = k;
			double next = (k + 1 < cnt) ? gaps[k + 1] : space;
			o.gate = next * clampf(p.length, 1.f, 99.f) * 0.01;
			lastIndex = k;
			k++;
			if (k >= cnt) active = false;
			else untilEvent += next;
		}
		o.count = cnt;
		o.index = lastIndex;
		o.busy = active;
		if (active) untilEvent -= dt;
		if (!p.external) untilGroup -= dt;
		return o;
	}
};

} // namespace fallen
