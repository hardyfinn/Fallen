// Deterministic hashing: everything that should look varied but repeat exactly uses this.
#pragma once
#include <cstdint>

namespace fallen {

inline uint32_t hash32(uint32_t x) {
	// SplitMix32-style finaliser (lowbias32).
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return x;
}

// Uniform in [0, 1). Same inputs give the same value on every platform.
inline float hash01(uint32_t seed, uint32_t a, uint32_t b = 0) {
	uint32_t h = hash32(seed ^ (a * 0x9E3779B9U) ^ hash32(b * 0x85EBCA6BU + 0x632BE5ABU));
	return (float)(h >> 8) * (1.f / 16777216.f);
}

} // namespace fallen
