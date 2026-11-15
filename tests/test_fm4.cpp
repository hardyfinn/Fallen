#include "util.hpp"
#include "../src/dsp/Fm4Voice.hpp"
#include "../src/dsp/FactoryBank.hpp"
#include "../src/io/Syx4op.hpp"
#include <chrono>

using namespace fallen;

static Voice4op oddVoice(int seed) {
	Voice4op v;
	auto h = [&](int k, int mod) { return (int)(hash01(seed, k) * mod); };
	for (int i = 0; i < 4; i++) {
		Op4& o = v.op[i];
		o.ar = h(i * 20 + 0, 32); o.d1r = h(i * 20 + 1, 32); o.d2r = h(i * 20 + 2, 32); o.rr = 1 + h(i * 20 + 3, 15); o.d1l = h(i * 20 + 4, 16);
		o.ls = h(i * 20 + 5, 100); o.rs = h(i * 20 + 6, 4); o.ebs = h(i * 20 + 7, 8); o.ame = h(i * 20 + 8, 2); o.kvs = h(i * 20 + 9, 8);
		o.out = h(i * 20 + 10, 100); o.crs = h(i * 20 + 11, 64); o.det = h(i * 20 + 12, 7);
		o.fix = h(i * 20 + 13, 2); o.fixrg = h(i * 20 + 14, 8); o.fine = h(i * 20 + 15, 16); o.osw = h(i * 20 + 16, 8); o.shft = h(i * 20 + 17, 4);
	}
	v.alg = h(100, 8); v.fb = h(101, 8); v.lfs = h(102, 100); v.lfd = h(103, 100); v.pmd = h(104, 100); v.amd = h(105, 100);
	v.sy = h(106, 2); v.lfw = h(107, 4); v.pms = h(108, 8); v.ams = h(109, 4); v.trps = h(110, 49); v.pbr = h(111, 13);
	v.mono = h(112, 2); v.porm = h(113, 2); v.port = h(114, 100); v.fcvol = h(115, 100); v.sus = h(116, 2); v.porta = h(117, 2);
	v.chorus = h(118, 2); v.mwp = h(119, 100); v.mwa = h(120, 100); v.bcp = h(121, 100); v.bca = h(122, 100); v.bcpb = h(123, 100);
	v.bceb = h(124, 100);
	for (int i = 0; i < 6; i++) v.peg[i] = h(130 + i, 100);
	v.rev = 1 + h(140, 7); v.fcpitch = h(141, 100); v.fcamp = h(142, 100);
	v.setName("TEST " + std::to_string(seed));
	v.extended = true;
	return v;
}

TEST("SysEx 32-voice bank round trip, every field") {
	Bank4op b;
	for (int i = 0; i < 32; i++) b.v[i] = oddVoice(i + 1);
	b.v[5].extended = false;
	for (auto& o : b.v[5].op) { o.fix = o.fixrg = o.fine = o.osw = o.shft = 0; }
	b.v[5].rev = b.v[5].fcpitch = b.v[5].fcamp = 0;
	auto msg = syx::bankMessage(b);
	CHECK(msg.size() == 4104);
	auto r = syx::parse(msg);
	CHECK(r.hasBank && !r.checksumBad && r.clamped == 0);
	int same = 0;
	for (int i = 0; i < 32; i++) same += (r.bank.v[i] == b.v[i]);
	CHECK(same == 32);
	// checksum damaged: still loads, with a warning
	msg[100] ^= 1;
	auto r2 = syx::parse(msg);
	CHECK(r2.hasBank && r2.checksumBad);
	// truncated file does not crash and loads nothing
	std::vector<uint8_t> cut(msg.begin(), msg.begin() + 2000);
	auto r3 = syx::parse(cut);
	CHECK(!r3.ok());
}

TEST("SysEx single voice with ACED, and 6-op refusal") {
	Voice4op v = oddVoice(77);
	auto msg = syx::voiceMessages(v);
	auto r = syx::parse(msg);
	CHECK(r.hasVoice && !r.checksumBad);
	CHECK(r.voice == v);
	// VCED alone is non-extended
	Voice4op w = oddVoice(78);
	auto m2 = syx::voiceMessages(w);
	std::vector<uint8_t> vced(m2.begin() + 6 + 10 + 23 + 2, m2.end());
	auto r2 = syx::parse(vced);
	CHECK(r2.hasVoice && !r2.voice.extended && r2.voice.op[0].out == w.op[0].out && r2.voice.op[3].crs == w.op[3].crs);
	std::vector<uint8_t> six = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
	six.resize(6 + 4096, 0); six.push_back(0); six.push_back(0xF7);
	auto r3 = syx::parse(six);
	CHECK(!r3.ok() && r3.sixOp);
	CHECK(r3.summary().find("6-operator") != std::string::npos);
	// raw 4096-byte bank
	Bank4op fb = factoryBank();
	auto bm = syx::bankMessage(fb);
	std::vector<uint8_t> raw(bm.begin() + 6, bm.begin() + 6 + 4096);
	auto r4 = syx::parse(raw, true);
	CHECK(r4.hasBank && r4.bank.v[0] == fb.v[0]);
}

TEST("Factory bank exports and re-imports identically") {
	Bank4op fb = factoryBank();
	auto r = syx::parse(syx::bankMessage(fb));
	int same = 0;
	for (int i = 0; i < 32; i++) same += (r.bank.v[i] == fb.v[i]);
	CHECK(same == 32);
	CHECK(fb.v[0].nameStr() == "HOUSE ORG");
	CHECK(fb.v[31].nameStr() == "INIT");
	int ext = 0;
	for (auto& v : fb.v) ext += v.extended;
	CHECK(ext >= 8);
}

static std::vector<float> render(const Bank4op& b, int voice, float pitch, double sec, float fs = 48000.f, double gateSec = 1e9) {
	Fm4Engine e; e.bank = &b; e.setSampleRate(fs); e.reset();
	Fm4Macros m; Fm4Macros mm[8];
	float sp[8] = {}, out[8];
	e.noteOn(0, voice, pitch, 1.f, m);
	std::vector<float> y;
	for (long i = 0; i < (long)(sec * fs); i++) {
		if (i == (long)(gateSec * fs)) e.noteOff(0);
		e.process(1, &pitch, mm, sp, out);
		y.push_back(out[0]);
	}
	return y;
}

TEST("FM4 INIT voice is a sine at the note frequency") {
	Bank4op b = factoryBank();
	auto y = render(b, 31, 0.f, 1.0);
	double f0 = tone(y, 261.6256, 48000, 4800);
	double h2 = tone(y, 523.25, 48000, 4800);
	CHECK(f0 > 3.0 && f0 < 6.0);
	CHECK(db(h2, f0) < -50);
	// ALG 8 single carrier, other ops silent: the same
	Bank4op c = b; c.v[31].alg = 7;
	auto z = render(c, 31, 1.f, 0.5);
	CHECK(tone(z, 523.2512, 48000, 4800) > 1.5);
	// pitch accuracy: zero crossings over 0.5 s
	int zc = 0;
	for (size_t i = 4801; i < y.size(); i++) if (y[i - 1] < 0 && y[i] >= 0) zc++;
	CHECK_NEAR(zc / ((y.size() - 4801) / 48000.0), 261.6256, 1.0);
}

TEST("FM4 feedback adds harmonics, envelopes decay and release") {
	Bank4op b = factoryBank();
	Voice4op& v = b.v[31];
	v.op[0].out = 0; v.op[3].out = 99; v.alg = 7;
	auto a = render(b, 31, 0.f, 0.5);
	v.fb = 7;
	auto c = render(b, 31, 0.f, 0.5);
	double ha = tone(a, 3 * 261.6256, 48000, 4800), hc = tone(c, 3 * 261.6256, 48000, 4800);
	CHECK(hc > 10 * ha + 0.01);
	// D1R 10 to D1L 0 with D2R 0: should fall a long way within 2 s
	Bank4op d = factoryBank();
	d.v[31].op[0].d1r = 14; d.v[31].op[0].d1l = 0;
	auto e = render(d, 31, 0.f, 3.0);
	double early = rms(std::vector<float>(e.begin() + 480, e.begin() + 4800));
	double late = rms(std::vector<float>(e.end() - 4800, e.end()));
	CHECK(db(late, early) < -30);
	// release
	auto r = render(factoryBank(), 31, 0.f, 2.0, 48000.f, 0.5);
	CHECK(rms(std::vector<float>(r.end() - 4800, r.end())) < 1e-3);
	// all factory voices are finite and audible
	Bank4op fb = factoryBank();
	int audible = 0;
	for (int i = 0; i < 32; i++) {
		auto y = render(fb, i, 0.f, 0.6, 48000.f, 0.4);
		bool fin = true; double pk = 0;
		for (float s : y) { fin &= std::isfinite(s); pk = std::max(pk, (double)std::fabs(s)); }
		CHECK(fin && pk < 10.0);
		if (pk > 0.3) audible++;
	}
	CHECK(audible == 32);
}

TEST("FM4 waveforms match their formulas") {
	const auto& W = fm4::waves();
	for (double ph : {0.1, 0.3, 0.6, 0.85}) {
		double th = 2 * M_PI * ph, s = std::sin(th);
		CHECK_NEAR(W.lookup(0, ph), s, 1e-3);
		CHECK_NEAR(W.lookup(2, ph), std::max(s, 0.0), 1e-3);
		CHECK_NEAR(W.lookup(4, ph), ph < 0.5 ? std::sin(2 * th) : 0.0, 2e-3);
		CHECK_NEAR(W.lookup(6, ph), ph < 0.5 ? std::fabs(std::sin(2 * th)) : 0.0, 2e-3);
	}
}

TEST("FM4 eight voices within CPU budget") {
	Bank4op b = factoryBank();
	Fm4Engine e; e.bank = &b; e.setSampleRate(48000); e.reset();
	Fm4Macros m; Fm4Macros mm[8]; float sp[8] = {}, out[8], pitch[8];
	for (int i = 0; i < 8; i++) { pitch[i] = i / 12.f; e.noteOn(i, i * 3, pitch[i], 1.f, m); }
	auto t0 = std::chrono::steady_clock::now();
	for (int i = 0; i < 48000; i++) e.process(8, pitch, mm, sp, out);
	double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::printf("  FM4 8 voices: %.2f%% of one core\n", s * 100);
	CHECK(s < 0.05);
}
