#include "Syx4op.hpp"
#include <cstring>
#include <cstdio>

namespace fallen {
namespace syx {

// Operator order inside Yamaha 4-op data: OP4, OP2, OP3, OP1.
static const int ORDER[4] = {3, 1, 2, 0};

static uint8_t cl(int v, int lo, int hi, int* clamped) {
	if (v < lo) { if (clamped) (*clamped)++; return (uint8_t)lo; }
	if (v > hi) { if (clamped) (*clamped)++; return (uint8_t)hi; }
	return (uint8_t)v;
}

uint8_t checksum(const uint8_t* d, size_t n) {
	unsigned s = 0;
	for (size_t i = 0; i < n; i++) s += d[i];
	return (uint8_t)((128 - (s & 127)) & 127);
}

// ---------------------------------------------------------------- VMEM (packed)
void packVmem(const Voice4op& v, uint8_t* o) {
	std::memset(o, 0, VMEM_VOICE);
	for (int j = 0; j < 4; j++) {
		const Op4& p = v.op[ORDER[j]];
		uint8_t* b = o + j * 10;
		b[0] = p.ar & 31; b[1] = p.d1r & 31; b[2] = p.d2r & 31; b[3] = p.rr & 15; b[4] = p.d1l & 15; b[5] = p.ls;
		b[6] = (uint8_t)(((p.ame & 1) << 6) | ((p.ebs & 7) << 3) | (p.kvs & 7));
		b[7] = p.out; b[8] = p.crs & 63;
		b[9] = (uint8_t)(((p.rs & 3) << 3) | (p.det & 7));
	}
	o[40] = (uint8_t)(((v.sy & 1) << 6) | ((v.fb & 7) << 3) | (v.alg & 7));
	o[41] = v.lfs; o[42] = v.lfd; o[43] = v.pmd; o[44] = v.amd;
	o[45] = (uint8_t)(((v.pms & 7) << 4) | ((v.ams & 3) << 2) | (v.lfw & 3));
	o[46] = v.trps; o[47] = v.pbr;
	o[48] = (uint8_t)(((v.chorus & 1) << 4) | ((v.mono & 1) << 3) | ((v.sus & 1) << 2) | ((v.porta & 1) << 1) | (v.porm & 1));
	o[49] = v.port; o[50] = v.fcvol; o[51] = v.mwp; o[52] = v.mwa; o[53] = v.bcp; o[54] = v.bca; o[55] = v.bcpb; o[56] = v.bceb;
	for (int i = 0; i < 10; i++) o[57 + i] = (uint8_t)(v.name[i] & 127);
	for (int i = 0; i < 6; i++) o[67 + i] = v.peg[i];
	if (v.extended) {
		for (int j = 0; j < 4; j++) {
			const Op4& p = v.op[ORDER[j]];
			o[73 + 2 * j] = (uint8_t)(((p.shft & 3) << 4) | ((p.fix & 1) << 3) | (p.fixrg & 7));
			o[74 + 2 * j] = (uint8_t)(((p.osw & 7) << 4) | (p.fine & 15));
		}
		o[81] = v.rev & 7; o[82] = v.fcpitch; o[83] = v.fcamp;
	}
}

Voice4op unpackVmem(const uint8_t* b0, int* c) {
	Voice4op v;
	for (int j = 0; j < 4; j++) {
		Op4& p = v.op[ORDER[j]];
		const uint8_t* b = b0 + j * 10;
		p.ar = cl(b[0], 0, 31, c); p.d1r = cl(b[1], 0, 31, c); p.d2r = cl(b[2], 0, 31, c); p.rr = cl(b[3], 1, 15, c);
		p.d1l = cl(b[4], 0, 15, c); p.ls = cl(b[5], 0, 99, c);
		p.ame = (b[6] >> 6) & 1; p.ebs = (b[6] >> 3) & 7; p.kvs = b[6] & 7;
		p.out = cl(b[7], 0, 99, c); p.crs = cl(b[8], 0, 63, c);
		p.rs = (b[9] >> 3) & 3; p.det = cl(b[9] & 7, 0, 6, c);
	}
	v.sy = (b0[40] >> 6) & 1; v.fb = (b0[40] >> 3) & 7; v.alg = b0[40] & 7;
	v.lfs = cl(b0[41], 0, 99, c); v.lfd = cl(b0[42], 0, 99, c); v.pmd = cl(b0[43], 0, 99, c); v.amd = cl(b0[44], 0, 99, c);
	v.pms = (b0[45] >> 4) & 7; v.ams = (b0[45] >> 2) & 3; v.lfw = b0[45] & 3;
	v.trps = cl(b0[46], 0, 48, c); v.pbr = cl(b0[47], 0, 12, c);
	v.chorus = (b0[48] >> 4) & 1; v.mono = (b0[48] >> 3) & 1; v.sus = (b0[48] >> 2) & 1; v.porta = (b0[48] >> 1) & 1; v.porm = b0[48] & 1;
	v.port = cl(b0[49], 0, 99, c); v.fcvol = cl(b0[50], 0, 99, c); v.mwp = cl(b0[51], 0, 99, c); v.mwa = cl(b0[52], 0, 99, c);
	v.bcp = cl(b0[53], 0, 99, c); v.bca = cl(b0[54], 0, 99, c); v.bcpb = cl(b0[55], 0, 99, c); v.bceb = cl(b0[56], 0, 99, c);
	for (int i = 0; i < 10; i++) v.name[i] = (char)(b0[57 + i] & 127);
	for (int i = 0; i < 6; i++) v.peg[i] = cl(b0[67 + i], 0, 99, c);
	bool ext = false;
	for (int i = 73; i <= 83; i++) if (b0[i]) ext = true;
	v.extended = ext;
	if (ext) {
		for (int j = 0; j < 4; j++) {
			Op4& p = v.op[ORDER[j]];
			uint8_t a = b0[73 + 2 * j], b = b0[74 + 2 * j];
			p.shft = (a >> 4) & 3; p.fix = (a >> 3) & 1; p.fixrg = a & 7;
			p.osw = (b >> 4) & 7; p.fine = b & 15;
		}
		v.rev = b0[81] & 7; v.fcpitch = cl(b0[82], 0, 99, c); v.fcamp = cl(b0[83], 0, 99, c);
	}
	return v;
}

// ---------------------------------------------------------------- VCED (unpacked single voice)
void packVced(const Voice4op& v, uint8_t* o) {
	std::memset(o, 0, VCED_SIZE);
	for (int j = 0; j < 4; j++) {
		const Op4& p = v.op[ORDER[j]];
		uint8_t* b = o + j * 13;
		b[0] = p.ar; b[1] = p.d1r; b[2] = p.d2r; b[3] = p.rr; b[4] = p.d1l; b[5] = p.ls; b[6] = p.rs;
		b[7] = p.ebs; b[8] = p.ame; b[9] = p.kvs; b[10] = p.out; b[11] = p.crs; b[12] = p.det;
	}
	o[52] = v.alg; o[53] = v.fb; o[54] = v.lfs; o[55] = v.lfd; o[56] = v.pmd; o[57] = v.amd; o[58] = v.sy; o[59] = v.lfw;
	o[60] = v.pms; o[61] = v.ams; o[62] = v.trps; o[63] = v.mono; o[64] = v.pbr; o[65] = v.porm; o[66] = v.port; o[67] = v.fcvol;
	o[68] = v.sus; o[69] = v.porta; o[70] = v.chorus; o[71] = v.mwp; o[72] = v.mwa; o[73] = v.bcp; o[74] = v.bca; o[75] = v.bcpb; o[76] = v.bceb;
	for (int i = 0; i < 10; i++) o[77 + i] = (uint8_t)(v.name[i] & 127);
	for (int i = 0; i < 6; i++) o[87 + i] = v.peg[i];
}

Voice4op unpackVced(const uint8_t* b0, int* c) {
	Voice4op v;
	for (int j = 0; j < 4; j++) {
		Op4& p = v.op[ORDER[j]];
		const uint8_t* b = b0 + j * 13;
		p.ar = cl(b[0], 0, 31, c); p.d1r = cl(b[1], 0, 31, c); p.d2r = cl(b[2], 0, 31, c); p.rr = cl(b[3], 1, 15, c);
		p.d1l = cl(b[4], 0, 15, c); p.ls = cl(b[5], 0, 99, c); p.rs = cl(b[6], 0, 3, c); p.ebs = cl(b[7], 0, 7, c);
		p.ame = cl(b[8], 0, 1, c); p.kvs = cl(b[9], 0, 7, c); p.out = cl(b[10], 0, 99, c); p.crs = cl(b[11], 0, 63, c); p.det = cl(b[12], 0, 6, c);
	}
	v.alg = cl(b0[52], 0, 7, c); v.fb = cl(b0[53], 0, 7, c); v.lfs = cl(b0[54], 0, 99, c); v.lfd = cl(b0[55], 0, 99, c);
	v.pmd = cl(b0[56], 0, 99, c); v.amd = cl(b0[57], 0, 99, c); v.sy = cl(b0[58], 0, 1, c); v.lfw = cl(b0[59], 0, 3, c);
	v.pms = cl(b0[60], 0, 7, c); v.ams = cl(b0[61], 0, 3, c); v.trps = cl(b0[62], 0, 48, c); v.mono = cl(b0[63], 0, 1, c);
	v.pbr = cl(b0[64], 0, 12, c); v.porm = cl(b0[65], 0, 1, c); v.port = cl(b0[66], 0, 99, c); v.fcvol = cl(b0[67], 0, 99, c);
	v.sus = cl(b0[68], 0, 1, c); v.porta = cl(b0[69], 0, 1, c); v.chorus = cl(b0[70], 0, 1, c);
	v.mwp = cl(b0[71], 0, 99, c); v.mwa = cl(b0[72], 0, 99, c); v.bcp = cl(b0[73], 0, 99, c); v.bca = cl(b0[74], 0, 99, c);
	v.bcpb = cl(b0[75], 0, 99, c); v.bceb = cl(b0[76], 0, 99, c);
	for (int i = 0; i < 10; i++) v.name[i] = (char)(b0[77 + i] & 127);
	for (int i = 0; i < 6; i++) v.peg[i] = cl(b0[87 + i], 0, 99, c);
	v.extended = false;
	return v;
}

// ---------------------------------------------------------------- ACED (TX81Z additional data)
void packAced(const Voice4op& v, uint8_t* o) {
	std::memset(o, 0, ACED_SIZE);
	for (int j = 0; j < 4; j++) {
		const Op4& p = v.op[ORDER[j]];
		uint8_t* b = o + j * 5;
		b[0] = p.fix; b[1] = p.fixrg; b[2] = p.fine; b[3] = p.osw; b[4] = p.shft;
	}
	o[20] = v.rev; o[21] = v.fcpitch; o[22] = v.fcamp;
}

void applyAced(Voice4op& v, const uint8_t* b0, int* c) {
	for (int j = 0; j < 4; j++) {
		Op4& p = v.op[ORDER[j]];
		const uint8_t* b = b0 + j * 5;
		p.fix = cl(b[0], 0, 1, c); p.fixrg = cl(b[1], 0, 7, c); p.fine = cl(b[2], 0, 15, c); p.osw = cl(b[3], 0, 7, c); p.shft = cl(b[4], 0, 3, c);
	}
	v.rev = cl(b0[20], 0, 7, c); v.fcpitch = cl(b0[21], 0, 99, c); v.fcamp = cl(b0[22], 0, 99, c);
	v.extended = true;
}

// ---------------------------------------------------------------- messages
static const char ACED_ID[] = "LM  8976AE";

std::vector<uint8_t> bankMessage(const Bank4op& b, int ch) {
	std::vector<uint8_t> m = {0xF0, 0x43, (uint8_t)(ch & 15), 0x04, 0x20, 0x00};
	size_t start = m.size();
	m.resize(start + VMEM_SIZE);
	for (int i = 0; i < 32; i++) packVmem(b.v[i], &m[start + i * VMEM_VOICE]);
	m.push_back(checksum(&m[start], VMEM_SIZE));
	m.push_back(0xF7);
	return m;
}

std::vector<uint8_t> voiceMessages(const Voice4op& v, int ch) {
	std::vector<uint8_t> m;
	// ACED: F0 43 0n 7E 00 21 "LM  8976AE" + 23 bytes, checksum over the 33 bytes after the count
	uint8_t a[10 + ACED_SIZE];
	std::memcpy(a, ACED_ID, 10);
	packAced(v, a + 10);
	m.insert(m.end(), {0xF0, 0x43, (uint8_t)(ch & 15), 0x7E, 0x00, 0x21});
	m.insert(m.end(), a, a + sizeof(a));
	m.push_back(checksum(a, sizeof(a)));
	m.push_back(0xF7);
	uint8_t d[VCED_SIZE];
	packVced(v, d);
	m.insert(m.end(), {0xF0, 0x43, (uint8_t)(ch & 15), 0x03, 0x00, 0x5D});
	m.insert(m.end(), d, d + VCED_SIZE);
	m.push_back(checksum(d, VCED_SIZE));
	m.push_back(0xF7);
	return m;
}

std::string LoadResult::summary() const {
	std::string s;
	if (sixOp && !ok()) return "This is a 6-operator bank; FM4 reads 4-operator banks";
	if (hasBank) s = "LOADED 32 VOICES";
	else if (hasVoice) s = "LOADED 1 VOICE";
	else s = "NO 4-OP VOICE DATA FOUND";
	if (checksumBad) s += " (CHECKSUM WARNING)";
	return s;
}

LoadResult parse(const std::vector<uint8_t>& d, bool rawBank) {
	LoadResult r;
	char line[160];
	if (rawBank && d.size() == (size_t)VMEM_SIZE) {
		for (int i = 0; i < 32; i++) r.bank.v[i] = unpackVmem(&d[i * VMEM_VOICE], &r.clamped);
		r.hasBank = true;
		r.messages = r.recognised = 1;
		r.report.push_back("Raw 4096-byte bank read as 32 packed voices.");
	}
	bool pendingAced = false;
	uint8_t aced[ACED_SIZE];
	size_t i = 0;
	while (!rawBank && i < d.size()) {
		if (d[i] != 0xF0) { i++; continue; }
		size_t e = i + 1;
		while (e < d.size() && d[e] != 0xF7) e++;
		if (e >= d.size()) {
			r.report.push_back("A message is cut off at the end of the file (ignored).");
			break;
		}
		const uint8_t* m = &d[i];
		size_t len = e - i + 1;
		r.messages++;
		auto checkSum = [&](const uint8_t* p, size_t n, uint8_t cs, const char* what) {
			if (checksum(p, n) != cs) {
				r.checksumBad = true;
				std::snprintf(line, sizeof(line), "%s: checksum does not match (loaded anyway).", what);
				r.report.push_back(line);
			}
		};
		if (len >= 6 && m[1] == 0x43) {
			uint8_t f = m[3];
			if (f == 0x04 && len == 6 + VMEM_SIZE + 2 && m[4] == 0x20 && m[5] == 0x00) {
				const uint8_t* p = m + 6;
				checkSum(p, VMEM_SIZE, m[6 + VMEM_SIZE], "32-voice bank");
				for (int k = 0; k < 32; k++) r.bank.v[k] = unpackVmem(p + k * VMEM_VOICE, &r.clamped);
				r.hasBank = true;
				r.recognised++;
				r.report.push_back("32-voice bank (VMEM) loaded.");
			} else if (f == 0x03 && len == 6 + VCED_SIZE + 2 && m[5] == 0x5D) {
				const uint8_t* p = m + 6;
				checkSum(p, VCED_SIZE, m[6 + VCED_SIZE], "Single voice");
				r.voice = unpackVced(p, &r.clamped);
				if (pendingAced) applyAced(r.voice, aced, &r.clamped);
				pendingAced = false;
				r.hasVoice = true;
				r.recognised++;
				r.report.push_back(std::string("Single voice (VCED) '") + r.voice.nameStr() + "' loaded" + (r.voice.extended ? " with extended data." : "."));
			} else if (f == 0x7E && len >= 6 + 10 + ACED_SIZE + 2 && std::memcmp(m + 6, ACED_ID, 10) == 0) {
				checkSum(m + 6, 10 + ACED_SIZE, m[6 + 10 + ACED_SIZE], "Additional voice data");
				std::memcpy(aced, m + 16, ACED_SIZE);
				pendingAced = true;
				if (r.hasVoice) { applyAced(r.voice, aced, &r.clamped); pendingAced = false; }
				r.recognised++;
				r.report.push_back("Additional voice data (ACED) read.");
			} else if (f == 0x09 && m[4] == 0x20 && m[5] == 0x00) {
				r.sixOp = true;
				r.report.push_back("6-operator 32-voice bank: not supported.");
			} else if (f == 0x00 && m[5] == 0x9B) {
				r.sixOp = true;
				r.report.push_back("6-operator single voice: not supported.");
			} else if (f == 0x7E && len > 16) {
				std::string id((const char*)m + 6, 10);
				r.report.push_back("Ignored: " + id + " data (performance, system or tuning).");
			} else {
				std::snprintf(line, sizeof(line), "Ignored: Yamaha message type %02X, %zu bytes.", f, len);
				r.report.push_back(line);
			}
		} else {
			std::snprintf(line, sizeof(line), "Ignored: non-Yamaha message, %zu bytes.", len);
			r.report.push_back(line);
		}
		i = e + 1;
	}
	if (r.clamped) {
		std::snprintf(line, sizeof(line), "%d out-of-range values were clamped.", r.clamped);
		r.report.push_back(line);
	}
	if (r.sixOp && !r.ok()) r.report.push_back("This is a 6-operator bank; FM4 reads 4-operator banks.");
	if (r.messages == 0) r.report.push_back("No SysEx messages found in the file.");
	return r;
}

} // namespace syx
} // namespace fallen
