// CYCLE: four lanes of values with their own lengths and dividers, all advanced by one trigger.
#pragma once
#include <cmath>
#include <numeric>
#include "Clock.hpp"

namespace fallen {

struct Cycle {
	static constexpr int LANES = 4, N = 16;
	enum Mode { UNI = 0, BI = 1, PITCH = 2, VOICE = 3 };
	float val[LANES][N];
	int pos[LANES] = {}, cnt[LANES] = {};
	bool fresh = true;
	long long trigs = 0;

	Cycle() {
		for (int l = 0; l < LANES; l++)
			for (int i = 0; i < N; i++) val[l][i] = 0.5f;
	}

	void reset() {
		for (int l = 0; l < LANES; l++) { pos[l] = 0; cnt[l] = 0; }
		fresh = true;
		trigs = 0;
	}

	void trigger(const int* len, const int* div) {
		trigs++;
		if (fresh) { fresh = false; return; }  // the first trigger after reset plays step 1
		for (int l = 0; l < LANES; l++) {
			int L = clampi(len[l], 1, N), D = clampi(div[l], 1, 8);
			if (++cnt[l] >= D) {
				cnt[l] = 0;
				pos[l] = (pos[l] + 1) % L;
			}
			if (pos[l] >= L) pos[l] = 0;
		}
	}

	// Output voltage of a lane. offset is in the knob's normalised units (-1..1 of the full range).
	static float toVolts(float v, int mode, float offset) {
		switch (mode) {
			case BI: return clampf((v + offset) * 10.f - 5.f, -5.f, 5.f);
			case PITCH: return std::round(clampf(v + offset, 0.f, 1.f) * 48.f - 24.f) / 12.f;
			case VOICE: return (float)clampi((int)std::lround(clampf(v + offset, 0.f, 1.f) * 31.f), 0, 31) * 0.1f;
			default: return clampf((v + offset) * 10.f, 0.f, 10.f);
		}
	}
	float out(int l, int mode, float offset) const { return toVolts(val[l][pos[l]], mode, offset); }

	static long long lcm(long long a, long long b) { return a / std::gcd(a, b) * b; }
	// Triggers until every lane is back at step 1 together.
	long long realignIn(const int* len, const int* div) const {
		long long L = 1;
		for (int l = 0; l < LANES; l++) L = lcm(L, (long long)clampi(len[l], 1, N) * clampi(div[l], 1, 8));
		if (trigs == 0) return 0;
		long long done = (trigs - 1) % L;
		return (L - done) % L;
	}
};

} // namespace fallen
