// FM4 tables and curves. Values marked (est.) are estimates to check against the 4-op owner's manuals.
#pragma once
#include <cmath>
#include <cstdint>
#include "Common.hpp"

namespace fallen {
namespace fm4 {

// Frequency ratio for CRS 0..63 (the 4-op ratio table).
static constexpr float RATIO[64] = {
	0.50f, 0.71f, 0.78f, 0.87f, 1.00f, 1.41f, 1.57f, 1.73f, 2.00f, 2.82f, 3.00f, 3.14f, 3.46f, 4.00f, 4.24f, 4.71f,
	5.00f, 5.19f, 5.65f, 6.00f, 6.28f, 6.92f, 7.00f, 7.07f, 7.85f, 8.00f, 8.48f, 8.65f, 9.00f, 9.42f, 9.89f, 10.00f,
	10.38f, 10.99f, 11.00f, 11.30f, 12.00f, 12.11f, 12.56f, 12.72f, 13.00f, 13.84f, 14.00f, 14.10f, 14.13f, 15.00f, 15.55f, 15.57f,
	15.70f, 16.96f, 17.27f, 17.30f, 18.37f, 18.84f, 19.03f, 19.78f, 20.41f, 20.76f, 21.20f, 21.98f, 22.49f, 23.55f, 24.22f, 25.95f};

// Algorithms. MOD[a][op] = bitmask of operators (bit i = op i+1) feeding operator op+1. CAR[a] = carrier mask.
// Every algorithm feeds from higher-numbered to lower-numbered operators, so computing op4, op3, op2, op1 in order works.
static constexpr uint8_t MOD[8][4] = {
	{0b0010, 0b0100, 0b1000, 0},       // 1: 4>3>2>1
	{0b0010, 0b1100, 0, 0},            // 2: (4+3)>2>1
	{0b1010, 0b0100, 0, 0},            // 3: 4>1, 3>2>1
	{0b0110, 0, 0b1000, 0},            // 4: 4>3>1, 2>1
	{0b0010, 0, 0b1000, 0},            // 5: 4>3, 2>1
	{0b1000, 0b1000, 0b1000, 0},       // 6: 4>3, 4>2, 4>1
	{0, 0, 0b1000, 0},                 // 7: 4>3, plus 2 and 1
	{0, 0, 0, 0},                      // 8: all carriers
};
static constexpr uint8_t CAR[8] = {0b0001, 0b0001, 0b0001, 0b0001, 0b0101, 0b0111, 0b0111, 0b1111};
static constexpr const char* ALG_TEXT[8] = {"4>3>2>1", "4+3>2>1", "4>1 3>2>1", "4>3>1 2>1", "4>3 2>1", "4>3,2,1", "4>3 +2 +1", "4+3+2+1"};

inline bool isCarrier(int alg, int op) { return (CAR[clampi(alg, 0, 7)] >> op) & 1; }

// Feedback scale in cycles (radians / 2pi): 0, pi/16 ... 4pi (est.)
static constexpr float FB_CYCLES[8] = {0.f, 1.f / 32.f, 1.f / 16.f, 1.f / 8.f, 1.f / 4.f, 1.f / 2.f, 1.f, 2.f};

// A modulator at full level gives this peak phase deviation in cycles (4pi rad) (est.).
constexpr float MOD_DEPTH = 2.f;

// Envelope rates (est.). Full-scale decay (96 dB) time for rate r >= 1.
inline float decayTime(float r) { return 38.f * std::pow(2.f, -(r - 1.f) / 2.35f); }
// Attack time from silence to full for rate r >= 1.
inline float attackTime(float r) { return 9.f * std::pow(2.f, -(r - 1.f) / 2.1f); }
// Output level 0..99 to attenuation in dB.
inline float levelDb(float out) { return (99.f - clampf(out, 0.f, 99.f)) * 0.75f; }
// D1L 0..15 (15 = full) to attenuation in dB (3 dB per step, est.).
inline float d1lDb(int d1l) { return (15 - clampi(d1l, 0, 15)) * 3.f; }

// LFO
inline float lfoHz(int lfs) { return 0.06f * std::pow(50.f / 0.06f, clampi(lfs, 0, 99) / 99.f); }
inline float lfoDelay(int lfd) { return 5.f * clampi(lfd, 0, 99) / 99.f; }
static constexpr float PMS_CENTS[8] = {0.f, 5.f, 10.f, 20.f, 50.f, 100.f, 400.f, 1200.f};
static constexpr float AMS_DB[4] = {0.f, 6.f, 12.f, 24.f};
static constexpr const char* LFW_NAMES[4] = {"SAW", "SQUARE", "TRIANGLE", "S&H"};

// Waveforms W1..W8 (TX81Z-style; W2/W4/W6/W8 shapes est.).
constexpr int WT_SIZE = 4096;
struct Waves {
	float t[8][WT_SIZE + 1];
	static float w2(double th) {  // sign(sin)*sin^2: the peaky "bent" sine
		double s = std::sin(th);
		return (float)(s * std::fabs(s));
	}
	Waves() {
		for (int i = 0; i <= WT_SIZE; i++) {
			double th = 2.0 * M_PI * (double)(i % WT_SIZE) / WT_SIZE;
			double s = std::sin(th);
			bool firstHalf = th < M_PI;
			t[0][i] = (float)s;
			t[1][i] = w2(th);
			t[2][i] = (float)std::max(s, 0.0);
			t[3][i] = std::max(w2(th), 0.f);
			t[4][i] = firstHalf ? (float)std::sin(2 * th) : 0.f;
			t[5][i] = firstHalf ? w2(2 * th) : 0.f;
			t[6][i] = firstHalf ? (float)std::fabs(std::sin(2 * th)) : 0.f;
			t[7][i] = firstHalf ? std::fabs(w2(2 * th)) : 0.f;
		}
	}
	// phase in cycles (any value)
	inline float lookup(int w, float ph) const {
		int ip = (int)ph;
		ph -= (float)ip;
		if (ph < 0.f) ph += 1.f;
		float x = ph * WT_SIZE;
		int i = (int)x;
		if (i >= WT_SIZE) i = WT_SIZE - 1;
		float f = x - i;
		const float* T = t[w & 7];
		return T[i] + (T[i + 1] - T[i]) * f;
	}
};
inline const Waves& waves() {
	static const Waves w;
	return w;
}

} // namespace fm4
} // namespace fallen
