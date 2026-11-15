// SPECTRA: up to 32 sine partials per voice, stretched, tilted and swept by a moving window.
#pragma once
#include <cmath>
#include "Common.hpp"
#include "../time/Hash.hpp"

namespace fallen {

struct SineTable {
	static constexpr int N = 2048;
	float t[N + 1];
	SineTable() { for (int i = 0; i <= N; i++) t[i] = (float)std::sin(2.0 * M_PI * i / N); }
	inline float at(float ph) const {  // ph in [0,1)
		float x = ph * N;
		int i = (int)x;
		float f = x - i;
		return t[i] + (t[i + 1] - t[i]) * f;
	}
};
inline const SineTable& sineTable() { static const SineTable s; return s; }

struct Additive {
	static constexpr int MAXP = 32;
	struct Params {
		int partials = 16;
		float stretch = 0.f;  // -1..1
		float tilt = -6.f;    // dB/oct
		float oddEven = 0.f;  // -1 odd only .. +1 even only
		float rate = 0.1f;    // Hz of the MOVE window
		float depth = 0.f;    // 0..1
		float detune = 0.f;   // cents
		float width = 0.f;    // 0..1
		float attack = 0.01f, release = 0.5f;
	};
	float phase[MAXP] = {};
	float inc[MAXP] = {};
	float ampL[MAXP] = {}, ampR[MAXP] = {};
	float tgtL[MAXP] = {}, tgtR[MAXP] = {};
	float env = 0.f;
	int count = 0;
	float sr = 48000.f;

	void reset() { for (int i = 0; i < MAXP; i++) { phase[i] = hash01(3, i); ampL[i] = ampR[i] = 0.f; } env = 0.f; }

	static float movePos(float movePhase) {  // triangle 0..1
		float f = movePhase - std::floor(movePhase);
		return f < 0.5f ? 2.f * f : 2.f - 2.f * f;
	}

	// Recompute partial frequencies and amplitudes (control rate).
	void update(float f0, const Params& p, float movePhase) {
		count = clampi(p.partials, 1, MAXP);
		float nyq = 0.45f * sr;
		float centre = 1.f + movePos(movePhase) * (count - 1);
		float total = 0.f;
		float raw[MAXP];
		for (int n = 1; n <= MAXP; n++) {
			int i = n - 1;
			if (n > count) { raw[i] = 0.f; inc[i] = 0.f; continue; }
			float f = f0 * std::pow((float)n, 1.f + 0.5f * clampf(p.stretch, -1.f, 1.f));
			f *= fastExp2(p.detune * (hash01(11, n) * 2.f - 1.f) / 1200.f);
			inc[i] = f / sr;
			float a = std::pow((float)n, p.tilt / 6.0206f);
			bool odd = (n % 2) == 1;
			if (p.oddEven < 0.f && !odd) a *= 1.f + p.oddEven;
			if (p.oddEven > 0.f && odd && n > 1) a *= 1.f - p.oddEven;
			float d = std::fabs((float)n - centre);
			float win = d < 3.f ? 0.5f + 0.5f * std::cos(PI * d / 3.f) : 0.f;
			a *= (1.f - p.depth) + p.depth * win;
			if (f >= nyq || f <= 0.f) a = 0.f;
			raw[i] = a;
			total += a;
		}
		float norm = total > 1e-6f ? 1.f / std::max(total, 1.f) : 0.f;
		for (int i = 0; i < MAXP; i++) {
			float a = raw[i] * norm;
			float pan = (i % 2 == 0) ? -p.width : p.width;  // partial 1 (i=0) leans left
			tgtL[i] = a * (1.f - std::max(pan, 0.f));
			tgtR[i] = a * (1.f + std::min(pan, 0.f));
		}
	}

	void process(bool gate, const Params& p, float& L, float& R) {
		float dt = 1.f / sr;
		if (gate) env += (1.f - env) * std::min(1.f, dt / std::max(p.attack, 1e-3f) * 3.f);
		else env -= env * std::min(1.f, dt / std::max(p.release, 1e-3f) * 3.f);
		if (env < 1e-6f && !gate) { env = 0.f; L = R = 0.f; return; }
		const SineTable& S = sineTable();
		float l = 0.f, r = 0.f;
		const float k = 0.002f;
		for (int i = 0; i < count; i++) {
			ampL[i] += (tgtL[i] - ampL[i]) * k;
			ampR[i] += (tgtR[i] - ampR[i]) * k;
			phase[i] += inc[i];
			if (phase[i] >= 1.f) phase[i] -= 1.f;
			float s = S.at(phase[i]);
			l += s * ampL[i];
			r += s * ampR[i];
		}
		L = l * env * 5.f;
		R = r * env * 5.f;
	}
};

} // namespace fallen
