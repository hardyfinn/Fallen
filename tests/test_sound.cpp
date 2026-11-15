#include "util.hpp"
#include "../src/dsp/Chords.hpp"
#include "../src/dsp/Additive.hpp"
#include "../src/dsp/KitVoices.hpp"
#include "../src/io/WavLoad.hpp"
#include <cstdio>
#include <cstring>

using namespace fallen;

TEST("CHORD notes, inversion, voicing") {
	uint32_t s[8]; Chords::factory(s);
	float n[6];
	int c = Chords::notes(s[0], 0, Chords::CLOSE, n);
	CHECK(c == 4 && n[0] == 0 && n[1] == 3 && n[2] == 7 && n[3] == 10);
	c = Chords::notes(s[0], 1, Chords::CLOSE, n);
	CHECK(c == 4 && n[0] == 3 && n[3] == 12);
	c = Chords::notes(s[0], 0, Chords::DROP2, n);  // 7 drops to -5
	CHECK(c == 4 && n[0] == -5 && n[1] == 0 && n[3] == 10);
	c = Chords::notes(s[2], 0, Chords::SPREAD, n);
	CHECK(c == 6 && n[5] == 29 && n[4] == 22);
}

TEST("SPECTRA partial frequencies, nyquist, cost") {
	Additive a; a.sr = 48000; a.reset();
	Additive::Params p; p.partials = 8; p.stretch = 0.f;
	a.update(100.f, p, 0.f);
	CHECK_NEAR(a.inc[2] * 48000, 300.0, 1e-2);
	p.stretch = 1.f; a.update(100.f, p, 0.f);
	CHECK_NEAR(a.inc[3] * 48000, 800.0, 0.05);
	p.partials = 32; p.stretch = 0.f; a.update(2000.f, p, 0.f);
	CHECK(a.tgtL[20] == 0.f);  // 21 * 2 kHz is above 0.45 fs
	CHECK(a.tgtL[5] > 0.f);
	std::vector<float> y; float L, R;
	for (int i = 0; i < 48000; i++) { if (i % 32 == 0) a.update(220.f, p, i / 48000.f); a.process(true, p, L, R); y.push_back(L); }
	CHECK(tone(y, 220, 48000, 9600) > 0.1);
	auto t0 = std::chrono::steady_clock::now();
	Additive v[4];
	for (auto& x : v) { x.sr = 48000; x.reset(); }
	for (int i = 0; i < 48000; i++) for (auto& x : v) { if (i % 32 == 0) x.update(110.f, p, 0.f); x.process(true, p, L, R); }
	double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::printf("  SPECTRA 4x32: %.2f%% of one core\n", s * 100);
}

TEST("KIT synth voices: finite, bounded, silent after decay") {
	for (int src = 0; src < 2; src++)
		for (int v = 0; v < KIT_VOICES; v++) {
			DrumSynth d; d.setup(v, 48000);
			d.trigger(1.f, 0.f, 1.f, src == 0);
			double pk = 0; bool fin = true;
			std::vector<float> y;
			for (int i = 0; i < 48000 * 3; i++) { float s = d.process(); y.push_back(s); fin &= std::isfinite(s); pk = std::max(pk, (double)std::fabs(s)); }
			CHECK(fin && pk < 2.0 && pk > 0.1);
			CHECK(!d.active);
			CHECK(std::fabs(y.back()) < 1e-6);
		}
	// choke: closed hat stops a ringing open hat within 25 ms
	DrumSynth oh; oh.setup(OHAT, 48000); oh.trigger(1, 0, 1, true);
	for (int i = 0; i < 480; i++) oh.process();
	oh.stopChoke();
	for (int i = 0; i < 1200; i++) oh.process();
	CHECK(!oh.active);
	// crunch keeps a signal and stays bounded
	Crunch c; c.set(0.5f, 48000);
	double mx = 0; for (int i = 0; i < 4800; i++) mx = std::max(mx, (double)std::fabs(c.process(0.8f * std::sin(i * 0.05f))));
	CHECK(mx > 0.5 && mx < 1.1);
}

static std::vector<uint8_t> makeWav(int bits, bool flt, int ch, int rate, const std::vector<float>& mono) {
	std::vector<uint8_t> b;
	auto put = [&](const void* p, size_t n) { const uint8_t* q = (const uint8_t*)p; b.insert(b.end(), q, q + n); };
	auto u32 = [&](uint32_t v) { put(&v, 4); };
	auto u16 = [&](uint16_t v) { put(&v, 2); };
	int bps = bits / 8;
	uint32_t data = (uint32_t)(mono.size() * ch * bps);
	put("RIFF", 4); u32(36 + data); put("WAVEfmt ", 8); u32(16); u16(flt ? 3 : 1); u16((uint16_t)ch); u32(rate);
	u32(rate * ch * bps); u16((uint16_t)(ch * bps)); u16((uint16_t)bits); put("data", 4); u32(data);
	for (float v : mono) for (int c = 0; c < ch; c++) {
		if (flt) { float f = v; put(&f, 4); }
		else if (bits == 8) { uint8_t q = (uint8_t)std::lround(v * 127 + 128); put(&q, 1); }
		else if (bits == 16) { int16_t q = (int16_t)std::lround(v * 32767); put(&q, 2); }
		else if (bits == 24) { int32_t q = (int32_t)std::lround(v * 8388607); put(&q, 3); }
		else { int32_t q = (int32_t)std::lround((double)v * 2147483647.0); put(&q, 4); }
	}
	return b;
}

TEST("WAV loading: formats, stereo, rates, corrupt files") {
	std::vector<float> s;
	for (int i = 0; i < 2205; i++) s.push_back(0.5f * std::sin(i * 0.1f));
	struct F { int bits; bool flt; int ch; int rate; double tol; };
	for (F f : {F{8, false, 1, 22050, 0.02}, F{16, false, 1, 44100, 1e-4}, F{24, false, 2, 96000, 1e-5}, F{32, false, 1, 44100, 1e-6}, F{32, true, 2, 44100, 1e-6}}) {
		auto bytes = makeWav(f.bits, f.flt, f.ch, f.rate, s);
		std::string err;
		auto sd = wav::loadMemory(bytes.data(), bytes.size(), err);
		CHECK(sd != nullptr);
		if (!sd) continue;
		CHECK(sd->orig.size() == s.size() && sd->origRate == (float)f.rate);
		double g = f.ch == 2 ? 0.70710678 * 2 / 2 * 2 : 1.0;  // stereo of identical channels: 2 * 0.707
		double maxErr = 0;
		for (size_t i = 0; i < s.size(); i++) maxErr = std::max(maxErr, std::fabs(sd->orig[i] - s[i] * g));
		CHECK(maxErr < f.tol * g + 1e-6);
	}
	std::string err;
	std::vector<uint8_t> junk = {'R', 'I', 'F', 'F', 1, 2, 3};
	CHECK(wav::loadMemory(junk.data(), junk.size(), err) == nullptr);
	auto good = makeWav(16, false, 1, 44100, s);
	good.resize(60);
	auto t = wav::loadMemory(good.data(), good.size(), err);
	CHECK(t == nullptr || t->orig.size() < s.size());
}

TEST("Resampling keeps pitch and rejects images; tune halves length") {
	std::vector<float> x;
	for (int i = 0; i < 44100; i++) x.push_back(0.5f * std::sin(2 * M_PI * 1000.0 * i / 44100));
	auto y = wav::resample(x, 44100, 48000);
	CHECK(std::labs((long)y.size() - 48000) <= 1);
	double a = tone(y, 1000, 48000, 2000);
	CHECK_NEAR(a, 0.5, 0.01);
	double img = tone(y, 47000 - 44100 + 0, 48000, 2000);  // nearest image of 1 kHz around 44.1k folded
	CHECK(db(img, a) < -80);
	SampleData sd; sd.orig = x; sd.origRate = 44100; wav::prepare(sd, 48000);
	SamplePlayer p; p.trigger(1.f, 12.f, 1.f, true, 48000);
	long n = 0; while (p.pos >= 0.0 && n < 200000) { p.process(&sd); n++; }
	CHECK(std::labs(n - 24000) <= 2);
}

TEST("Kit folder keywords") {
	CHECK(wav::voiceForFileName("BD_01.wav") == KICK);
	CHECK(wav::voiceForFileName("909 Kick.WAV") == KICK);
	CHECK(wav::voiceForFileName("snare-tight.wav") == SNARE);
	CHECK(wav::voiceForFileName("SD 3.wav") == SNARE);
	CHECK(wav::voiceForFileName("clap.wav") == CLAP);
	CHECK(wav::voiceForFileName("CP.wav") == CLAP);
	CHECK(wav::voiceForFileName("ClosedHat.wav") == CHAT);
	CHECK(wav::voiceForFileName("HH 1.wav") == CHAT);
	CHECK(wav::voiceForFileName("OpenHat.wav") == OHAT);
	CHECK(wav::voiceForFileName("OH_long.wav") == OHAT);
	CHECK(wav::voiceForFileName("ohh.wav") == OHAT);
	CHECK(wav::voiceForFileName("Rim.wav") == PERC);
	CHECK(wav::voiceForFileName("cowbell.wav") == PERC);
	CHECK(wav::voiceForFileName("pad.wav") == -1);
}
