// DURA: a loop of up to 16 steps, each a time interval with a repeat count.
#pragma once
#include <cstdint>
#include "Clock.hpp"
#include "Hash.hpp"

namespace fallen {

struct Dura {
	static constexpr int N = 16;
	enum Mode { MS = 0, UNITS = 1, RATIO = 2 };
	static constexpr int NRATIOS = 14;
	static constexpr float RATIOS[NRATIOS] = {4.f, 3.f, 2.f, 1.5f, 4.f / 3.f, 1.f, 0.75f, 2.f / 3.f, 0.5f, 0.375f, 1.f / 3.f, 0.25f, 1.f / 6.f, 0.125f};
	static constexpr const char* RATIO_NAMES[NRATIOS] = {"4", "3", "2", "3/2", "4/3", "1", "3/4", "2/3", "1/2", "3/8", "1/3", "1/4", "1/6", "1/8"};

	struct Params {
		int mode = UNITS;
		float unitMs = 40.f, baseMs = 500.f;
		float stretch = 1.f;     // already includes CV, clamped 1/16..16
		int steps = 8;
		float nudge = 0.f;       // -0.5..0.5
		float length = 50.f;     // percent
		float spread = 0.f;      // 0..1
		int seed = 0;
		bool evolve = false;
		double extPeriod = 0.0;  // seconds, 0 = no external clock
		float lenMs[N];
		int units[N];
		int ratioIdx[N];
		int rep[N];
		bool on[N];
		Params() {
			for (int i = 0; i < N; i++) { lenMs[i] = 125.f; units[i] = 1; ratioIdx[i] = 5; rep[i] = 1; on[i] = true; }
		}
	};

	struct Out {
		bool event = false;     // an event time arrived (also for rests)
		bool trig = false;      // event on an ON step
		bool eoc = false;       // loop wrapped
		double interval = 0.0;  // seconds from this event to the next
		double gate = 0.0;      // gate length (seconds) if trig
		int step = 0, rep = 0;
	};

	double untilNext = 0.0;
	int step = 0, rep = 0;
	uint32_t loop = 0;
	bool first = true;
	// last event info, held for outputs
	int curStep = 0, curRep = 0;
	double curInterval = 0.1;

	void reset() {
		untilNext = 0.0;
		step = 0; rep = 0; loop = 0;
		first = true;
		curStep = 0; curRep = 0;
	}

	// Interval of step i in seconds, before NUDGE, after STRETCH.
	static double stepInterval(const Params& p, int i) {
		double ms;
		switch (p.mode) {
			case MS: ms = p.lenMs[i] * (p.extPeriod > 0.0 ? p.extPeriod / 0.1 : 1.0); break;
			case RATIO: ms = RATIOS[clampi(p.ratioIdx[i], 0, NRATIOS - 1)] * (p.extPeriod > 0.0 ? p.extPeriod * 1000.0 : (double)p.baseMs); break;
			default: ms = clampi(p.units[i], 1, 16) * (p.extPeriod > 0.0 ? p.extPeriod * 1000.0 : (double)p.unitMs); break;
		}
		return std::max(ms * 0.001 * p.stretch, MIN_INTERVAL);
	}
	static int reps(const Params& p, int i) { return clampi(p.rep[i], 1, 16); }

	// Duration of the whole loop (for the display).
	static double loopLength(const Params& p) {
		int n = clampi(p.steps, 1, N);
		double t = 0;
		for (int i = 0; i < n; i++) t += stepInterval(p, i) * reps(p, i);
		t += stepInterval(p, n - 1) * p.nudge;
		return t;
	}

	float gatePercent(const Params& p, int s, int r) const {
		uint32_t b = p.evolve ? loop + 1 : 0;
		float h = hash01((uint32_t)p.seed * 7919u + 17u, (uint32_t)(s * 16 + r), b);
		return clampf(p.length + p.spread * (h - 0.5f) * 2.f * 98.f, 1.f, 99.f);
	}

	// Advance one sample of length dt. Event times are exact to the sample (no drift).
	Out process(double dt, const Params& p) {
		Out o;
		int n = clampi(p.steps, 1, N);
		if (untilNext <= 0.0) {
			if (step >= n) { step = 0; rep = 0; }
			int s = step;
			if (rep >= reps(p, s)) rep = 0;
			double I = stepInterval(p, s);
			bool lastOfLoop = (s == n - 1) && (rep == reps(p, s) - 1);
			if (lastOfLoop) I = std::max(I * (1.0 + clampf(p.nudge, -0.5f, 0.5f)), MIN_INTERVAL);
			o.event = true;
			o.trig = p.on[s];
			o.eoc = (s == 0 && rep == 0 && !first);
			o.interval = I;
			o.gate = I * gatePercent(p, s, rep) * 0.01;
			o.step = s; o.rep = rep;
			curStep = s; curRep = rep; curInterval = I;
			first = false;
			untilNext += I;
			if (untilNext < 0.0) untilNext = 0.0;  // can only happen after a huge dt
			rep++;
			if (rep >= reps(p, s)) {
				rep = 0;
				step++;
				if (step >= n) { step = 0; loop++; }
			}
		}
		o.step = curStep; o.rep = curRep; o.interval = o.event ? o.interval : curInterval;
		untilNext -= dt;
		return o;
	}
};

} // namespace fallen
