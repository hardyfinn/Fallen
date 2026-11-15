// Reading and writing 4-operator Yamaha voice data (.syx): 32-voice VMEM bulk, single VCED, TX81Z ACED.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../dsp/Fm4Voice.hpp"

namespace fallen {
namespace syx {

constexpr int VMEM_VOICE = 128;
constexpr int VMEM_SIZE = 4096;
constexpr int VCED_SIZE = 93;
constexpr int ACED_SIZE = 23;

// Packed 128-byte voice (VMEM) <-> voice.
void packVmem(const Voice4op& v, uint8_t* out128);
Voice4op unpackVmem(const uint8_t* in128, int* clamped = nullptr);
// Unpacked single voice (VCED, 93 bytes) and additional data (ACED, 23 bytes).
void packVced(const Voice4op& v, uint8_t* out93);
Voice4op unpackVced(const uint8_t* in93, int* clamped = nullptr);
void packAced(const Voice4op& v, uint8_t* out23);
void applyAced(Voice4op& v, const uint8_t* in23, int* clamped = nullptr);

uint8_t checksum(const uint8_t* data, size_t n);

// Complete SysEx messages.
std::vector<uint8_t> bankMessage(const Bank4op& b, int channel = 0);
std::vector<uint8_t> voiceMessages(const Voice4op& v, int channel = 0);  // ACED then VCED

struct LoadResult {
	bool hasBank = false;
	Bank4op bank;
	bool hasVoice = false;
	Voice4op voice;
	bool sixOp = false;
	bool checksumBad = false;
	int clamped = 0;
	int messages = 0, recognised = 0;
	std::vector<std::string> report;
	std::string summary() const;
	bool ok() const { return hasBank || hasVoice; }
};

// Parse a whole file. rawBank: treat a header-less 4096-byte file as VMEM.
LoadResult parse(const std::vector<uint8_t>& data, bool rawBank = false);

} // namespace syx
} // namespace fallen
