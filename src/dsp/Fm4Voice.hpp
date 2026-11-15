// FM4: the 4-operator voice data model and the polyphonic engine.
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include "Common.hpp"
#include "Fm4Tables.hpp"
#include "../time/Hash.hpp"

namespace fallen {

// One operator, in the original integer ranges.
struct Op4 {
	uint8_t ar = 31, d1r = 0, d2r = 0, rr = 7, d1l = 15, ls = 0, rs = 0, ebs = 0, ame = 0, kvs = 0, out = 0, crs = 4, det = 3;
	// extended (TX81Z-style) data
	uint8_t fix = 0, fixrg = 0, fine = 0, osw = 0, shft = 0;
	bool operator==(const Op4& o) const { return std::memcmp(this, &o, sizeof(Op4)) == 0; }
};

// One voice. op[0] is operator 1 (always a carrier), op[3] is operator 4 (has feedback).
struct Voice4op {
	Op4 op[4];
	uint8_t alg = 0, fb = 0;  // alg 0..7 = ALG 1..8
	uint8_t lfs = 35, lfd = 0, pmd = 0, amd = 0, sy = 1, lfw = 2, pms = 0, ams = 0;
	uint8_t trps = 24, pbr = 4, mono = 0, porm = 0, port = 0, fcvol = 40, sus = 0, porta = 0, chorus = 0;
	uint8_t mwp = 50, mwa = 0, bcp = 0, bca = 0, bcpb = 50, bceb = 0;
	uint8_t peg[6] = {99, 99, 99, 50, 50, 50};
	uint8_t rev = 0, fcpitch = 0, fcamp = 0;
	char name[10] = {'I', 'N', 'I', 'T', ' ', ' ', ' ', ' ', ' ', ' '};
	bool extended = false;

	Voice4op() {
		op[0].out = 99;
	}
	std::string nameStr() const {
		std::string s(name, 10);
		for (char& c : s) if (c < 32 || c > 126) c = ' ';
		while (!s.empty() && s.back() == ' ') s.pop_back();
		return s;
	}
	void setName(const std::string& s) {
		for (int i = 0; i < 10; i++) name[i] = i < (int)s.size() ? s[i] : ' ';
	}
	bool operator==(const Voice4op& o) const {
		for (int i = 0; i < 4; i++) if (!(op[i] == o.op[i])) return false;
		return alg == o.alg && fb == o.fb && lfs == o.lfs && lfd == o.lfd && pmd == o.pmd && amd == o.amd && sy == o.sy && lfw == o.lfw &&
		       pms == o.pms && ams == o.ams && trps == o.trps && pbr == o.pbr && mono == o.mono && porm == o.porm && port == o.port &&
		       fcvol == o.fcvol && sus == o.sus && porta == o.porta && chorus == o.chorus && mwp == o.mwp && mwa == o.mwa && bcp == o.bcp &&
		       bca == o.bca && bcpb == o.bcpb && bceb == o.bceb && std::memcmp(peg, o.peg, 6) == 0 && rev == o.rev && fcpitch == o.fcpitch &&
		       fcamp == o.fcamp && std::memcmp(name, o.name, 10) == 0 && extended == o.extended;
	}
};

struct Bank4op {
	Voice4op v[32];
};

// Panel macros: offsets applied on top of the stored voice (0 = no change).
struct Fm4Macros {
	float bright = 0.f;  // modulator output level steps, ±20
	float ratio = 0.f;   // modulator CRS steps, ±8
	float decay = 0.f;   // rate steps, + = longer, ±10
	float attack = 0.f;  // rate steps, + = slower, ±10
	float fdbk = 0.f;    // ±7
};

// Halfband-style 2x decimator (31-tap windowed sinc).
struct Decim2 {
	static constexpr int T = 31;
	float h[T];
	float buf[64] = {};
	int w = 0;
	Decim2() {
		double sum = 0;
		for (int i = 0; i < T; i++) {
			double n = i - (T - 1) / 2.0;
			double x = n * 0.5 * 0.9;  // cutoff just under the output Nyquist
			double s = n == 0 ? 1.0 : std::sin(M_PI * x) / (M_PI * x);
			double win = 0.42 - 0.5 * std::cos(2 * M_PI * i / (T - 1)) + 0.08 * std::cos(4 * M_PI * i / (T - 1));
			h[i] = (float)(s * win);
			sum += h[i];
		}
		for (float& v : h) v = (float)(v / sum);
	}
	void reset() { std::memset(buf, 0, sizeof(buf)); }
	float process(float a, float b) {
		buf[w] = buf[w + 32] = a; w = (w + 1) & 31;
		buf[w] = buf[w + 32] = b; w = (w + 1) & 31;
		float y = 0.f;
		const float* x = buf + w;  // x[0] is the oldest of the last 32
		for (int i = 0; i < T; i++) y += h[i] * x[32 - T + i];
		return y;
	}
};

struct Fm4Engine {
	static constexpr int MAXV = 8;
	enum Stage { IDLE, ATTACK, D1, D2, REL };

	struct Prepared {
		bool fixed[4];
		float ratio[4], fixedHz[4], detune[4];
		int wave[4];
		float gain[4];         // level, velocity and level scaling combined
		float atkK[4], f1[4], f2[4], fr[4], d1lAmp[4];
		bool ame[4];
		int alg = 0;
		float fbCycles = 0.f, carScale = 1.f;
		float transpose = 0.f;
		float pmsCents = 0.f, amsDb = 0.f, pmd = 0.f, amd = 0.f, lfoDelay = 0.f;
	};
	struct Chan {
		bool gate = false, active = false;
		float pitch = 0.f, vel = 1.f;
		int voice = 0;
		Prepared pr;
		float phase[4] = {}, amp[4] = {};
		int stage[4] = {};
		float fb1 = 0.f, fb2 = 0.f;
		float sinceOn = 0.f;
		int prepCount = 0;
		Decim2 dec;
	};

	Chan ch[MAXV];
	const Bank4op* bank = nullptr;
	float sr = 48000.f;
	// global LFO (shared by all voices, as on the hardware)
	float lfoPhase = 0.f, lfoSH = 0.f;
	uint32_t lfoCount = 0;
	int lfoVoice = 0;

	void setSampleRate(float s) { sr = s; for (auto& c : ch) c.prepCount = 0; }
	void reset() {
		for (auto& c : ch) { c = Chan(); }
		lfoPhase = 0.f;
	}

	static float rateFactor(float rate, float dt) {
		if (rate <= 0.f) return 1.f;
		float dbps = 96.f / fm4::decayTime(std::min(rate, 31.f));
		return std::pow(10.f, -dbps * dt / 20.f);
	}

	void prepare(Chan& c, const Fm4Macros& m) {
		const Voice4op& v = bank->v[clampi(c.voice, 0, 31)];
		Prepared& p = c.pr;
		float dt = 1.f / sr;
		int alg = clampi(v.alg, 0, 7);
		p.alg = alg;
		int nc = 0;
		float midi = 60.f + 12.f * c.pitch;
		float keyRate = std::max(0.f, midi - 24.f) / 24.f;
		for (int i = 0; i < 4; i++) {
			const Op4& o = v.op[i];
			bool car = fm4::isCarrier(alg, i);
			if (car) nc++;
			float out = o.out, crs = o.crs;
			if (!car) {
				out = clampf(out + m.bright, 0.f, 99.f);
				if (!o.fix) crs = clampf(std::round(crs + m.ratio), 0.f, 63.f);
			}
			p.fixed[i] = v.extended && o.fix;
			p.ratio[i] = fm4::RATIO[(int)crs] * (v.extended ? 1.f + o.fine / 16.f : 1.f);
			p.fixedHz[i] = std::max(1.f, (float)(16 * (o.crs >> 2) + o.fine) * (float)(1 << clampi(o.fixrg, 0, 7)));
			p.detune[i] = std::pow(2.f, (clampi(o.det, 0, 6) - 3) * 2.6f / 1200.f);
			p.wave[i] = v.extended ? clampi(o.osw, 0, 7) : 0;
			float att = fm4::levelDb(out);
			att += clampi(o.kvs, 0, 7) / 7.f * 24.f * (1.f - clampf(c.vel, 0.f, 1.f));
			att += clampi(o.ls, 0, 99) / 99.f * 24.f * std::max(0.f, (midi - 48.f) / 12.f);
			p.gain[i] = out <= 0.f ? 0.f : std::pow(10.f, -att / 20.f);
			float rsAdd = clampi(o.rs, 0, 3) * keyRate;
			float ar = o.ar > 0 ? clampf(o.ar - m.attack + rsAdd, 1.f, 31.f) : 0.f;
			float d1r = o.d1r > 0 ? clampf(o.d1r - m.decay + rsAdd, 1.f, 31.f) : 0.f;
			float d2r = o.d2r > 0 ? clampf(o.d2r - m.decay + rsAdd, 1.f, 31.f) : 0.f;
			float rr = clampf(clampi(o.rr, 1, 15) * 2.f + 1.f + rsAdd, 1.f, 31.f);
			p.atkK[i] = ar > 0.f ? 1.f - std::exp(-dt * 3.0445f / fm4::attackTime(ar)) : 0.f;
			p.f1[i] = rateFactor(d1r, dt);
			p.f2[i] = rateFactor(d2r, dt);
			p.fr[i] = rateFactor(rr, dt);
			p.d1lAmp[i] = std::pow(10.f, -fm4::d1lDb(o.d1l) / 20.f);
			p.ame[i] = o.ame != 0;
		}
		p.fbCycles = fm4::FB_CYCLES[clampi((int)std::round(v.fb + m.fdbk), 0, 7)];
		p.carScale = 1.f / (1.f + 0.5f * (nc - 1));
		p.transpose = (clampi(v.trps, 0, 48) - 24) / 12.f;
		p.pmsCents = fm4::PMS_CENTS[clampi(v.pms, 0, 7)];
		p.amsDb = fm4::AMS_DB[clampi(v.ams, 0, 3)];
		p.pmd = clampi(v.pmd, 0, 99) / 99.f;
		p.amd = clampi(v.amd, 0, 99) / 99.f;
		p.lfoDelay = fm4::lfoDelay(v.lfd);
	}

	void noteOn(int i, int voice, float pitch, float vel, const Fm4Macros& m) {
		Chan& c = ch[i];
		c.voice = clampi(voice, 0, 31);
		c.pitch = pitch;
		c.vel = vel;
		c.gate = true;
		c.active = true;
		c.sinceOn = 0.f;
		prepare(c, m);
		c.prepCount = 32;
		for (int k = 0; k < 4; k++) { c.stage[k] = ATTACK; c.phase[k] = 0.f; }
		c.fb1 = c.fb2 = 0.f;
		const Voice4op& v = bank->v[c.voice];
		lfoVoice = c.voice;
		if (v.sy) lfoPhase = 0.f;
	}
	void noteOff(int i) {
		Chan& c = ch[i];
		c.gate = false;
		for (int k = 0; k < 4; k++) if (c.stage[k] != IDLE) c.stage[k] = REL;
	}

	float lfoValue(int w) {
		float ph = lfoPhase;
		switch (w & 3) {
			case 0: return 2.f * ph - 1.f;
			case 1: return ph < 0.5f ? 1.f : -1.f;
			case 2: return ph < 0.5f ? 4.f * ph - 1.f : 3.f - 4.f * ph;
			default: return lfoSH;
		}
	}

	// One sample. pitch per channel may change while held (glides from cables). Returns per-channel volts in out[].
	void process(int nch, const float* pitch, const Fm4Macros& m, const float* spreadCents, float* out) {
		const Voice4op& lv = bank->v[clampi(lfoVoice, 0, 31)];
		float dt = 1.f / sr;
		lfoPhase += fm4::lfoHz(lv.lfs) * dt;
		if (lfoPhase >= 1.f) {
			lfoPhase -= std::floor(lfoPhase);
			lfoCount++;
			lfoSH = hash01(0x5eed, lfoCount) * 2.f - 1.f;
		}
		float lfo = lfoValue(lv.lfw);
		const fm4::Waves& W = fm4::waves();
		float hdt = 0.5f / sr;
		for (int i = 0; i < MAXV; i++) {
			Chan& c = ch[i];
			out[i] = 0.f;
			if (!c.active) continue;
			if (i < nch) c.pitch = pitch[i];
			if (--c.prepCount <= 0) { prepare(c, m); c.prepCount = 32; }
			Prepared& p = c.pr;
			c.sinceOn += dt;
			float ramp = p.lfoDelay > 0.f ? clampf(c.sinceOn / p.lfoDelay, 0.f, 1.f) : 1.f;
			float pmCents = lfo * p.pmd * p.pmsCents * ramp;
			float amDb = (0.5f - 0.5f * lfo) * p.amd * p.amsDb * ramp;
			float amGain = amDb > 0.f ? std::pow(10.f, -amDb / 20.f) : 1.f;
			float base = C4 * std::pow(2.f, c.pitch + p.transpose + (pmCents + (i < MAXV ? spreadCents[i] : 0.f)) / 1200.f);
			float inc[4], a[4];
			bool alive = false;
			for (int k = 0; k < 4; k++) {
				// envelope at 1x
				float& e = c.amp[k];
				switch (c.stage[k]) {
					case ATTACK:
						if (p.atkK[k] <= 0.f) break;
						e += (1.05f - e) * p.atkK[k];
						if (e >= 1.f) { e = 1.f; c.stage[k] = D1; }
						break;
					case D1:
						if (e <= p.d1lAmp[k]) { c.stage[k] = D2; break; }
						e *= p.f1[k];
						if (e <= p.d1lAmp[k]) { e = p.d1lAmp[k]; c.stage[k] = D2; }
						break;
					case D2: e *= p.f2[k]; break;
					case REL:
						e *= p.fr[k];
						if (e < 1e-5f) { e = 0.f; c.stage[k] = IDLE; }
						break;
					default: e = 0.f; break;
				}
				if (e < 1e-6f && c.stage[k] == D2) e = 0.f;
				a[k] = e * p.gain[k] * (p.ame[k] ? amGain : 1.f);
				float f = p.fixed[k] ? p.fixedHz[k] : base * p.ratio[k] * p.detune[k];
				inc[k] = std::min(f * hdt, 0.5f);
				if (fm4::isCarrier(p.alg, k) && (c.stage[k] != IDLE && c.stage[k] != ATTACK ? e > 1e-5f : c.stage[k] == ATTACK)) alive = true;
			}
			if (!alive && !c.gate) { c.active = false; c.dec.reset(); continue; }
			const uint8_t* mod = fm4::MOD[p.alg];
			uint8_t car = fm4::CAR[p.alg];
			float y2[2];
			for (int s = 0; s < 2; s++) {
				float y[4];
				float fbm = p.fbCycles * 0.5f * (c.fb1 + c.fb2);
				y[3] = W.lookup(p.wave[3], c.phase[3] + fbm) * a[3];
				c.fb2 = c.fb1; c.fb1 = y[3];
				for (int k = 2; k >= 0; k--) {
					float mm = 0.f;
					uint8_t msk = mod[k];
					for (int j = k + 1; j < 4; j++) if (msk & (1 << j)) mm += y[j];
					y[k] = W.lookup(p.wave[k], c.phase[k] + mm * fm4::MOD_DEPTH) * a[k];
				}
				float o = 0.f;
				for (int k = 0; k < 4; k++) {
					if (car & (1 << k)) o += y[k];
					c.phase[k] += inc[k];
					if (c.phase[k] >= 1.f) c.phase[k] -= 1.f;
				}
				y2[s] = o * p.carScale;
			}
			float v = c.dec.process(y2[0], y2[1]) * 5.f;
			out[i] = std::isfinite(v) ? v : 0.f;
			if (!std::isfinite(v)) { c = Chan(); }
		}
	}
};

} // namespace fallen
