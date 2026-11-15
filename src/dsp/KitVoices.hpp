// KIT: six drum voices, each synthesised in two characters (EARLY, ANALOG) or played from a WAV.
#pragma once
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
#include "Common.hpp"
#include "Filters.hpp"
#include "../time/Hash.hpp"

namespace fallen {

enum KitVoiceId { KICK = 0, SNARE, CLAP, CHAT, OHAT, PERC, KIT_VOICES };
enum KitSource { SRC_EARLY = 0, SRC_ANALOG = 1, SRC_SAMPLE = 2 };

// Deterministic white noise (xorshift), -1..1.
struct Noise {
	uint32_t s = 0x9E3779B9u;
	explicit Noise(uint32_t seed = 1) : s(hash32(seed) | 1u) {}
	float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float)(int32_t)s * (1.f / 2147483648.f); }
};

// Early-80s sampled-machine character: companded 8-bit and a low sample rate.
struct Crunch {
	Svf pre, post;
	float hold = 0.f, acc = 1.f;
	float sr = 48000.f, amount = -1.f;
	float rateRatio = 1.f, levels = 0.f;
	void set(float a, float s) {
		if (a == amount && s == sr) return;
		amount = a; sr = s;
		a = clampf(a, 0.f, 1.f);
		float target, bits;
		if (a <= 0.5f) { target = 40000.f - 24000.f * a; bits = 12.f - 8.f * a; }  // 0.5 -> 28 kHz, 8 bit
		else { target = 28000.f - 20000.f * (a - 0.5f); bits = 8.f - 4.f * (a - 0.5f); }  // 1 -> 18 kHz, 6 bit
		rateRatio = std::min(1.f, target / sr);
		levels = std::pow(2.f, bits - 1.f);
		pre.set(target * 0.39f, 0.707f, sr);
		post.set(target * 0.43f, 0.707f, sr);
	}
	static float mulaw(float x) { const float mu = 255.f; float s = x < 0 ? -1.f : 1.f; return s * std::log1p(mu * std::fabs(x)) / std::log1p(mu); }
	static float unmulaw(float y) { const float mu = 255.f; float s = y < 0 ? -1.f : 1.f; return s * (std::pow(1.f + mu, std::fabs(y)) - 1.f) / mu; }
	float process(float x) {  // x in roughly -1..1
		if (amount <= 0.f) return x;
		float f = pre.process(x).lp;
		acc += rateRatio;
		if (acc >= 1.f) {
			acc -= 1.f;
			float c = mulaw(clampf(f, -1.f, 1.f));
			c = std::round(c * levels) / levels;
			hold = unmulaw(c);
		}
		return post.process(hold).lp;
	}
	void reset() { pre.reset(); post.reset(); hold = 0.f; acc = 1.f; }
};

// A loaded sample: the original (for re-resampling) and the copy at the engine rate.
struct SampleData {
	std::vector<float> orig;
	float origRate = 48000.f;
	std::vector<float> data;  // at `rate`
	float rate = 48000.f;
	std::string path, name;
	bool truncated = false;
};

struct SamplePlayer {
	double pos = -1.0;
	float env = 1.f, envMul = 1.f, gain = 1.f;
	// the previous hit, fading out over 2 ms
	double tpos = -1.0;
	float tgain = 0.f, tstep = 0.f;
	double tinc = 1.0;
	double inc = 1.0;

	static float hermite(const std::vector<float>& d, double p) {
		long i = (long)p;
		float f = (float)(p - i);
		long n = (long)d.size();
		auto at = [&](long k) { return (k >= 0 && k < n) ? d[k] : 0.f; };
		float xm1 = at(i - 1), x0 = at(i), x1 = at(i + 1), x2 = at(i + 2);
		float c = (x1 - xm1) * 0.5f, v = x0 - x1, w = c + v, a = w + v + (x2 - x0) * 0.5f, b = w + a;
		return ((((a * f) - b) * f + c) * f + x0);
	}
	void trigger(float vel, float tuneSemis, float decaySec, bool full, float sr) {
		if (pos >= 0.0) { tpos = pos; tinc = inc; tgain = gain * env; tstep = tgain / (0.002f * sr); }
		pos = 0.0;
		inc = std::pow(2.0, tuneSemis / 12.0);
		gain = vel;
		env = 1.f;
		envMul = full ? 1.f : std::pow(0.001f, 1.f / std::max(decaySec * sr, 1.f));
	}
	float process(const SampleData* s) {
		if (!s || s->data.empty()) { pos = tpos = -1.0; return 0.f; }
		float y = 0.f;
		long n = (long)s->data.size();
		if (pos >= 0.0) {
			y += hermite(s->data, pos) * gain * env;
			pos += inc;
			env *= envMul;
			if (pos >= n || env < 1e-4f) pos = -1.0;
		}
		if (tpos >= 0.0) {
			y += hermite(s->data, tpos) * tgain;
			tpos += tinc;
			tgain -= tstep;
			if (tgain <= 0.f || tpos >= n) tpos = -1.0;
		}
		return y;
	}
	void stop(float sr) { if (pos >= 0.0) { tpos = pos; tinc = inc; tgain = gain * env; tstep = tgain / (0.02f * sr); pos = -1.0; } }
};

// One synthesised drum voice.
struct DrumSynth {
	int id = KICK;
	bool early = true;
	float sr = 48000.f;
	float t = 1e9f;       // seconds since trigger
	float vel = 1.f, tune = 1.f, dmul = 1.f;
	float ph[6] = {};
	Noise noise;
	Svf f1, f2, f3;
	OnePole hp;
	bool active = false;
	float choke = 1.f, chokeStep = 0.f;
	float snappy = 0.6f;  // ANALOG snare noise balance (menu: 0.3, 0.6, 0.9)
	static constexpr float HAT_F[6] = {205.3f, 304.4f, 369.6f, 522.7f, 540.0f, 800.0f};

	void setup(int voiceId, float s) { id = voiceId; sr = s; noise = Noise(voiceId * 7 + 3); }

	void trigger(float v, float tuneSemis, float decayMul, bool isEarly) {
		vel = v; tune = std::pow(2.f, tuneSemis / 12.f); dmul = decayMul; early = isEarly;
		t = 0.f; active = true; choke = 1.f; chokeStep = 0.f;
		for (float& p : ph) p = 0.f;
		switch (id) {
			case SNARE: f1.set(early ? 3500.f : 1500.f, early ? 0.7f : 0.5f, sr); break;
			case CLAP: f1.set(early ? 1100.f : 1000.f, early ? 1.2f : 1.0f, sr); f2.set(early ? 1100.f : 1000.f, 0.8f, sr); break;
			case CHAT: case OHAT:
				f1.set(7100.f * std::min(tune, 1.5f), 3.f, sr); f2.set(10000.f * std::min(tune, 1.5f), 3.f, sr); hp.set(early ? 7000.f : 6000.f, sr); break;
			case PERC: f1.set(2000.f, 2.f, sr); hp.set(200.f, sr); break;
			default: break;
		}
	}
	void stopChoke() { if (active) chokeStep = 1.f / (0.02f * sr); }

	static float edecay(float t, float T60) { return std::exp(-6.9078f * t / std::max(T60, 1e-4f)); }
	float sine(int k, float f) { ph[k] += f / sr; ph[k] -= std::floor(ph[k]); return std::sin(2.f * PI * ph[k]); }
	float square(int k, float f) { ph[k] += f / sr; ph[k] -= std::floor(ph[k]); return ph[k] < 0.5f ? 1.f : -1.f; }

	float process() {
		if (!active) return 0.f;
		float dt = 1.f / sr;
		float y = 0.f, T;
		switch (id) {
			case KICK:
				if (early) {
					float f = (58.f + (220.f - 58.f) * std::exp(-t / 0.0065f)) * tune;
					T = 0.18f * dmul;
					y = sine(0, f) * edecay(t, T);
					if (t < 0.002f) y += 0.6f * noise.next() * (1.f - t / 0.002f);
				} else {
					float f = 52.f * tune * (1.f + 0.4142f * std::exp(-t / 0.008f));
					T = 0.6f * dmul;
					y = sine(0, f) * edecay(t, T);
					if (t < 0.001f) y += 0.5f * (1.f - t / 0.001f);
				}
				break;
			case SNARE: {
				float tone, nz;
				if (early) {
					T = 0.09f * dmul;
					tone = 0.5f * (sine(0, 185.f * tune) + sine(1, 330.f * tune)) * edecay(t, T);
					nz = f1.process(noise.next()).bp * 1.6f * edecay(t, 0.16f * dmul);
					y = 0.4f * tone + 0.6f * nz;
				} else {
					T = 0.12f * dmul;
					tone = 0.5f * (sine(0, 180.f * tune) + sine(1, 330.f * tune)) * edecay(t, T);
					nz = f1.process(noise.next()).hp * edecay(t, 0.2f * dmul);
					y = (1.f - snappy) * tone + snappy * nz;
				}
				T = 0.2f * dmul;
				break;
			}
			case CLAP: {
				int bursts = early ? 3 : 4;
				float gap = early ? 0.009f : 0.011f;
				float e = 0.f;
				for (int b = 0; b < bursts; b++) {
					float tb = t - b * gap;
					if (tb >= 0.f) e = std::max(e, std::exp(-tb / 0.003f));
				}
				float tailStart = (bursts - 1) * gap;
				float tail = t >= tailStart ? edecay(t - tailStart, (early ? 0.12f : 0.25f) * dmul) : 0.f;
				float n = noise.next();
				y = f1.process(n).bp * 2.f * e + f2.process(n).bp * 1.4f * tail * 0.8f;
				T = tailStart + (early ? 0.12f : 0.25f) * dmul;
				break;
			}
			case CHAT: case OHAT: {
				float metal = 0.f;
				for (int k = 0; k < 6; k++) metal += square(k, HAT_F[k] * tune);
				metal *= 1.f / 6.f;
				float src = early ? 0.5f * metal + 0.5f * noise.next() : metal;
				float b = f1.process(src).bp + f2.process(src).bp;
				T = (id == CHAT ? (early ? 0.045f : 0.05f) : (early ? 0.45f : 0.4f)) * dmul;
				y = hp.hp(b) * (early ? 1.4f : 2.2f) * edecay(t, T);
				break;
			}
			default: {  // PERC
				if (early) {
					float f = 900.f * tune * (1.f + 0.6f * std::exp(-t / 0.005f));
					T = 0.04f * dmul;
					y = sine(0, f) * edecay(t, T);
					if (t < 0.004f) y += 0.5f * f1.process(noise.next()).bp * (1.f - t / 0.004f);
				} else {
					T = 0.03f * dmul;
					y = 0.6f * sine(0, 1700.f * tune) * edecay(t, 0.02f * dmul) + 0.6f * sine(1, 480.f * tune) * edecay(t, T);
					y = hp.hp(y);
				}
				break;
			}
		}
		t += dt;
		if (chokeStep > 0.f) { choke -= chokeStep; if (choke <= 0.f) { choke = 0.f; active = false; } }
		if (t > T * 1.2f + 0.01f) active = false;
		return y * vel * choke;
	}
};

} // namespace fallen
