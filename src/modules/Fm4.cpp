// FM4: four-operator FM voice that loads 4-op Yamaha SysEx banks.
#include "Common.hpp"
#include "../dsp/Fm4Voice.hpp"
#include "../dsp/FactoryBank.hpp"
#include "../io/Syx4op.hpp"
#include <atomic>

using fallen::Voice4op;
using fallen::Bank4op;

// A bank or voice change prepared on the UI thread and applied at the top of process().
struct Fm4Job {
	enum Kind { BANK, VOICE } kind = BANK;
	Bank4op bank;       // BANK: new working bank
	bool setOriginal = true;
	Voice4op voice;     // VOICE: new voice for `slot`
	int slot = 0;
};

struct CrsQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		int c = math::clamp((int)std::lround(getValue()), 0, 63);
		return string::f("%d (ratio %.2f)", c, fallen::fm4::RATIO[c]);
	}
};
struct WaveQuantity : ParamQuantity {
	std::string getDisplayValueString() override { return string::f("W%d", (int)std::lround(getValue())); }
};

struct FM4Module : Module, FM4Ids {
	Bank4op bank, orig;
	fallen::Fm4Engine engine;
	std::atomic<Fm4Job*> pending{nullptr}, done{nullptr};

	int maxVoices = 8;
	int selOp = 0;        // 0 = operator 1
	int editVoice = 0;    // the VOICE knob's voice, which the edit section changes
	int syncedVoice = -1, syncedOp = -1;
	int last[PARAMS_LEN];
	bool resync = true;

	dsp::SchmittTrigger gateIn[8], stepResetIn, opBtn[4];
	bool gateHigh[8] = {};
	int stepIdx = 0;
	int playing[8] = {};  // voice each channel started with
	int lastPlayed = 0;
	fallen::Fm4Macros macros[8];
	float spread[8] = {};
	dsp::ClockDivider editDiv;

	// Shown on the display for a few seconds after a load.
	std::string message;
	float messageTime = 0.f;
	std::vector<std::string> report = {"Nothing loaded yet."};
	std::string lastDir;

	FM4Module() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configInt(this, VOICE_PARAM, 1, 32, 1, "VOICE (voice played and edited)");
		configSwitch(STEP_PARAM, 0.f, 1.f, 0.f, "STEP (next voice on each new note)", {"Off", "On"});
		configInt(this, FIRST_PARAM, 1, 32, 1, "FIRST voice of the STEP range");
		configInt(this, COUNT_PARAM, 1, 32, 3, "COUNT of voices in the STEP range");
		configParam(LEVEL_PARAM, 0.f, 2.f, 1.f, "LEVEL", " dB", -10.f, 20.f);
		configParam(BRIGHT_PARAM, -20.f, 20.f, 0.f, "BRIGHT (modulator levels, steps)");
		configParam(RATIO_PARAM, -8.f, 8.f, 0.f, "RATIO (modulator ratio table steps)");
		configParam(DECAY_PARAM, -10.f, 10.f, 0.f, "DECAY (rate steps, + = longer)");
		configParam(ATTACK_PARAM, -10.f, 10.f, 0.f, "ATTACK (rate steps, + = slower)");
		configParam(FDBK_PARAM, -7.f, 7.f, 0.f, "FDBK (feedback steps)");
		configParam(SPREAD_PARAM, 0.f, 20.f, 0.f, "SPREAD (detune between voices)", " cents");
		for (int i = 0; i < 4; i++) {
			configButton(OP1_PARAM + i, string::f("Edit operator %d", i + 1));
			configLight(OP1_LIGHT + i, string::f("Operator %d chosen", i + 1));
		}
		configInt(this, ALG_PARAM, 1, 8, 1, "ALG (algorithm)");
		configInt(this, FB_PARAM, 0, 7, 0, "FB (operator 4 feedback)");
		configParam<CrsQuantity>(CRS_PARAM, 0.f, 63.f, 4.f, "CRS (coarse ratio)")->snapEnabled = true;
		configInt(this, FINE_PARAM, 0, 15, 0, "FINE (fine ratio)");
		configInt(this, OPLEVEL_PARAM, 0, 99, 99, "LEVEL (operator output level)");
		configParam<WaveQuantity>(WAVE_PARAM, 1.f, 8.f, 1.f, "WAVE (operator waveform)")->snapEnabled = true;
		configInt(this, DET_PARAM, -3, 3, 0, "DET (detune)");
		configInt(this, AR_PARAM, 0, 31, 31, "AR (attack rate)");
		configInt(this, D1R_PARAM, 0, 31, 0, "D1R (first decay rate)");
		configInt(this, D1L_PARAM, 0, 15, 15, "D1L (first decay level)");
		configInt(this, D2R_PARAM, 0, 31, 0, "D2R (second decay rate, 0 = hold)");
		configInt(this, RR_PARAM, 1, 15, 7, "RR (release rate)");
		const char* mac[5] = {"BRIGHT", "RATIO", "DECAY", "ATTACK", "FDBK"};
		for (int i = 0; i < 5; i++) configInput(BRIGHT_INPUT + i, string::f("%s CV (±5 V = full range, polyphonic)", mac[i]));
		configInput(VOCT_INPUT, "V/OCT (channels = voices, up to 8)");
		configInput(GATE_INPUT, "GATE (polyphonic)");
		configInput(VEL_INPUT, "VEL (0 to 10 V, polyphonic)");
		configInput(VOICE_INPUT, "VOICE CV (0.1 V per voice, polyphonic)");
		configInput(STEPRESET_INPUT, "STEP RESET");
		configOutput(OUT_OUTPUT, "OUT (mono sum)");
		configOutput(POLY_OUTPUT, "POLY OUT (one channel per voice)");
		// The edit knobs mirror the voice data; randomising them would rewrite the voice.
		for (int id = ALG_PARAM; id <= RR_PARAM; id++) getParamQuantity(id)->randomizeEnabled = false;
		bank = fallen::factoryBank();
		orig = bank;
		engine.bank = &bank;
		editDiv.setDivision(32);
		for (int i = 0; i < PARAMS_LEN; i++) last[i] = 0;
		onSampleRateChange();
	}
	~FM4Module() {
		delete pending.exchange(nullptr);
		delete done.exchange(nullptr);
	}

	void onSampleRateChange() override {
		engine.setSampleRate(APP->engine->getSampleRate());
	}
	void onReset() override {
		bank = fallen::factoryBank();
		orig = bank;
		selOp = 0;
		stepIdx = 0;
		engine.reset();
		resync = true;
	}

	// UI thread: queue a change.
	void post(Fm4Job* j) {
		delete done.exchange(nullptr);
		delete pending.exchange(j);
	}
	void showMessage(const std::string& s) {
		message = s;
		messageTime = 4.f;
	}

	// ---- edit section: read and write the chosen operator of the edited voice
	int readEdit(int id) {
		const Voice4op& v = bank.v[editVoice];
		const fallen::Op4& o = v.op[selOp];
		switch (id) {
			case ALG_PARAM: return v.alg + 1;
			case FB_PARAM: return v.fb;
			case CRS_PARAM: return o.crs;
			case FINE_PARAM: return o.fine;
			case OPLEVEL_PARAM: return o.out;
			case WAVE_PARAM: return o.osw + 1;
			case DET_PARAM: return (int)o.det - 3;
			case AR_PARAM: return o.ar;
			case D1R_PARAM: return o.d1r;
			case D1L_PARAM: return o.d1l;
			case D2R_PARAM: return o.d2r;
			case RR_PARAM: return o.rr;
		}
		return 0;
	}
	void writeEdit(int id, int x) {
		Voice4op& v = bank.v[editVoice];
		fallen::Op4& o = v.op[selOp];
		auto u = [](int a, int lo, int hi) { return (uint8_t)math::clamp(a, lo, hi); };
		switch (id) {
			case ALG_PARAM: v.alg = u(x - 1, 0, 7); break;
			case FB_PARAM: v.fb = u(x, 0, 7); break;
			case CRS_PARAM: o.crs = u(x, 0, 63); break;
			case FINE_PARAM: o.fine = u(x, 0, 15); v.extended = true; break;
			case OPLEVEL_PARAM: o.out = u(x, 0, 99); break;
			case WAVE_PARAM: o.osw = u(x - 1, 0, 7); v.extended = true; break;
			case DET_PARAM: o.det = u(x + 3, 0, 6); break;
			case AR_PARAM: o.ar = u(x, 0, 31); break;
			case D1R_PARAM: o.d1r = u(x, 0, 31); break;
			case D1L_PARAM: o.d1l = u(x, 0, 15); break;
			case D2R_PARAM: o.d2r = u(x, 0, 31); break;
			case RR_PARAM: o.rr = u(x, 1, 15); break;
		}
	}
	void syncKnobs() {
		for (int id = ALG_PARAM; id <= RR_PARAM; id++) {
			last[id] = readEdit(id);
			params[id].setValue((float)last[id]);
		}
		syncedVoice = editVoice;
		syncedOp = selOp;
		resync = false;
	}
	void editStep() {
		editVoice = math::clamp(intVal(this, VOICE_PARAM) - 1, 0, 31);
		if (resync || editVoice != syncedVoice || selOp != syncedOp) {
			syncKnobs();
			return;
		}
		for (int id = ALG_PARAM; id <= RR_PARAM; id++) {
			int x = intVal(this, id);
			if (x != last[id]) {
				writeEdit(id, x);
				last[id] = x;
			}
		}
	}

	int chooseVoice(int c) {
		if (params[STEP_PARAM].getValue() > 0.5f) {
			int first = intVal(this, FIRST_PARAM) - 1, count = std::max(1, intVal(this, COUNT_PARAM));
			int v = (first + stepIdx % count) % 32;
			stepIdx = (stepIdx + 1) % count;
			return v;
		}
		int v = intVal(this, VOICE_PARAM) - 1;
		if (inputs[VOICE_INPUT].isConnected()) v += (int)std::lround(inputs[VOICE_INPUT].getPolyVoltage(c) * 10.f);
		return math::clamp(v, 0, 31);
	}

	void process(const ProcessArgs& args) override {
		if (Fm4Job* j = pending.exchange(nullptr)) {
			if (j->kind == Fm4Job::BANK) {
				bank = j->bank;
				if (j->setOriginal) orig = j->bank;
			} else {
				bank.v[j->slot] = j->voice;
				orig.v[j->slot] = j->voice;
			}
			resync = true;
			delete done.exchange(j);
		}
		for (int i = 0; i < 4; i++)
			if (opBtn[i].process(params[OP1_PARAM + i].getValue())) selOp = i;
		if (editDiv.process()) editStep();
		if (messageTime > 0.f) messageTime -= args.sampleTime;

		if (stepResetIn.process(inputs[STEPRESET_INPUT].getVoltage(), 0.1f, 1.f)) stepIdx = 0;

		int nch = std::max({1, inputs[VOCT_INPUT].getChannels(), inputs[GATE_INPUT].getChannels()});
		nch = std::min(nch, maxVoices);

		float spreadC = params[SPREAD_PARAM].getValue();
		float pitch[8];
		const float range[5] = {20.f, 8.f, 10.f, 10.f, 7.f};
		for (int c = 0; c < nch; c++) {
			pitch[c] = inputs[VOCT_INPUT].getPolyVoltage(c);
			float mv[5];
			for (int k = 0; k < 5; k++) {
				mv[k] = params[BRIGHT_PARAM + k].getValue();
				if (inputs[BRIGHT_INPUT + k].isConnected()) mv[k] += inputs[BRIGHT_INPUT + k].getPolyVoltage(c) / 5.f * range[k];
				mv[k] = math::clamp(mv[k], -range[k], range[k]);
			}
			macros[c].bright = mv[0];
			macros[c].ratio = mv[1];
			macros[c].decay = mv[2];
			macros[c].attack = mv[3];
			macros[c].fdbk = mv[4];
			spread[c] = nch > 1 ? spreadC * (2.f * fallen::hash01(29, c) - 1.f) : 0.f;

			float g = inputs[GATE_INPUT].getPolyVoltage(c);
			if (gateIn[c].process(g, 0.1f, 1.f)) {
				int v = chooseVoice(c);
				float vel = inputs[VEL_INPUT].isConnected() ? math::clamp(inputs[VEL_INPUT].getPolyVoltage(c) / 10.f, 0.f, 1.f) : 1.f;
				playing[c] = lastPlayed = v;
				engine.noteOn(c, v, pitch[c], vel, macros[c]);
				gateHigh[c] = true;
			} else if (gateHigh[c] && !gateIn[c].isHigh()) {
				engine.noteOff(c);
				gateHigh[c] = false;
			}
		}
		// Channels above the current count are released.
		for (int c = nch; c < 8; c++) {
			if (gateHigh[c]) { engine.noteOff(c); gateHigh[c] = false; }
			pitch[c] = 0.f;
			spread[c] = 0.f;
			gateIn[c].reset();
		}

		float out[8];
		engine.process(8, pitch, macros, spread, out);
		float lvl = params[LEVEL_PARAM].getValue();
		float sum = 0.f;
		for (int c = 0; c < 8; c++) sum += out[c];
		outputs[OUT_OUTPUT].setVoltage(sum * lvl);
		outputs[POLY_OUTPUT].setChannels(nch);
		for (int c = 0; c < nch; c++) outputs[POLY_OUTPUT].setVoltage(out[c] * lvl, c);
		for (int i = 0; i < 4; i++) lights[OP1_LIGHT + i].setBrightness(i == selOp ? 1.f : 0.f);
	}

	// ---- patch storage: the bank and the originals as base64 VMEM images
	static std::string bankB64(const Bank4op& b) {
		std::vector<uint8_t> img(fallen::syx::VMEM_SIZE);
		for (int i = 0; i < 32; i++) fallen::syx::packVmem(b.v[i], img.data() + i * fallen::syx::VMEM_VOICE);
		return string::toBase64(img);
	}
	static bool bankFromB64(const char* s, Bank4op& b) {
		std::vector<uint8_t> img = string::fromBase64(s);
		if (img.size() != (size_t)fallen::syx::VMEM_SIZE) return false;
		for (int i = 0; i < 32; i++) b.v[i] = fallen::syx::unpackVmem(img.data() + i * fallen::syx::VMEM_VOICE);
		return true;
	}
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "bank", json_string(bankB64(bank).c_str()));
		json_object_set_new(root, "original", json_string(bankB64(orig).c_str()));
		json_object_set_new(root, "maxVoices", json_integer(maxVoices));
		json_object_set_new(root, "op", json_integer(selOp));
		json_object_set_new(root, "lastDir", json_string(lastDir.c_str()));
		return root;
	}
	void dataFromJson(json_t* root) override {
		json_t* j = json_object_get(root, "bank");
		if (j && json_is_string(j)) bankFromB64(json_string_value(j), bank);
		j = json_object_get(root, "original");
		if (!(j && json_is_string(j) && bankFromB64(json_string_value(j), orig))) orig = bank;
		maxVoices = math::clamp(jsonInt(root, "maxVoices", 8), 1, 8);
		selOp = math::clamp(jsonInt(root, "op", 0), 0, 3);
		j = json_object_get(root, "lastDir");
		if (j && json_is_string(j)) lastDir = json_string_value(j);
		resync = true;
	}

	// ---- menu actions (UI thread)
	void loadFile(bool raw) {
		std::string path = fileDialog(FileDialog::OPEN, lastDir, "", raw ? nullptr : "SysEx:syx,SYX,mid,MID");
		if (path.empty()) return;
		lastDir = system::getDirectory(path);
		std::vector<uint8_t> data;
		try {
			data = system::readFile(path);
		} catch (Exception& e) {
			report = {"Could not read " + path};
			showMessage("COULD NOT READ FILE");
			return;
		}
		fallen::syx::LoadResult r = fallen::syx::parse(data, raw);
		std::string fname = system::getFilename(path);
		report = r.report;
		report.insert(report.begin(), "File: " + fname);
		if (!r.ok()) {
			showMessage(r.sixOp ? "6-OP BANK: FM4 READS 4-OP" : "NO 4-OP VOICE DATA FOUND");
			return;
		}
		Fm4Job* job = new Fm4Job;
		if (r.hasBank) {
			job->kind = Fm4Job::BANK;
			job->bank = r.bank;
			showMessage(std::string(r.checksumBad ? "LOADED 32 (BAD CHECKSUM) " : "LOADED 32 VOICES ") + fname);
		} else {
			job->kind = Fm4Job::VOICE;
			job->voice = r.voice;
			job->slot = editVoice;
			showMessage(string::f("LOADED VOICE INTO %d ", editVoice + 1) + fname);
		}
		post(job);
	}
	void saveBank() {
		std::string path = fileDialog(FileDialog::SAVE, lastDir, "fm4-bank.syx", "SysEx:syx");
		if (path.empty()) return;
		if (system::getExtension(path).empty()) path += ".syx";
		lastDir = system::getDirectory(path);
		try {
			system::writeFile(path, fallen::syx::bankMessage(bank));
			showMessage("SAVED BANK " + system::getFilename(path));
		} catch (Exception& e) {
			showMessage("COULD NOT SAVE");
		}
	}
	void saveVoice() {
		const Voice4op v = bank.v[editVoice];
		std::string name = v.nameStr();
		for (char& ch : name) if (ch == ' ' || ch == '/' || ch == '.') ch = '_';
		std::string path = fileDialog(FileDialog::SAVE, lastDir, name + ".syx", "SysEx:syx");
		if (path.empty()) return;
		if (system::getExtension(path).empty()) path += ".syx";
		lastDir = system::getDirectory(path);
		try {
			system::writeFile(path, fallen::syx::voiceMessages(v));
			showMessage("SAVED VOICE " + system::getFilename(path));
		} catch (Exception& e) {
			showMessage("COULD NOT SAVE");
		}
	}
	void revertVoice() {
		Fm4Job* job = new Fm4Job;
		job->kind = Fm4Job::VOICE;
		job->slot = editVoice;
		job->voice = orig.v[editVoice];
		post(job);
		showMessage(string::f("REVERTED VOICE %d", editVoice + 1));
	}
	void factory() {
		Fm4Job* job = new Fm4Job;
		job->bank = fallen::factoryBank();
		post(job);
		report = {"Factory bank loaded."};
		showMessage("FACTORY BANK");
	}
};

// Name, algorithm and the chosen operator's values.
struct Fm4Screen : FallenScreen {
	FM4Module* m = nullptr;
	void drawContents(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		float W = box.size.x;
		if (!m) {
			text(vg, 4, 4, "1 HOUSE ORG", 13.f);
			text(vg, 4, 22, "ALG 8  4+3+2+1", 9.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, ink(0.7f));
			return;
		}
		const Voice4op& v = m->bank.v[m->editVoice];
		const fallen::Op4& o = v.op[m->selOp];
		text(vg, 4, 3, string::f("%d %s", m->editVoice + 1, v.nameStr().c_str()), 13.f);
		if (m->params[FM4Ids::STEP_PARAM].getValue() > 0.5f) {
			int first = intVal(m, FM4Ids::FIRST_PARAM), count = intVal(m, FM4Ids::COUNT_PARAM);
			int lastV = (first - 1 + count - 1) % 32 + 1;
			text(vg, W - 4, 4, string::f("STEP %d-%d  >%d", first, lastV, m->lastPlayed + 1), 9.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, accent());
		}
		text(vg, 4, 21, string::f("ALG %d  %s  FB %d%s", v.alg + 1, fallen::fm4::ALG_TEXT[v.alg & 7], v.fb, v.extended ? "  EXT" : ""), 9.f,
		     NVG_ALIGN_LEFT | NVG_ALIGN_TOP, ink(0.8f));
		std::string freq = o.fix ? string::f("FIX %dHz", std::max(1, (16 * (o.crs >> 2) + o.fine) * (1 << (o.fixrg & 7))))
		                         : string::f("x%.2f", fallen::fm4::RATIO[o.crs & 63]);
		bool car = fallen::fm4::isCarrier(v.alg, m->selOp);
		text(vg, 4, 33, string::f("OP%d %s %s LV%d W%d DET%+d", m->selOp + 1, car ? "CAR" : "MOD", freq.c_str(), o.out, o.osw + 1, (int)o.det - 3), 9.f,
		     NVG_ALIGN_LEFT | NVG_ALIGN_TOP, accent());
		text(vg, 4, 45, string::f("AR%d D1R%d D1L%d D2R%d RR%d", o.ar, o.d1r, o.d1l, o.d2r, o.rr), 9.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, ink(0.8f));
		if (m->messageTime > 0.f) {
			rect(vg, 0, box.size.y - 13, W, 13, nvgRGB(0x0d, 0x0d, 0x0d));
			text(vg, 4, box.size.y - 12, m->message, 9.f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, accent());
		}
	}
};

struct FM4Widget : ModuleWidget {
	FM4Widget(FM4Module* m) {
		fallenScaffold(this, m, "FM4");
		placeFM4<FM4Module>(this, m);
		auto* d = new Fm4Screen;
		d->m = m;
		d->setRectMm(FM4_SCREEN_MM);
		addChild(d);
	}
	void step() override {
		if (FM4Module* m = dynamic_cast<FM4Module*>(module)) delete m->done.exchange(nullptr);
		ModuleWidget::step();
	}
	void appendContextMenu(Menu* menu) override {
		FM4Module* m = dynamic_cast<FM4Module*>(module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("SysEx (4-operator Yamaha banks and voices)"));
		menu->addChild(createMenuItem("Load .syx…", "", [=]() { m->loadFile(false); }));
		menu->addChild(createMenuItem("Load raw 4096-byte bank…", "", [=]() { m->loadFile(true); }));
		menu->addChild(createMenuItem("Save bank as .syx…", "", [=]() { m->saveBank(); }));
		menu->addChild(createMenuItem("Save voice as .syx…", "", [=]() { m->saveVoice(); }));
		menu->addChild(createMenuItem("Revert voice", "", [=]() { m->revertVoice(); }));
		menu->addChild(createMenuItem("Load factory bank", "", [=]() { m->factory(); }));
		menu->addChild(createSubmenuItem("Last load report", "", [=](Menu* sub) {
			for (const std::string& line : m->report) sub->addChild(createMenuLabel(line));
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Voices", {"1", "2", "3", "4", "5", "6", "7", "8"},
		                                      [=]() { return m->maxVoices - 1; }, [=](int i) { m->maxVoices = i + 1; }));
		menu->addChild(createMenuLabel("Voice allocation: by channel"));
	}
};

Model* modelFM4 = createModel<FM4Module, FM4Widget>("FM4");
