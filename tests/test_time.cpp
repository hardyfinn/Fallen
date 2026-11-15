#include "check.hpp"
#include "../src/time/Dura.hpp"
#include "../src/time/Group.hpp"
#include "../src/time/Prime.hpp"
#include "../src/time/Lanes.hpp"
#include "../src/time/Cycle.hpp"
#include "../src/time/Hold.hpp"
#include <vector>
#include <cmath>

using namespace fallen;

// Run DURA for `sec` seconds at rate fs; return event sample indices (ON steps only).
static std::vector<long> duraEvents(Dura::Params p, double fs, double sec, std::vector<double>* gates = nullptr, std::vector<double>* ints = nullptr) {
	Dura d; d.reset();
	std::vector<long> ev;
	long n = (long)(sec * fs);
	for (long i = 0; i < n; i++) {
		auto o = d.process(1.0 / fs, p);
		if (o.trig) { ev.push_back(i); if (gates) gates->push_back(o.gate); if (ints) ints->push_back(o.interval); }
	}
	return ev;
}

TEST("DURA units 1,2,3 at 40 ms, exact over 10 minutes at all rates") {
	for (double fs : {44100.0, 48000.0, 96000.0, 192000.0}) {
		Dura::Params p; p.mode = Dura::UNITS; p.unitMs = 40; p.steps = 3;
		p.units[0] = 1; p.units[1] = 2; p.units[2] = 3;
		auto ev = duraEvents(p, fs, 600.0);
		CHECK(ev.size() == 7500);  // 600 s / 240 ms * 3
		CHECK(ev[0] == 0);
		CHECK(std::labs(ev[1] - (long)std::ceil(0.040 * fs)) <= 1);
		CHECK(std::labs(ev[2] - (long)std::ceil(0.120 * fs)) <= 1);
		// last loop start: 2499 loops * 0.24 s
		long expect = (long)std::ceil(2499 * 0.24 * fs - 1e-6);
		CHECK(std::labs(ev[7497] - expect) <= 1);
	}
}

TEST("DURA repeats, stretch, ratio, nudge") {
	double fs = 48000;
	Dura::Params p; p.mode = Dura::MS; p.steps = 1; p.lenMs[0] = 10; p.rep[0] = 4;
	auto ev = duraEvents(p, fs, 0.0399);
	CHECK(ev.size() == 4);
	CHECK(std::labs(ev[3] - 1440) <= 1);
	Dura::Params q; q.mode = Dura::UNITS; q.unitMs = 50; q.steps = 2; q.stretch = 2.f;
	auto e2 = duraEvents(q, fs, 0.25);
	CHECK(e2.size() == 3 && std::labs(e2[1] - 4800) <= 1);
	Dura::Params r; r.mode = Dura::RATIO; r.baseMs = 300; r.steps = 1; r.ratioIdx[0] = 7;  // 2/3
	auto e3 = duraEvents(r, fs, 0.41);
	CHECK(e3.size() == 3 && std::labs(e3[1] - 9600) <= 1);
	Dura::Params u; u.mode = Dura::UNITS; u.unitMs = 100; u.steps = 8; u.nudge = -0.5f;
	CHECK_NEAR(Dura::loopLength(u), 0.75, 1e-9);
	auto e4 = duraEvents(u, fs, 1.0);
	CHECK(std::labs(e4[8] - 36000) <= 1);  // loop is 7.5 units = 750 ms
}

TEST("DURA gate spread is deterministic and spans 1..99") {
	Dura::Params p; p.mode = Dura::MS; p.steps = 16; p.spread = 1.f; p.length = 50.f;
	for (int i = 0; i < 16; i++) { p.lenMs[i] = 20; p.rep[i] = 4; }
	std::vector<double> g1, g2, i1;
	duraEvents(p, 48000, 2.0, &g1, &i1);
	duraEvents(p, 48000, 2.0, &g2);
	CHECK(g1 == g2);
	double lo = 1, hi = 0;
	for (size_t k = 0; k < g1.size(); k++) { double pc = g1[k] / i1[k]; lo = std::min(lo, pc); hi = std::max(hi, pc); }
	CHECK(lo < 0.1 && hi > 0.9 && lo >= 0.0099 && hi <= 0.9901);
	// LOOP mode: the same every loop
	CHECK_NEAR(g1[0], g1[64], 1e-12);
	p.evolve = true;
	std::vector<double> g3, g4;
	duraEvents(p, 48000, 2.0, &g3); duraEvents(p, 48000, 2.0, &g4);
	CHECK(g3 == g4);
	bool differs = false;
	for (int k = 0; k < 64; k++) if (std::fabs(g3[k] - g3[k + 64]) > 1e-9) differs = true;
	CHECK(differs);
}

TEST("DURA interval output is the time to the next event") {
	Dura::Params p; p.mode = Dura::MS; p.steps = 3; p.lenMs[0] = 100; p.lenMs[1] = 200; p.lenMs[2] = 50;
	std::vector<double> ints;
	auto ev = duraEvents(p, 48000, 0.34, nullptr, &ints);
	CHECK(ev.size() == 3);
	CHECK_NEAR(ints[0], 0.1, 1e-9); CHECK_NEAR(ints[1], 0.2, 1e-9); CHECK_NEAR(ints[2], 0.05, 1e-9);
	CHECK(std::labs(ev[1] - ev[0] - 4800) <= 1 && std::labs(ev[2] - ev[1] - 9600) <= 1);
}

TEST("DURA stretch changes take effect at the next event") {
	Dura d; Dura::Params p; p.mode = Dura::MS; p.steps = 1; p.lenMs[0] = 100;
	double fs = 48000; std::vector<long> ev;
	for (long i = 0; i < 48000; i++) {
		if (i == 2400) p.stretch = 4.f;  // mid-interval
		if (d.process(1 / fs, p).trig) ev.push_back(i);
	}
	CHECK(ev.size() >= 2 && std::labs(ev[1] - 4800) <= 1);  // second event still at 100 ms
	CHECK(ev.size() >= 3 && std::labs(ev[2] - ev[1] - 19200) <= 1);
}

TEST("GROUP spacing, curve, period and drift") {
	double fs = 48000;
	auto run = [&](Group::Params p, double sec) {
		Group g; g.reset(); std::vector<long> ev;
		for (long i = 0; i < (long)(sec * fs); i++) if (g.process(1 / fs, p, false).event) ev.push_back(i);
		return ev;
	};
	Group::Params p; p.count = 5; p.spaceMs = 100; p.curve = 0; p.periodMs = 1000;
	auto ev = run(p, 0.99);
	CHECK(ev.size() == 5);
	for (int k = 0; k < 5; k++) CHECK(std::labs(ev[k] - k * 4800) <= 1);
	p.curve = -1;
	ev = run(p, 0.99);
	CHECK(ev.size() == 5 && std::labs(ev[1] - ev[0] - 4800) <= 1 && std::labs(ev[4] - ev[3] - 600) <= 1);
	// period shorter than the span: no overlap, next group one SPACE after the last event
	Group::Params q; q.count = 4; q.spaceMs = 100; q.curve = 0; q.periodMs = 50;
	ev = run(q, 0.75);
	CHECK(ev.size() >= 5 && std::labs(ev[4] - 19200) <= 1);
	// drift repeatable
	Group::Params r; r.count = 3; r.spaceMs = 50; r.periodMs = 300; r.rate = 0.1f; r.depth = 1.f;
	CHECK(run(r, 5.0) == run(r, 5.0));
}

TEST("PRIME realign, nesting, euclid") {
	Prime pr;
	Prime::Row rows[4];
	rows[0] = {3, 1, 0, false}; rows[1] = {5, 1, 0, false}; rows[2] = {1, 1, 0, false}; rows[3] = {1, 1, 0, false};
	pr.setRows(rows);
	bool ev[4];
	int re = 0, first = -1, second = -1;
	for (int n = 0; n < 40; n++) if (pr.pulse(ev)) { re++; if (first < 0) first = n; else if (second < 0) second = n; }
	CHECK(first == 0 && second == 15);
	Prime p2; Prime::Row r2[4] = {{3, 1, 0, false}, {5, 1, 0, true}, {1, 1, 0, false}, {1, 1, 0, false}};
	p2.setRows(r2);
	std::vector<int> bev;
	for (int n = 0; n < 60; n++) { p2.pulse(ev); if (ev[1]) bev.push_back(n); }
	CHECK(bev.size() == 4 && bev[1] - bev[0] == 15);
	bool pat[5]; euclid(pat, 5, 2);
	int hits = 0; for (bool b : pat) hits += b;
	CHECK(hits == 2 && pat[0]);
	Prime p3; p3.setRows(r2);
	CHECK(p3.pulsesToRealign() == 0);
	p3.pulse(ev);
	CHECK(p3.pulsesToRealign() == 14);
}

TEST("LANES loop lengths, scroll, intensity, pattern switch") {
	Lanes L; Lanes::Params p;
	L.pats[0].set(0, 0, true);
	L.pats[0].set(1, 0, true);
	p.len[0] = 16; p.len[1] = 13;
	int both = 0, firstBoth = -1, secondBoth = -1;
	for (int c = 0; c < 450; c++) { auto o = L.clock(p); if (o.ev[0] && o.ev[1]) { both++; if (firstBoth < 0) firstBoth = c; else if (secondBoth < 0) secondBoth = c; } }
	CHECK(firstBoth == 0 && secondBoth == 208);
	Lanes S; Lanes::Params q; q.len[0] = 4; q.scroll[0] = 1; S.pats[0].set(0, 0, true);
	std::vector<int> at;
	for (int c = 0; c < 12; c++) if (S.clock(q).ev[0]) at.push_back(c);
	CHECK(at.size() == 3 && at[0] == 0 && at[1] == 5 && at[2] == 10);
	Lanes I; Lanes::Params ip; ip.len[0] = 16; ip.intensity = 0.5f;
	for (int s = 0; s < 16; s += 2) I.pats[0].set(0, s, true);  // 8 active
	std::vector<int> played;
	for (int c = 0; c < 16; c++) if (I.clock(ip).ev[0]) played.push_back(c);
	CHECK(played.size() == 4 && played[0] == 0);
	std::vector<int> again;
	for (int c = 0; c < 16; c++) if (I.clock(ip).ev[0]) again.push_back(c);
	CHECK(played == again);
	Lanes P; Lanes::Params pp; pp.len[0] = 4; P.pats[1].set(0, 1, true);
	pp.pattern = 1;
	bool ev1 = false;
	for (int c = 0; c < 4; c++) if (P.clock(pp).ev[0]) ev1 = true;
	CHECK(!ev1);  // switched only at the wrap
	CHECK(!P.clock(pp).ev[0]);
	CHECK(P.clock(pp).ev[0]);
}

TEST("CYCLE lengths, dividers, voice volts") {
	Cycle c; int len[4] = {3, 5, 1, 1}, div[4] = {1, 1, 1, 1};
	c.trigger(len, div);
	CHECK(c.realignIn(len, div) == 0);
	for (int i = 0; i < 15; i++) c.trigger(len, div);
	CHECK(c.pos[0] == 0 && c.pos[1] == 0);
	CHECK(c.realignIn(len, div) == 0);
	Cycle d; int l2[4] = {16, 1, 1, 1}, d2[4] = {2, 1, 1, 1};
	for (int i = 0; i < 5; i++) d.trigger(l2, d2);
	CHECK(d.pos[0] == 2);
	CHECK_NEAR(Cycle::toVolts(11.f / 31.f, Cycle::VOICE, 0.f), 1.1, 1e-6);
	CHECK_NEAR(Cycle::toVolts(0.75f, Cycle::PITCH, 0.f), 1.0, 1e-6);
}

TEST("HOLD gate follows the gap") {
	double fs = 48000;
	Hold h; Hold::Params p; p.length = 50;
	long gateSamples = 0;
	for (int k = 0; k < 3; k++) {
		h.trigger(p, -1);
		long g = 0;
		for (int i = 0; i < 9600; i++) { h.process(1 / fs, p); if (h.gate()) g++; }
		gateSamples = g;
	}
	CHECK(std::labs(gateSamples - 4800) <= 2);
	h.trigger(p, 0.05);
	long g = 0;
	for (int i = 0; i < 9600; i++) { h.process(1 / fs, p); if (h.gate()) g++; }
	CHECK(std::labs(g - 1200) <= 2);
}
