#define DR_WAV_IMPLEMENTATION
#define DRWAV_NO_STDIO_WIDE
#include "third_party/dr_wav.h"
#include "WavLoad.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace fallen {
namespace wav {

static std::shared_ptr<SampleData> fromDrwav(drwav& w, std::string& err) {
	if (w.channels < 1 || w.sampleRate < 1000) { err = "unsupported WAV format"; return nullptr; }
	drwav_uint64 frames = w.totalPCMFrameCount;
	drwav_uint64 maxFrames = (drwav_uint64)(MAX_SECONDS * w.sampleRate);
	auto s = std::make_shared<SampleData>();
	if (frames > maxFrames) { frames = maxFrames; s->truncated = true; }
	std::vector<float> inter((size_t)(frames * w.channels));
	drwav_uint64 got = drwav_read_pcm_frames_f32(&w, frames, inter.data());
	if (got == 0) { err = "no audio in file"; return nullptr; }
	s->orig.resize((size_t)got);
	float g = w.channels > 1 ? 0.70710678f * 2.f / w.channels : 1.f;  // stereo: (L+R) at -3 dB
	for (drwav_uint64 i = 0; i < got; i++) {
		float a = 0.f;
		for (unsigned c = 0; c < w.channels; c++) a += inter[(size_t)(i * w.channels + c)];
		s->orig[(size_t)i] = a * g;
	}
	s->origRate = (float)w.sampleRate;
	return s;
}

std::shared_ptr<SampleData> load(const std::string& path, std::string& err) {
	drwav w;
	if (!drwav_init_file(&w, path.c_str(), nullptr)) { err = "could not read WAV file"; return nullptr; }
	auto s = fromDrwav(w, err);
	drwav_uninit(&w);
	if (s) {
		s->path = path;
		size_t sl = path.find_last_of("/\\");
		s->name = sl == std::string::npos ? path : path.substr(sl + 1);
	}
	return s;
}

std::shared_ptr<SampleData> loadMemory(const void* data, size_t size, std::string& err) {
	drwav w;
	if (!drwav_init_memory(&w, data, size, nullptr)) { err = "could not read WAV data"; return nullptr; }
	auto s = fromDrwav(w, err);
	drwav_uninit(&w);
	return s;
}

std::vector<float> resample(const std::vector<float>& in, double inRate, double outRate) {
	if (in.empty()) return {};
	if (std::fabs(inRate - outRate) < 1e-6) return in;
	double ratio = outRate / inRate;
	size_t n = (size_t)std::ceil(in.size() * ratio);
	std::vector<float> out(n);
	const int HALF = 16;                      // taps each side, at the lower of the two rates
	double cutoff = std::min(1.0, ratio) * 0.94;
	double step = 1.0 / ratio;
	long len = (long)in.size();
	for (size_t i = 0; i < n; i++) {
		double x = i * step;
		long c = (long)std::floor(x);
		int span = (int)std::ceil(HALF / cutoff);
		double acc = 0, wsum = 0;
		for (long k = c - span + 1; k <= c + span; k++) {
			double d = x - k;
			double u = d * cutoff;
			if (std::fabs(u) >= HALF) continue;
			double sinc = std::fabs(u) < 1e-9 ? 1.0 : std::sin(M_PI * u) / (M_PI * u);
			double win = 0.42 + 0.5 * std::cos(M_PI * u / HALF) + 0.08 * std::cos(2 * M_PI * u / HALF);  // Blackman
			double wgt = sinc * win;
			wsum += wgt;
			if (k >= 0 && k < len) acc += in[(size_t)k] * wgt;
		}
		out[i] = (float)(wsum != 0 ? acc / wsum * 1.0 : 0.0);
	}
	return out;
}

void prepare(SampleData& s, float outRate) {
	s.data = resample(s.orig, s.origRate, outRate);
	s.rate = outRate;
}

bool write16(const std::string& path, const std::vector<float>& x, int ch, int rate, float fullScale) {
	FILE* f = std::fopen(path.c_str(), "wb");
	if (!f) return false;
	auto u32 = [&](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [&](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	uint32_t bytes = (uint32_t)(x.size() * 2);
	std::fwrite("RIFF", 1, 4, f); u32(36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16); u16(1); u16((uint16_t)ch); u32((uint32_t)rate); u32((uint32_t)(rate * ch * 2)); u16((uint16_t)(ch * 2)); u16(16);
	std::fwrite("data", 1, 4, f); u32(bytes);
	for (float v : x) {
		float s = clampf(v / fullScale, -1.f, 1.f);
		int16_t q = (int16_t)std::lround(s * 32767.f);
		std::fwrite(&q, 2, 1, f);
	}
	std::fclose(f);
	return true;
}

static bool hasWord(const std::string& s, const char* w) {
	// match w as a whole token: letters around it break the match (so "chh" in "chhh" still fails, "oh" in "hohner" fails)
	std::string word(w);
	size_t p = 0;
	while ((p = s.find(word, p)) != std::string::npos) {
		bool before = p == 0 || !std::isalpha((unsigned char)s[p - 1]);
		size_t e = p + word.size();
		bool after = e >= s.size() || !std::isalpha((unsigned char)s[e]);
		if (before && after) return true;
		p++;
	}
	return false;
}

int voiceForFileName(const std::string& name0) {
	std::string s;
	for (char c : name0) s += (char)std::tolower((unsigned char)c);
	size_t dot = s.find_last_of('.');
	if (dot != std::string::npos) s = s.substr(0, dot);
	auto any = [&](std::initializer_list<const char*> ws, bool substr) {
		for (const char* w : ws) if (substr ? s.find(w) != std::string::npos : hasWord(s, w)) return true;
		return false;
	};
	bool open = any({"open", "ohh"}, true) || any({"oh", "oht"}, false);
	if (any({"kick", "kik", "bassdrum"}, true) || any({"bd"}, false)) return KICK;
	if (any({"snare", "snr"}, true) || any({"sd"}, false)) return SNARE;
	if (any({"clap", "clp"}, true) || any({"cp"}, false)) return CLAP;
	if (open && (any({"hat", "hh", "ohh"}, true) || any({"oh"}, false))) return OHAT;
	if (any({"chh", "closed", "hihat", "hat"}, true) || any({"hh", "ch"}, false)) return CHAT;
	if (any({"rim", "perc", "cow", "tom", "tamb", "shaker"}, true) || any({"rs"}, false)) return PERC;
	return -1;
}

} // namespace wav
} // namespace fallen
