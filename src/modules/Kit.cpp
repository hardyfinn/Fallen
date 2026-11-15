// KIT: six drum voices, synthesised (EARLY, ANALOG) or played from WAV files.
#include "Common.hpp"
#include "../dsp/KitVoices.hpp"
#include "../io/WavLoad.hpp"
#include <atomic>
#include <mutex>
#include <thread>

using fallen::SampleData;
static const char* KIT_NAMES[6] = {"KICK", "SNARE", "CLAP", "C.HAT", "O.HAT", "PERC"};

// Finished loads from worker threads. Shared with the threads so it outlives the module if needed.
struct KitLoader {
	struct Result {
		int voice = 0, gen = 0;
		std::shared_ptr<SampleData> data;
		std::string err, path;
		bool switchSrc = false;
	};
	std::mutex mx;
	std::vector<Result> results;
};

struct KITModule : Module, KITIds {
	fallen::DrumSynth synth[6];
	fallen::SamplePlayer player[6];
	fallen::Crunch crunch[6];
	dsp::SchmittTrigger trig[6];
	bool lastEarly[6] = {};
	float hit[6] = {};

	// Samples: `live` is what process() reads; `holder` owns it (UI thread only).
	std::atomic<SampleData*> live[6];
	std::shared_ptr<SampleData> holder[6];
	std::vector<std::pair<std::shared_ptr<SampleData>, double>> graveyard;
	std::shared_ptr<KitLoader> loader = std::make_shared<KitLoader>();
	int gen[6] = {};
	bool loading[6] = {};
	std::string path[6];      // the file this voice plays, kept when missing so relinking is easy
	bool missing[6] = {};
	std::string error[6];

	// options
	bool storeSamples = false, crunchSamples = false, choke = true, removeFromMix = false, kickDrive = false;
	int snappy = 1;  // 0.3, 0.6, 0.9
	std::string lastDir;
	std::vector<std::string> report = {"Nothing loaded yet."};

	KITModule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < 6; i++) {
			std::string n = KIT_NAMES[i];
			configSwitch(SRC1_PARAM + i, 0.f, 2.f, i == 0 ? 1.f : 0.f, n + " source", {"EARLY", "ANALOG", "SAMPLE"});
			configParam(TUNE1_PARAM + i, -24.f, 24.f, 0.f, n + " TUNE", " semitones");
			configParam(DECAY1_PARAM + i, 0.f, 1.f, fallen::expNorm(1.f, 0.1f, 2.f), n + " DECAY (× natural decay; samples play in full at 1× and above)", "×", 20.f, 0.1f);
			configParam(LEVEL1_PARAM + i, 0.f, 2.f, 1.f, n + " LEVEL", " dB", -10.f, 20.f);
			configInput(TRIG1_INPUT + i, n + " TRIG (voltage at the edge sets velocity: 10 V = full)");
			configInput(TUNE1_INPUT + i, n + " TUNE CV (1 V/oct)");
			configOutput(OUT1_OUTPUT + i, n + " OUT");
			live[i].store(nullptr);
		}
		configParam(CRUNCH_PARAM, 0.f, 1.f, 0.5f, "CRUNCH (EARLY character: 0 = clean, 0.5 = 8-bit 28 kHz, 1 = 6-bit 18 kHz)", "%", 0.f, 100.f);
		configInput(ACCENT_INPUT, "ACCENT (gate: +4 dB while high)");
		configOutput(MIX_OUTPUT, "MIX");
		onSampleRateChange();
	}

	void onSampleRateChange() override {
		float sr = APP->engine->getSampleRate();
		for (int i = 0; i < 6; i++) synth[i].setup(i, sr);
		// re-resample loaded samples from their originals
		for (int i = 0; i < 6; i++)
			if (holder[i]) startJob(i, holder[i]->path, holder[i], false);
	}

	// ---- loading (UI thread)
	void startJob(int voice, const std::string& p, std::shared_ptr<SampleData> base, bool switchSrc) {
		int g = ++gen[voice];
		loading[voice] = true;
		error[voice].clear();
		float rate = APP->engine->getSampleRate();
		std::shared_ptr<KitLoader> L = loader;
		std::thread([=]() {
			KitLoader::Result r;
			r.voice = voice;
			r.gen = g;
			r.path = p;
			r.switchSrc = switchSrc;
			std::shared_ptr<SampleData> d;
			if (base) {
				d = std::make_shared<SampleData>();
				d->orig = base->orig;
				d->origRate = base->origRate;
				d->path = base->path;
				d->name = base->name;
				d->truncated = base->truncated;
			} else {
				d = fallen::wav::load(p, r.err);
			}
			if (d) {
				fallen::wav::prepare(*d, rate);
				r.data = d;
			}
			std::lock_guard<std::mutex> lock(L->mx);
			L->results.push_back(r);
		}).detach();
	}
	void install(int voice, std::shared_ptr<SampleData> d) {
		if (holder[voice]) graveyard.push_back({holder[voice], system::getTime()});
		holder[voice] = d;
		live[voice].store(d.get());
	}
	// Called from the widget's step().
	void poll() {
		std::vector<KitLoader::Result> rs;
		{
			std::lock_guard<std::mutex> lock(loader->mx);
			rs.swap(loader->results);
		}
		for (auto& r : rs) {
			if (r.gen != gen[r.voice]) continue;  // superseded
			loading[r.voice] = false;
			if (r.data) {
				install(r.voice, r.data);
				path[r.voice] = r.data->path;
				missing[r.voice] = false;
				if (r.switchSrc) params[SRC1_PARAM + r.voice].setValue(2.f);
				if (r.data->truncated) report.push_back(std::string(KIT_NAMES[r.voice]) + ": truncated to 20 s");
			} else {
				error[r.voice] = r.err;
				report.push_back(std::string(KIT_NAMES[r.voice]) + ": " + r.err);
			}
		}
		double now = system::getTime();
		graveyard.erase(std::remove_if(graveyard.begin(), graveyard.end(), [&](const std::pair<std::shared_ptr<SampleData>, double>& g) { return now - g.second > 1.0; }),
		                graveyard.end());
	}
	void loadPath(int voice, const std::string& p) {
		path[voice] = p;
		missing[voice] = false;
		report = {std::string(KIT_NAMES[voice]) + ": " + system::getFilename(p)};
		startJob(voice, p, nullptr, true);
	}
	void loadDialog(int voice) {
		std::string p = fileDialog(FileDialog::OPEN, lastDir, "", "WAV:wav,WAV");
		if (p.empty()) return;
		lastDir = system::getDirectory(p);
		loadPath(voice, p);
	}
	void clearSample(int voice) {
		gen[voice]++;
		loading[voice] = false;
		install(voice, nullptr);
		path[voice].clear();
		missing[voice] = false;
		error[voice].clear();
		params[SRC1_PARAM + voice].setValue(voice == 0 ? 1.f : 0.f);
	}
	void loadFolder() {
		std::string dir = fileDialog(FileDialog::FOLDER, lastDir, "", nullptr);
		if (dir.empty()) return;
		lastDir = dir;
		std::vector<std::string> files;
		for (const std::string& e : system::getEntries(dir)) {
			std::string ext = string::lowercase(system::getExtension(e));
			if (ext == ".wav" && system::isFile(e)) files.push_back(e);
		}
		std::sort(files.begin(), files.end());
		std::string found[6];
		std::vector<std::string> unused;
		for (const std::string& f : files) {
			int v = fallen::wav::voiceForFileName(system::getFilename(f));
			if (v >= 0 && found[v].empty()) found[v] = f;
			else unused.push_back(system::getFilename(f));
		}
		report = {"Folder: " + system::getFilename(dir)};
		for (int v = 0; v < 6; v++) {
			if (found[v].empty()) {
				report.push_back(std::string(KIT_NAMES[v]) + ": no match, unchanged");
			} else {
				report.push_back(std::string(KIT_NAMES[v]) + ": " + system::getFilename(found[v]));
				path[v] = found[v];
				missing[v] = false;
				startJob(v, found[v], nullptr, true);
			}
		}
		if (!unused.empty()) report.push_back(string::f("%d other WAV files not used", (int)unused.size()));
	}

	// ---- audio
	void process(const ProcessArgs& args) override {
		float sr = args.sampleRate;
		float crunchAmt = params[CRUNCH_PARAM].getValue();
		bool accent = inputs[ACCENT_INPUT].getVoltage() >= 1.f;
		float mix = 0.f;
		for (int i = 0; i < 6; i++) {
			int src = intVal(this, SRC1_PARAM + i);
			SampleData* s = live[i].load(std::memory_order_acquire);
			bool useSample = src == fallen::SRC_SAMPLE && s && !s->data.empty();
			if (trig[i].process(inputs[TRIG1_INPUT + i].getVoltage(), 0.1f, 1.f)) {
				float vel = math::clamp(inputs[TRIG1_INPUT + i].getVoltage() / 10.f, 0.f, 1.f);
				if (accent) vel *= 1.5849f;
				float tune = params[TUNE1_PARAM + i].getValue() + 12.f * inputs[TUNE1_INPUT + i].getVoltage();
				float dmul = expVal(this, DECAY1_PARAM + i, 0.1f, 2.f);
				if (useSample) {
					float lenSec = s->data.size() / std::max(s->rate, 1.f);
					player[i].trigger(vel, tune, std::max(0.02f, dmul * lenSec), dmul >= 0.999f, sr);
					lastEarly[i] = false;
				} else {
					bool early = src != fallen::SRC_ANALOG;  // a SAMPLE voice with no sample plays EARLY
					synth[i].snappy = 0.3f + 0.3f * snappy;
					synth[i].trigger(vel, tune, dmul, early);
					lastEarly[i] = early;
				}
				if (i == fallen::CHAT && choke) {
					synth[fallen::OHAT].stopChoke();
					player[fallen::OHAT].stop(sr);
				}
				hit[i] = 0.08f;
			}
			float yd = synth[i].process();
			if (i == fallen::KICK && kickDrive && !lastEarly[i]) yd = std::tanh(2.f * yd) * 0.6f;
			float ys = player[i].process(s);
			float y;
			if (lastEarly[i] && synth[i].active) {
				crunch[i].set(crunchAmt, sr);
				y = crunch[i].process(yd) + ys;
			} else if (crunchSamples && ys != 0.f) {
				crunch[i].set(crunchAmt, sr);
				y = yd + crunch[i].process(ys);
			} else {
				y = yd + ys;
			}
			float v = 5.f * y * params[LEVEL1_PARAM + i].getValue();
			outputs[OUT1_OUTPUT + i].setVoltage(v);
			if (!(removeFromMix && outputs[OUT1_OUTPUT + i].isConnected())) mix += v;
			if (hit[i] > 0.f) hit[i] -= args.sampleTime;
		}
		outputs[MIX_OUTPUT].setVoltage(mix);
	}

	// ---- patch storage
	static std::string encode16(const std::vector<float>& x) {
		std::vector<uint8_t> b(x.size() * 2);
		for (size_t i = 0; i < x.size(); i++) {
			int16_t s = (int16_t)math::clamp((int)std::lround(x[i] * 32767.f), -32768, 32767);
			b[2 * i] = (uint8_t)(s & 0xff);
			b[2 * i + 1] = (uint8_t)((s >> 8) & 0xff);
		}
		return string::toBase64(b);
	}
	static std::vector<float> decode16(const std::string& s) {
		std::vector<uint8_t> b = string::fromBase64(s);
		std::vector<float> x(b.size() / 2);
		for (size_t i = 0; i < x.size(); i++) x[i] = (int16_t)(b[2 * i] | (b[2 * i + 1] << 8)) / 32767.f;
		return x;
	}
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* vs = json_array();
		for (int i = 0; i < 6; i++) {
			json_t* v = json_object();
			json_object_set_new(v, "path", json_string(path[i].c_str()));
			if (storeSamples && holder[i]) {
				json_object_set_new(v, "name", json_string(holder[i]->name.c_str()));
				json_object_set_new(v, "rate", json_real(holder[i]->origRate));
				json_object_set_new(v, "data", json_string(encode16(holder[i]->orig).c_str()));
			}
			json_array_append_new(vs, v);
		}
		json_object_set_new(root, "voices", vs);
		json_object_set_new(root, "storeSamples", json_boolean(storeSamples));
		json_object_set_new(root, "crunchSamples", json_boolean(crunchSamples));
		json_object_set_new(root, "choke", json_boolean(choke));
		json_object_set_new(root, "removeFromMix", json_boolean(removeFromMix));
		json_object_set_new(root, "kickDrive", json_boolean(kickDrive));
		json_object_set_new(root, "snappy", json_integer(snappy));
		json_object_set_new(root, "lastDir", json_string(lastDir.c_str()));
		return root;
	}
	void dataFromJson(json_t* root) override {
		storeSamples = jsonBool(root, "storeSamples", false);
		crunchSamples = jsonBool(root, "crunchSamples", false);
		choke = jsonBool(root, "choke", true);
		removeFromMix = jsonBool(root, "removeFromMix", false);
		kickDrive = jsonBool(root, "kickDrive", false);
		snappy = math::clamp(jsonInt(root, "snappy", 1), 0, 2);
		json_t* d = json_object_get(root, "lastDir");
		if (d && json_is_string(d)) lastDir = json_string_value(d);
		json_t* vs = json_object_get(root, "voices");
		for (int i = 0; i < 6; i++) {
			json_t* v = vs ? json_array_get(vs, i) : nullptr;
			std::string p;
			json_t* jp = v ? json_object_get(v, "path") : nullptr;
			if (jp && json_is_string(jp)) p = json_string_value(jp);
			json_t* data = v ? json_object_get(v, "data") : nullptr;
			gen[i]++;
			install(i, nullptr);
			path[i] = p;
			missing[i] = false;
			if (data && json_is_string(data)) {
				auto base = std::make_shared<SampleData>();
				base->orig = decode16(json_string_value(data));
				base->origRate = jsonFloat(v, "rate", 48000.f);
				json_t* nm = json_object_get(v, "name");
				base->name = nm && json_is_string(nm) ? json_string_value(nm) : system::getStem(p);
				base->path = p;
				startJob(i, p, base, false);
			} else if (!p.empty()) {
				if (system::isFile(p)) startJob(i, p, nullptr, false);
				else missing[i] = true;
			}
		}
	}
};

// One voice's name strip. Right-click for the voice menu; drop a WAV onto it to load.
struct KitName : FallenDisplay {
	KITModule* m = nullptr;
	int voice = 0;
	void drawContents(const DrawArgs& args) override {
		std::string s;
		NVGcolor col = ink();
		if (!m) {
			s = voice == 0 ? "ANALOG" : "EARLY";
		} else {
			int src = intVal(m, KITIds::SRC1_PARAM + voice);
			if (src == fallen::SRC_SAMPLE) {
				if (m->loading[voice]) { s = "LOADING"; col = ink(0.6f); }
				else if (m->missing[voice]) { s = "MISSING: " + system::getStem(m->path[voice]); col = accent(); }
				else if (!m->error[voice].empty()) { s = "ERROR"; col = accent(); }
				else if (m->holder[voice]) s = m->holder[voice]->name;
				else { s = "NO SAMPLE"; col = ink(0.6f); }
			} else {
				s = src == fallen::SRC_EARLY ? "EARLY" : "ANALOG";
				col = ink(0.75f);
			}
			if (m->hit[voice] > 0.f) rect(args.vg, box.size.x - 4, 2, 2.5f, box.size.y - 4, accent());
		}
		if (s.size() > 11) s = s.substr(0, 10) + "~";
		text(args.vg, 2.5f, box.size.y * 0.5f, s, 9.f, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, col);
	}
	void onButton(const ButtonEvent& e) override {
		if (m && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			Menu* menu = createMenu();
			appendVoiceMenu(menu, m, voice);
			e.consume(this);
			return;
		}
		FallenDisplay::onButton(e);
	}
	void onPathDrop(const PathDropEvent& e) override {
		if (!m || e.paths.empty()) return;
		std::string p = e.paths[0];
		if (string::lowercase(system::getExtension(p)) != ".wav") return;
		m->lastDir = system::getDirectory(p);
		m->loadPath(voice, p);
		e.consume(this);
	}
	static void appendVoiceMenu(Menu* menu, KITModule* m, int voice) {
		menu->addChild(createMenuLabel(KIT_NAMES[voice]));
		menu->addChild(createMenuItem("Load sample…", "", [=]() { m->loadDialog(voice); }));
		menu->addChild(createMenuItem("Clear sample", "", [=]() { m->clearSample(voice); }, m->path[voice].empty()));
		if (!m->path[voice].empty()) menu->addChild(createMenuLabel(m->path[voice]));
	}
};

struct KITWidget : ModuleWidget {
	KITWidget(KITModule* m) {
		fallenScaffold(this, m, "KIT");
		placeKIT<KITModule>(this, m);
		const float* rects[6] = {KIT_NAME1_MM, KIT_NAME2_MM, KIT_NAME3_MM, KIT_NAME4_MM, KIT_NAME5_MM, KIT_NAME6_MM};
		for (int i = 0; i < 6; i++) {
			auto* d = new KitName;
			d->m = m;
			d->voice = i;
			d->setRectMm(rects[i]);
			addChild(d);
		}
	}
	void step() override {
		if (KITModule* m = dynamic_cast<KITModule*>(module)) m->poll();
		ModuleWidget::step();
	}
	void appendContextMenu(Menu* menu) override {
		KITModule* m = dynamic_cast<KITModule*>(module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Samples (right-click a name strip, or drop a WAV on it)"));
		for (int i = 0; i < 6; i++)
			menu->addChild(createSubmenuItem(KIT_NAMES[i], m->path[i].empty() ? "" : system::getStem(m->path[i]),
			                                 [=](Menu* sub) { KitName::appendVoiceMenu(sub, m, i); }));
		menu->addChild(createMenuItem("Load kit from folder…", "", [=]() { m->loadFolder(); }));
		menu->addChild(createSubmenuItem("Last load report", "", [=](Menu* sub) {
			for (const std::string& line : m->report) sub->addChild(createMenuLabel(line));
		}));
		menu->addChild(createBoolPtrMenuItem("Store samples in patch", "", &m->storeSamples));
		menu->addChild(createBoolPtrMenuItem("Crunch samples", "", &m->crunchSamples));
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("C.HAT chokes O.HAT", "", &m->choke));
		menu->addChild(createBoolPtrMenuItem("Individual outs remove from mix", "", &m->removeFromMix));
		menu->addChild(createBoolPtrMenuItem("Kick drive (ANALOG)", "", &m->kickDrive));
		menu->addChild(createIndexPtrSubmenuItem("Snare snappy (ANALOG)", {"0.3", "0.6", "0.9"}, &m->snappy));
	}
};

Model* modelKIT = createModel<KITModule, KITWidget>("KIT");
