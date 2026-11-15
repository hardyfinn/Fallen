// Small filters: TPT state-variable filter, one-pole, DC blocker.
#pragma once
#include <cmath>
#include "Common.hpp"

namespace fallen {

struct Svf {
	float ic1 = 0.f, ic2 = 0.f;
	float g = 0.f, k = 1.f, a1 = 0.f, a2 = 0.f, a3 = 0.f;
	void set(float fc, float q, float sr) {
		fc = clampf(fc, 5.f, 0.49f * sr);
		g = std::tan(PI * fc / sr);
		k = 1.f / std::max(q, 0.05f);
		a1 = 1.f / (1.f + g * (g + k));
		a2 = g * a1;
		a3 = g * a2;
	}
	struct Out { float lp, bp, hp; };
	Out process(float x) {
		float v3 = x - ic2;
		float v1 = a1 * ic1 + a2 * v3;
		float v2 = ic2 + a2 * ic1 + a3 * v3;
		ic1 = 2.f * v1 - ic1;
		ic2 = 2.f * v2 - ic2;
		return {v2, v1, x - k * v1 - v2};
	}
	void reset() { ic1 = ic2 = 0.f; }
};

struct OnePole {
	float y = 0.f, a = 0.f;
	void set(float fc, float sr) { a = 1.f - std::exp(-2.f * PI * clampf(fc, 1.f, 0.49f * sr) / sr); }
	float lp(float x) { y += a * (x - y); return y; }
	float hp(float x) { return x - lp(x); }
	void reset() { y = 0.f; }
};

struct DcBlock {
	float x1 = 0.f, y1 = 0.f, r = 0.995f;
	void set(float sr) { r = 1.f - 2.f * PI * 10.f / sr; }
	float process(float x) { float y = x - x1 + r * y1; x1 = x; y1 = y; return y; }
};

} // namespace fallen
