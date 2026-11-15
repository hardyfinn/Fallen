// Reading WAV files (dr_wav) into mono float, resampling, and writing test WAVs.
#pragma once
#include <string>
#include <vector>
#include <memory>
#include "../dsp/KitVoices.hpp"

namespace fallen {
namespace wav {

constexpr float MAX_SECONDS = 20.f;

// Load a WAV from disk as mono float at its own rate. Returns nullptr and sets err on failure.
std::shared_ptr<SampleData> load(const std::string& path, std::string& err);
// Same, from bytes in memory.
std::shared_ptr<SampleData> loadMemory(const void* data, size_t size, std::string& err);

// Windowed-sinc resampler (offline; use at load time, never in process()).
std::vector<float> resample(const std::vector<float>& in, double inRate, double outRate);
// Fill s->data at outRate from s->orig.
void prepare(SampleData& s, float outRate);

// Write 16-bit PCM WAV (mono or interleaved stereo). For tests and demos.
bool write16(const std::string& path, const std::vector<float>& interleaved, int channels, int rate, float fullScale = 10.f);

// Keyword matching for "Load kit from folder": voice index for a file name, or -1.
int voiceForFileName(const std::string& name);

} // namespace wav
} // namespace fallen
