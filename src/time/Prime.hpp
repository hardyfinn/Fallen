// PRIME: four loops of prime lengths, each optionally nested in the row above.
#pragma once
#include "Clock.hpp"

namespace fallen {

struct Prime {
	static constexpr int NP = 11;
	static constexpr int PRIMES[NP] = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31};
	struct Row {
		int len = 3, hits = 1, rot = 0;
		bool nest = false;
		bool operator!=(const Row& o) const { return len != o.len || hits != o.hits || rot != o.rot || nest != o.nest; }
	};
	Row rows[4];
	bool pat[4][32] = {};
	int pos[4] = {0, 0, 0, 0};
	Row built[4];
	bool dirty = true;

	Prime() {
		rows[0].len = 3; rows[1].len = 5; rows[2].len = 7; rows[3].len = 11;
		for (auto& r : rows) r.hits = 2;
	}

	void setRows(const Row* r) {
		for (int i = 0; i < 4; i++) {
			Row x = r[i];
			x.len = clampi(x.len, 1, 31);
			x.hits = clampi(x.hits, 1, x.len);
			if (i == 0) x.nest = false;
			if (x != built[i] || dirty) {
				rows[i] = x;
				euclid(pat[i], x.len, x.hits, x.rot);
				built[i] = x;
				pos[i] %= x.len;
			}
		}
		dirty = false;
	}

	void reset() { for (int& p : pos) p = 0; }

	bool aligned() const { return pos[0] == 0 && pos[1] == 0 && pos[2] == 0 && pos[3] == 0; }

	// One pulse. ev[i] = row i plays a hit. Returns true if all rows were at step 0 (realign).
	bool pulse(bool* ev) {
		bool re = aligned();
		for (int i = 0; i < 4; i++) {
			bool adv = !rows[i].nest || (i > 0 && ev[i - 1]);
			ev[i] = false;
			if (adv) {
				ev[i] = pat[i][pos[i]];
				pos[i] = (pos[i] + 1) % rows[i].len;
			}
		}
		return re;
	}

	// Pulses until the next realignment (0 = the next pulse realigns), or -1 beyond the limit.
	int pulsesToRealign(int limit = 100000) const {
		Prime c = *this;
		bool ev[4];
		for (int n = 0; n <= limit; n++) {
			if (c.aligned()) return n;
			c.pulse(ev);
		}
		return -1;
	}
};

} // namespace fallen
