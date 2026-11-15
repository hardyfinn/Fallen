// CHORD: chord shapes as semitone sets, with inversion and voicing.
#pragma once
#include <cstdint>
#include <algorithm>
#include "Common.hpp"

namespace fallen {

struct Chords {
	static constexpr int KEYS = 25;  // offsets 0..24 on the two-octave keyboard
	static constexpr int MAXN = 6;
	enum Voicing { CLOSE = 0, DROP2 = 1, SPREAD = 2 };

	static uint32_t mask(std::initializer_list<int> notes) {
		uint32_t m = 0;
		for (int n : notes) m |= 1u << n;
		return m;
	}
	static void factory(uint32_t* slots) {
		slots[0] = mask({0, 3, 7, 10});            // m7
		slots[1] = mask({0, 3, 7, 10, 14});        // m9
		slots[2] = mask({0, 3, 7, 10, 14, 17});    // m11
		slots[3] = mask({0, 4, 7, 11});            // maj7
		slots[4] = mask({0, 4, 7, 11, 14});        // maj9
		slots[5] = mask({0, 5, 7, 10});            // sus4 (7sus4)
		slots[6] = mask({0, 5, 10, 15});           // quartal
		slots[7] = mask({0, 4, 7, 10});            // dominant 7
	}
	static constexpr const char* FACTORY_NAMES[8] = {"m7", "m9", "m11", "maj7", "maj9", "sus4", "quartal", "7"};

	// Notes (semitones) of a slot after inversion and voicing, sorted. Returns the count (0..6).
	static int notes(uint32_t m, int invert, int voicing, float* out) {
		int n = 0;
		int tmp[MAXN];
		for (int k = 0; k < KEYS && n < MAXN; k++) if (m & (1u << k)) tmp[n++] = k;
		if (n == 0) return 0;
		std::sort(tmp, tmp + n);
		for (int i = 0; i < clampi(invert, 0, 5); i++) {
			tmp[0] += 12;
			std::sort(tmp, tmp + n);
		}
		if (voicing == DROP2 && n >= 2) {
			tmp[n - 2] -= 12;
			std::sort(tmp, tmp + n);
		} else if (voicing == SPREAD) {
			for (int i = 1; i < n; i += 2) tmp[i] += 12;
			std::sort(tmp, tmp + n);
		}
		for (int i = 0; i < n; i++) out[i] = (float)tmp[i];
		return n;
	}
};

} // namespace fallen
