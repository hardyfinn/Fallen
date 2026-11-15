// End to end: DURA -> CYCLE -> FM4 and DURA -> LANES -> KIT, plus a SPECTRA drone, for 10 s.
// Writes tests/out/fallen-demo.wav so the build can be listened to.
#include "util.hpp"
#include "../src/time/Dura.hpp"
#include "../src/time/Lanes.hpp"
#include "../src/time/Cycle.hpp"
#include "../src/dsp/Fm4Voice.hpp"
#include "../src/dsp/FactoryBank.hpp"
#include "../src/dsp/KitVoices.hpp"
#include "../src/dsp/Additive.hpp"
#include "../src/io/WavLoad.hpp"
#include <sys/stat.h>

using namespace fallen;

TEST("End to end demo render (tests/out/fallen-demo.wav)") {
	const float fs = 48000.f;
	const double dt = 1.0 / fs;
	const long total = (long)(10.0 * fs);

	// DURA A: the "one slider" UNITS pattern 3,3,3,4,2,1 at 50 ms, with a stutter on step 5.
	Dura durA; durA.reset();
	Dura::Params pa; pa.mode = Dura::UNITS; pa.unitMs = 50.f; pa.steps = 6; pa.length = 60.f;
	int u[6] = {3, 3, 3, 4, 2, 1};
	for (int i = 0; i < 6; i++) pa.units[i] = u[i];
	pa.rep[4] = 3;
	// DURA B: a steady 125 ms clock for the drum grid.
	Dura durB; durB.reset();
	Dura::Params pb; pb.mode = Dura::MS; pb.steps = 1; pb.lenMs[0] = 125.f;

	// CYCLE: lane 0 pitch (length 5), lane 1 FM4 voice number (length 3): sounds against notes.
	Cycle cyc; int clen[4] = {5, 3, 1, 1}, cdiv[4] = {1, 1, 1, 1};
	float pitches[5] = {0.f, 3.f, 7.f, 10.f, 5.f};
	for (int i = 0; i < 5; i++) cyc.val[0][i] = 0.5f + pitches[i] / 12.f / 4.f;  // PITCH mode: (v-0.5)*4 octaves
	int fmVoices[3] = {15, 12, 13};  // KALIMBA, MARIMBA, METAL PERC
	for (int i = 0; i < 3; i++) cyc.val[1][i] = fmVoices[i] / 31.f;

	// LANES: loops a little too short.
	Lanes lanes;
	Lanes::Params lp;
	lp.len[0] = 16; lp.len[1] = 15; lp.len[2] = 13; lp.len[3] = 15; lp.len[4] = 16;
	lp.scroll[1] = -1;
	for (int s : {0, 4, 8, 12}) lanes.pats[0].set(0, s, true);
	for (int s : {4, 12}) lanes.pats[0].set(1, s, true);
	for (int s = 0; s < 13; s += 2) lanes.pats[0].set(2, s, true);
	lanes.pats[0].set(3, 6, true);
	for (int s : {3, 11, 14}) lanes.pats[0].set(4, s, true);
	const int rowVoice[5] = {KICK, CLAP, CHAT, OHAT, PERC};

	Bank4op bank = factoryBank();
	Fm4Engine fm; fm.bank = &bank; fm.setSampleRate(fs); fm.reset();
	Fm4Macros mac[8];
	float fmPitch[8] = {}, spread[8] = {}, fmOut[8];
	double fmOff[8] = {};
	int nextCh = 0;

	DrumSynth drums[6];
	Crunch crunch[6];
	for (int i = 0; i < 6; i++) { drums[i].setup(i, fs); crunch[i].set(0.5f, fs); }

	Additive pad; pad.sr = fs; pad.reset();
	Additive::Params sp; sp.partials = 24; sp.stretch = 0.15f; sp.depth = 0.7f; sp.rate = 0.08f; sp.width = 0.8f; sp.attack = 2.f;

	std::vector<float> stereo;
	stereo.reserve(total * 2);
	double peak = 0;
	bool finite = true;
	int fmNotes = 0, hits = 0;
	for (long n = 0; n < total; n++) {
		double t = n * dt;
		// slow STRETCH performance on DURA A
		pa.stretch = (float)(1.0 + 0.35 * std::sin(2 * M_PI * t / 10.0));
		Dura::Out a = durA.process(dt, pa);
		if (a.trig) {
			cyc.trigger(clen, cdiv);
			int ch = nextCh;
			nextCh = (nextCh + 1) % 4;
			fmPitch[ch] = Cycle::toVolts(cyc.val[0][cyc.pos[0]], Cycle::PITCH, 0.f) - 1.f;
			int voice = (int)std::lround(Cycle::toVolts(cyc.val[1][cyc.pos[1]], Cycle::VOICE, 0.f) * 10.f);
			fm.noteOn(ch, voice, fmPitch[ch], 0.8f, mac[ch]);
			fmOff[ch] = t + a.gate;
			fmNotes++;
		}
		for (int c = 0; c < 4; c++)
			if (fmOff[c] > 0 && t >= fmOff[c]) { fm.noteOff(c); fmOff[c] = 0; }
		if (durB.process(dt, pb).trig) {
			lp.intensity = (float)std::min(1.0, 0.45 + t / 8.0);  // relaxed to intense
			Lanes::Out o = lanes.clock(lp);
			for (int r = 0; r < 5; r++)
				if (o.ev[r]) {
					int v = rowVoice[r];
					drums[v].trigger(r == 0 ? 1.f : 0.7f, 0.f, 1.f, v != KICK);
					if (v == CHAT) drums[OHAT].stopChoke();
					hits++;
				}
		}
		fm.process(4, fmPitch, mac, spread, fmOut);
		float fmSum = 0.f;
		for (int c = 0; c < 4; c++) fmSum += fmOut[c];
		float dr = 0.f;
		for (int i = 0; i < 6; i++) {
			float y = drums[i].process();
			dr += drums[i].early ? crunch[i].process(y) : y;
		}
		if (n % 32 == 0) pad.update(65.41f, sp, (float)(t * sp.rate));
		float pl, pr;
		pad.process(true, sp, pl, pr);
		float L = 0.45f * fmSum + 4.0f * dr + 2.0f * pl;
		float R = 0.45f * fmSum + 4.0f * dr + 2.0f * pr;
		finite &= std::isfinite(L) && std::isfinite(R);
		peak = std::max(peak, (double)std::max(std::fabs(L), std::fabs(R)));
		stereo.push_back(L);
		stereo.push_back(R);
	}
	std::printf("  demo: %d FM notes, %d drum hits, peak %.2f V\n", fmNotes, hits, peak);
	CHECK(finite);
	CHECK(peak > 1.0 && peak < 10.0);
	CHECK(fmNotes > 40 && hits > 60);
	mkdir("tests/out", 0755);
	CHECK(wav::write16("tests/out/fallen-demo.wav", stereo, 2, (int)fs, 10.f));
}
