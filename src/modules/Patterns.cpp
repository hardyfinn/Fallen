// LANES, CYCLE and CHORD: the modules you draw into.
#include "Common.hpp"
#include "../time/Lanes.hpp"
#include "../time/Cycle.hpp"
#include "../time/Hash.hpp"
#include "../dsp/Chords.hpp"

// ============================================================================ LANES
struct LANESModule : Module, LANESIds {
	fallen::Lanes engine;
	fallen::Lanes::Params pr;
	fallen::ClockMeter clock;
	dsp::SchmittTrigger resetIn, resetBtn;
	fallen::TrigOut trigs[5];
	fallen::GateOut gates[5], pitchGate;
	float pitch = 0.f;
	std::string rowNames[6] = {"R1", "R2", "R3", "R4", "R5", "PITCH"};
	bool switchNow = false;
	int lightDiv = 0;

	LANESModule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int r = 0; r < 6; r++) {
			configSwitch(MUTE1_PARAM + r, 0.f, 1.f, 0.f, string::f("Row %d mute", r + 1), {"Playing", "Muted"});
			configInt(this, LEN1_PARAM + r, 1, 32, 16, string::f("Row %d loop length", r + 1));
			configInt(this, SCROLL1_PARAM + r, -4, 4, 0, string::f("Row %d scroll (steps per loop, + = right)", r + 1));
			configLight(MUTE1_LIGHT + r, string::f("Row %d muted", r + 1));
		}
		configParam(INTENSITY_PARAM, 0.f, 1.f, 1.f, "INTENSITY (relaxed to intense)", "%", 0.f, 100.f);
		configInt(this, PATTERN_PARAM, 1, 8, 1, "PATTERN");
		configButton(RESET_PARAM, "RESET");
		configInput(CLOCK_INPUT, "CLOCK");
		configInput(RESET_INPUT, "RESET");
		configInput(INTENSITY_INPUT, "INTENSITY CV (0 to 10 V adds 0 to 100%)");
		configInput(PATTERN_INPUT, "PATTERN CV (1 V per pattern)");
		for (int r = 0; r < 5; r++) configOutput(TRIG1_OUTPUT + r, string::f("TRIG %d", r + 1));
		configOutput(GATES_OUTPUT, "GATES (rows 1 to 5, polyphonic)");
		configOutput(PITCH_OUTPUT, "PITCH (row 6, V/oct)");
		configOutput(PITCHGATE_OUTPUT, "PITCH GATE (row 6)");
		configLight(RESET_LIGHT, "Reset");
		defaultPattern();
	}

	void defaultPattern() {
		for (auto& p : engine.pats) p = fallen::LanesPattern();
		fillFromSeed(0, 1);
	}

	// A club-style starting pattern from a seed (deterministic).
	void fillFromSeed(int pat, int seed) {
		fallen::LanesPattern& P = engine.pats[pat];
		P = fallen::LanesPattern();
		auto h = [&](int r, int s) { return fallen::hash01((uint32_t)seed * 101u + 7u, (uint32_t)(r * 64 + s)); };
		for (int s = 0; s < 32; s++) {
			if (s % 4 == 0 || h(0, s) < 0.06f) P.set(0, s, true);                       // kick
			if (s % 8 == 4 || (s % 8 == 7 && h(1, s) < 0.3f)) P.set(1, s, true);         // snare
			if (h(2, s) < 0.18f) P.set(2, s, true);                                       // clap
			if (s % 2 == 0 || h(3, s) < 0.35f) P.set(3, s, true);                        // closed hat
			if (s % 4 == 2 && h(4, s) < 0.7f) P.set(4, s, true);                          // open hat
			if (h(5, s) < 0.3f) P.set(5, s, true);                                        // pitch row
			const int scale[7] = {0, 3, 5, 7, 10, 12, 15};
			P.pitch[s] = (int8_t)scale[(int)(h(6, s) * 7.f) % 7];
		}
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		defaultPattern();
		engine.reset();
	}

	void process(const ProcessArgs& args) override {
		for (int r = 0; r < 6; r++) {
			pr.len[r] = intVal(this, LEN1_PARAM + r);
			pr.scroll[r] = intVal(this, SCROLL1_PARAM + r);
			pr.mute[r] = params[MUTE1_PARAM + r].getValue() > 0.5f;
		}
		pr.intensity = math::clamp(params[INTENSITY_PARAM].getValue() + inV(this, INTENSITY_INPUT) / 10.f, 0.f, 1.f);
		pr.pattern = math::clamp(intVal(this, PATTERN_PARAM) - 1 + (int)std::lround(inV(this, PATTERN_INPUT)), 0, 7);
		pr.switchNow = switchNow;
		if (resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f) | resetBtn.process(params[RESET_PARAM].getValue())) engine.reset();
		if (clock.process(inputs[CLOCK_INPUT].getVoltage(), args.sampleTime)) {
			double gl = clock.valid() ? clock.period() * 0.5 : 0.05;
			fallen::Lanes::Out o = engine.clock(pr);
			for (int r = 0; r < 5; r++) if (o.ev[r]) { trigs[r].fire(); gates[r].open(gl); }
			if (o.pitchEv) { pitch = o.pitch; pitchGate.open(gl); }
		}
		outputs[GATES_OUTPUT].setChannels(5);
		for (int r = 0; r < 5; r++) {
			outputs[TRIG1_OUTPUT + r].setVoltage(trigs[r].process(args.sampleTime));
			outputs[GATES_OUTPUT].setVoltage(gates[r].process(args.sampleTime), r);
		}
		outputs[PITCH_OUTPUT].setVoltage(pitch);
		outputs[PITCHGATE_OUTPUT].setVoltage(pitchGate.process(args.sampleTime));
		if (++lightDiv >= 64) {
			lightDiv = 0;
			for (int r = 0; r < 6; r++) lights[MUTE1_LIGHT + r].setBrightness(pr.mute[r] ? 1.f : 0.f);
			lights[RESET_LIGHT].setBrightnessSmooth(params[RESET_PARAM].getValue(), args.sampleTime * 64);
		}
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_t* pats = json_array();
		for (auto& P : engine.pats) {
			json_t* p = json_object();
			json_t* cells = json_array();
			for (int r2 = 0; r2 < 6; r2++) json_array_append_new(cells, json_integer(P.cells[r2]));
			json_object_set_new(p, "cells", cells);
			json_t* pit = json_array();
			for (int s = 0; s < 32; s++) json_array_append_new(pit, json_integer(P.pitch[s]));
			json_object_set_new(p, "pitch", pit);
			json_array_append_new(pats, p);
		}
		json_object_set_new(r, "patterns", pats);
		json_t* names = json_array();
		for (auto& n : rowNames) json_array_append_new(names, json_string(n.c_str()));
		json_object_set_new(r, "rowNames", names);
		json_object_set_new(r, "switchNow", json_boolean(switchNow));
		return r;
	}
	void dataFromJson(json_t* r) override {
		json_t* pats = json_object_get(r, "patterns");
		if (pats && json_is_array(pats)) {
			for (int i = 0; i < 8 && i < (int)json_array_size(pats); i++) {
				json_t* p = json_array_get(pats, i);
				json_t* cells = json_object_get(p, "cells");
				json_t* pit = json_object_get(p, "pitch");
				for (int r2 = 0; r2 < 6 && cells && r2 < (int)json_array_size(cells); r2++) engine.pats[i].cells[r2] = (uint32_t)json_integer_value(json_array_get(cells, r2));
				for (int s = 0; s < 32 && pit && s < (int)json_array_size(pit); s++) engine.pats[i].pitch[s] = (int8_t)json_integer_value(json_array_get(pit, s));
			}
		}
		json_t* names = json_object_get(r, "rowNames");
		for (int i = 0; names && i < 6 && i < (int)json_array_size(names); i++) rowNames[i] = json_string_value(json_array_get(names, i));
		switchNow = jsonBool(r, "switchNow", false);
	}
};

struct LanesGrid : FallenDisplay {
	LANESModule* m = nullptr;
	int dragRow = -1, dragMode = -1;  // dragMode: 0 = erase, 1 = paint
	float dragStartY = 0.f, dragStartPitch = 0.f;
	int dragStep = -1;
	Vec dragPos;

	static constexpr float LABEL_W = 20.f;
	float cellW() const { return (box.size.x - LABEL_W - 2.f) / 32.f; }
	float rowH() const { return box.size.y / 6.f; }

	bool cellAt(Vec p, int& r, int& s) const {
		if (p.x < LABEL_W) return false;
		r = math::clamp((int)(p.y / rowH()), 0, 5);
		s = math::clamp((int)((p.x - LABEL_W) / cellW()), 0, 31);
		return true;
	}
	fallen::LanesPattern& pat() { return m->engine.pats[m->engine.cur]; }

	void onButton(const ButtonEvent& e) override {
		if (!m || e.button != GLFW_MOUSE_BUTTON_LEFT || e.action != GLFW_PRESS) { FallenDisplay::onButton(e); return; }
		int r, s;
		if (!cellAt(e.pos, r, s)) return;
		e.consume(this);
		dragRow = r;
		dragStep = s;
		dragPos = e.pos;
		if (r == 5 && pat().get(r, s) && (e.mods & RACK_MOD_MASK) == 0) {
			// pitch row: dragging an active cell changes its pitch; a click without movement toggles it off on release
			dragMode = 2;
			dragStartY = e.pos.y;
			dragStartPitch = pat().pitch[s];
			return;
		}
		dragMode = pat().get(r, s) ? 0 : 1;
		pat().set(r, s, dragMode == 1);
	}
	void onDragMove(const DragMoveEvent& e) override {
		if (!m || dragRow < 0) return;
		float z = getAbsoluteZoom();
		dragPos = dragPos.plus(e.mouseDelta.div(z));
		if (dragMode == 2) {
			int v = (int)std::lround(dragStartPitch - (dragPos.y - dragStartY) / 3.f);
			pat().pitch[dragStep] = (int8_t)math::clamp(v, -24, 24);
			return;
		}
		int r, s;
		if (cellAt(dragPos, r, s) && r == dragRow) pat().set(r, s, dragMode == 1);
	}
	void onDragEnd(const DragEndEvent& e) override {
		if (m && dragMode == 2 && std::fabs(dragPos.y - dragStartY) < 1.f) pat().set(5, dragStep, false);
		dragRow = -1;
	}

	void drawContents(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		float cw = cellW(), rh = rowH();
		for (int r = 0; r < 6; r++) {
			float y = r * rh;
			std::string name = m ? m->rowNames[r] : (r < 5 ? string::f("R%d", r + 1) : "PITCH");
			text(vg, 2.f, y + rh * 0.5f - 4.f, name.substr(0, 6), 8.5f);
			int len = m ? m->pr.len[r] : 16;
			int pos = m ? m->engine.pos[r] : 0;
			for (int s = 0; s < 32; s++) {
				float x = LABEL_W + s * cw;
				bool on = m ? m->engine.pats[m->engine.cur].get(r, s) : (r == 0 && s % 4 == 0) || (r == 3 && s % 2 == 0);
				float a = s >= len ? 0.12f : (on ? 0.85f : 0.16f);
				NVGcolor c = ink(a);
				if (m && on && s < len) {
					int idx = s;
					if (!m->engine.passes(r, idx, len, m->pr.intensity)) c = ink(0.38f);
				}
				// playhead: the position that played last, shown at its original cell
				if (m && s < len) {
					int played = (pos - 1 + len) % len;
					if (m->engine.cellAt(r, played, len) == s && !m->engine.fresh) c = on ? accent(1.f) : accent(0.35f);
				}
				float hgt = rh - 2.f;
				float yy = y + 1.f;
				if (r == 5 && on && m) {
					int pv = m->engine.pats[m->engine.cur].pitch[s];
					float t = (pv + 24) / 48.f;
					hgt = std::max(2.f, (rh - 2.f) * (0.2f + 0.8f * t));
					yy = y + 1.f + (rh - 2.f) - hgt;
				}
				rect(vg, x + 0.5f, yy, cw - 1.f, hgt, c);
				if (s % 4 == 0) rect(vg, x, y + rh - 1.f, 0.5f, 1.f, ink(0.4f));
			}
		}
		if (m) {
			std::string s = string::f("P%d", m->engine.cur + 1);
			if (m->engine.pending >= 0) s += string::f(">%d", m->engine.pending + 1);
			text(vg, 2.f, box.size.y - 10.f, s, 8.5f, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, accent(1.f));
		}
	}
};

struct LANESWidget : ModuleWidget {
	LANESWidget(LANESModule* m) {
		fallenScaffold(this, m, "LANES");
		placeLANES<LANESModule>(this, m);
		auto* d = new LanesGrid;
		d->m = m;
		d->setRectMm(LANES_GRID_MM);
		addChild(d);
	}
	void appendContextMenu(Menu* menu) override {
		LANESModule* m = dynamic_cast<LANESModule*>(module);
		if (!m) return;
		static fallen::LanesPattern clip;
		static bool hasClip = false;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Current pattern"));
		menu->addChild(createMenuItem("Clear pattern", "", [=]() { m->engine.pats[m->engine.cur] = fallen::LanesPattern(); }));
		menu->addChild(createMenuItem("Copy pattern", "", [=]() { clip = m->engine.pats[m->engine.cur]; hasClip = true; }));
		menu->addChild(createMenuItem("Paste pattern", "", [=]() { if (hasClip) m->engine.pats[m->engine.cur] = clip; }, !hasClip));
		menu->addChild(createSubmenuItem("Fill from seed", "", [=](Menu* sub) {
			for (int s = 1; s <= 16; s++) sub->addChild(createMenuItem(string::f("Seed %d", s), "", [=]() { m->fillFromSeed(m->engine.cur, s); }));
		}));
		menu->addChild(createSubmenuItem("Clear row", "", [=](Menu* sub) {
			for (int r = 0; r < 6; r++) sub->addChild(createMenuItem(m->rowNames[r], "", [=]() { m->engine.pats[m->engine.cur].cells[r] = 0; }));
		}));
		menu->addChild(createSubmenuItem("Rename rows", "", [=](Menu* sub) {
			for (int r = 0; r < 5; r++) {
				auto* f = new ui::TextField;
				f->box.size.x = 120;
				f->setText(m->rowNames[r]);
				struct NameField : ui::TextField {};
				sub->addChild(createMenuLabel(string::f("Row %d", r + 1)));
				auto* tf = new ui::TextField;
				tf->box.size.x = 120;
				tf->setText(m->rowNames[r]);
				struct Watch : widget::Widget {
					ui::TextField* tf; LANESModule* m; int r;
					void step() override { m->rowNames[r] = tf->getText().substr(0, 8); }
				};
				auto* w = new Watch; w->tf = tf; w->m = m; w->r = r;
				tf->addChild(w);
				sub->addChild(tf);
				delete f;
			}
		}));
		menu->addChild(createBoolPtrMenuItem("Switch pattern immediately (not at the end of row 1)", "", &m->switchNow));
	}
};
Model* modelLANES = createModel<LANESModule, LANESWidget>("LANES");

// ============================================================================ CYCLE
static const char* CYCLE_MODES[4] = {"UNI", "BI", "PITCH", "VOICE"};

struct CYCLEModule : Module, CYCLEIds {
	fallen::Cycle engine;
	dsp::SchmittTrigger trigIn, resetIn, resetBtn;
	int len[4], div[4], mode[4];
	float glideMs[4] = {0, 0, 0, 0};
	float out[4] = {};

	CYCLEModule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		const int defLen[4] = {5, 3, 4, 7};
		const int defMode[4] = {2, 3, 0, 1};
		for (int l = 0; l < 4; l++) {
			char L = "ABCD"[l];
			configInt(this, LENA_PARAM + l, 1, 16, defLen[l], string::f("Lane %c length", L));
			configInt(this, DIVA_PARAM + l, 1, 8, 1, string::f("Lane %c divider (moves every Nth trigger)", L));
			configParam(OFFSETA_PARAM + l, -1.f, 1.f, 0.f, string::f("Lane %c offset", L), "%", 0.f, 100.f);
			configSwitch(MODEA_PARAM + l, 0.f, 3.f, defMode[l], string::f("Lane %c mode", L), {"UNI (0 to 10 V)", "BI (±5 V)", "PITCH (semitones, V/oct)", "VOICE (FM4 voice 1 to 32)"});
			configOutput(OUTA_OUTPUT + l, string::f("OUT %c", L));
		}
		configButton(RESET_PARAM, "RESET");
		configInput(TRIG_INPUT, "TRIG (advances every lane)");
		configInput(RESET_INPUT, "RESET");
		configOutput(POLY_OUTPUT, "POLY (lanes A to D)");
		configLight(RESET_LIGHT, "Reset");
		defaultValues();
	}
	void defaultValues() {
		const float pitch[16] = {0, 3, 7, 10, 12, 7, 5, 3, 0, 15, 12, 10, 7, 5, 3, 0};
		for (int i = 0; i < 16; i++) {
			engine.val[0][i] = (pitch[i] + 24.f) / 48.f;
			engine.val[1][i] = (float)((i * 5) % 8) / 31.f;
			engine.val[2][i] = fallen::hash01(21, i);
			engine.val[3][i] = 0.5f + 0.4f * std::sin(i * 0.9f);
		}
	}
	void onReset(const ResetEvent& e) override { Module::onReset(e); engine = fallen::Cycle(); defaultValues(); }

	void process(const ProcessArgs& args) override {
		for (int l = 0; l < 4; l++) {
			len[l] = intVal(this, LENA_PARAM + l);
			div[l] = intVal(this, DIVA_PARAM + l);
			mode[l] = math::clamp(intVal(this, MODEA_PARAM + l), 0, 3);
		}
		if (resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f) | resetBtn.process(params[RESET_PARAM].getValue())) engine.reset();
		if (trigIn.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f)) engine.trigger(len, div);
		outputs[POLY_OUTPUT].setChannels(4);
		for (int l = 0; l < 4; l++) {
			float target = engine.out(l, mode[l], params[OFFSETA_PARAM + l].getValue());
			if (glideMs[l] > 0.f) out[l] += (target - out[l]) * std::min(1.f, args.sampleTime / (glideMs[l] * 0.001f));
			else out[l] = target;
			outputs[OUTA_OUTPUT + l].setVoltage(out[l]);
			outputs[POLY_OUTPUT].setVoltage(out[l], l);
		}
		lights[RESET_LIGHT].setBrightnessSmooth(params[RESET_PARAM].getValue(), args.sampleTime);
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_t* v = json_array();
		for (int l = 0; l < 4; l++) json_array_append_new(v, jsonFloats(engine.val[l], 16));
		json_object_set_new(r, "values", v);
		json_object_set_new(r, "glide", jsonFloats(glideMs, 4));
		return r;
	}
	void dataFromJson(json_t* r) override {
		json_t* v = json_object_get(r, "values");
		for (int l = 0; v && l < 4 && l < (int)json_array_size(v); l++) {
			json_t* row = json_array_get(v, l);
			for (int i = 0; i < 16 && i < (int)json_array_size(row); i++) engine.val[l][i] = (float)json_number_value(json_array_get(row, i));
		}
		readFloats(r, "glide", glideMs, 4);
	}
};

struct CycleBars : FallenDisplay {
	CYCLEModule* m = nullptr;
	int dragLane = -1;
	Vec dragPos;
	static constexpr float TOP = 9.f, LW = 9.f;
	float laneH() const { return (box.size.y - TOP) / 4.f; }
	float barW() const { return (box.size.x - LW - 2.f) / 16.f; }

	void setAt(Vec p, bool first) {
		if (!m) return;
		int l = math::clamp((int)((p.y - TOP) / laneH()), 0, 3);
		if (first) dragLane = l;
		if (l != dragLane) l = dragLane;
		int i = math::clamp((int)((p.x - LW) / barW()), 0, 15);
		float y0 = TOP + l * laneH() + 1.f, h = laneH() - 2.f;
		float v = math::clamp(1.f - (p.y - y0) / h, 0.f, 1.f);
		int md = m->mode[l];
		if (md == fallen::Cycle::PITCH) v = std::round(v * 48.f) / 48.f;
		if (md == fallen::Cycle::VOICE) v = std::round(v * 31.f) / 31.f;
		m->engine.val[l][i] = v;
	}
	void onButton(const ButtonEvent& e) override {
		if (m && e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && e.pos.y > TOP && e.pos.x > LW) {
			e.consume(this);
			dragPos = e.pos;
			setAt(e.pos, true);
			return;
		}
		FallenDisplay::onButton(e);
	}
	void onDragMove(const DragMoveEvent& e) override {
		if (dragLane < 0) return;
		dragPos = dragPos.plus(e.mouseDelta.div(getAbsoluteZoom()));
		setAt(dragPos, false);
	}
	void onDragEnd(const DragEndEvent& e) override { dragLane = -1; }

	void drawContents(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		long long re = m ? m->engine.realignIn(m->len, m->div) : 0;
		text(vg, 3.f, 1.5f, string::f("REALIGN IN %lld", re), 8.5f);
		for (int l = 0; l < 4; l++) {
			float y0 = TOP + l * laneH() + 1.f, h = laneH() - 2.f;
			int len = m ? m->len[l] : 8;
			int pos = m ? m->engine.pos[l] : 0;
			int md = m ? m->mode[l] : 0;
			text(vg, 1.5f, y0 + 1.f, std::string(1, "ABCD"[l]), 8.f);
			text(vg, box.size.x - 2.f, y0 + 1.f, CYCLE_MODES[md], 7.f, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, ink(0.6f));
			for (int i = 0; i < 16; i++) {
				float v = m ? m->engine.val[l][i] : 0.3f + 0.4f * ((i * 3) % 5) / 4.f;
				float x = LW + i * barW();
				NVGcolor c = i >= len ? ink(0.12f) : (m && i == pos && !m->engine.fresh ? accent(1.f) : ink(0.7f));
				float bh = std::max(1.f, h * v);
				rect(vg, x + 0.6f, y0 + h - bh, barW() - 1.2f, bh, c);
			}
		}
	}
};

struct CYCLEWidget : ModuleWidget {
	CYCLEWidget(CYCLEModule* m) {
		fallenScaffold(this, m, "CYCLE");
		placeCYCLE<CYCLEModule>(this, m);
		auto* d = new CycleBars;
		d->m = m;
		d->setRectMm(CYCLE_BARS_MM);
		addChild(d);
	}
	void appendContextMenu(Menu* menu) override {
		CYCLEModule* m = dynamic_cast<CYCLEModule*>(module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		for (int l = 0; l < 4; l++) {
			menu->addChild(createSubmenuItem(string::f("Lane %c glide", "ABCD"[l]), string::f("%.0f ms", m->glideMs[l]), [=](Menu* sub) {
				for (float g : {0.f, 5.f, 20.f, 50.f, 100.f, 200.f, 500.f})
					sub->addChild(createCheckMenuItem(string::f("%.0f ms", g), "", [=]() { return m->glideMs[l] == g; }, [=]() { m->glideMs[l] = g; }));
			}));
		}
	}
};
Model* modelCYCLE = createModel<CYCLEModule, CYCLEWidget>("CYCLE");

// ============================================================================ CHORD
struct CHORDModule : Module, CHORDIds {
	uint32_t slots[8];
	dsp::SchmittTrigger gateTrig;
	float notes[6] = {};
	int count = 0;
	int playingSlot = 0;

	CHORDModule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configInt(this, CHORD_PARAM, 1, 8, 1, "CHORD (slot)");
		configInt(this, INVERT_PARAM, 0, 5, 0, "INVERT");
		configSwitch(VOICING_PARAM, 0.f, 2.f, 0.f, "VOICING", {"CLOSE", "DROP2 (second-highest note down an octave)", "SPREAD (every other note up an octave)"});
		configInt(this, TRANSPOSE_PARAM, -12, 12, 0, "TRANSPOSE", " semitones");
		configInput(ROOT_INPUT, "ROOT (V/oct)");
		configInput(GATEIN_INPUT, "GATE IN (chord changes on its rising edge)");
		configInput(CHORD_INPUT, "CHORD CV (1 V per slot)");
		configInput(INVERT_INPUT, "INVERT CV (1 V per inversion)");
		configOutput(VOCT_OUTPUT, "V/OCT (one channel per note)");
		configOutput(GATE_OUTPUT, "GATE (GATE IN copied to each channel)");
		fallen::Chords::factory(slots);
	}
	void onReset(const ResetEvent& e) override { Module::onReset(e); fallen::Chords::factory(slots); }

	int slotNow() { return math::clamp(intVal(this, CHORD_PARAM) - 1 + (int)std::lround(inV(this, CHORD_INPUT)), 0, 7); }

	void process(const ProcessArgs& args) override {
		bool gated = inputs[GATEIN_INPUT].isConnected();
		float g = inputs[GATEIN_INPUT].getVoltage();
		bool rise = gateTrig.process(g, 0.1f, 1.f);
		if (!gated || rise) {
			playingSlot = slotNow();
			int inv = math::clamp(intVal(this, INVERT_PARAM) + (int)std::lround(inV(this, INVERT_INPUT)), 0, 5);
			count = fallen::Chords::notes(slots[playingSlot], inv, intVal(this, VOICING_PARAM), notes);
		}
		float root = inputs[ROOT_INPUT].getVoltage() + intVal(this, TRANSPOSE_PARAM) / 12.f;
		int n = std::max(count, 1);
		outputs[VOCT_OUTPUT].setChannels(n);
		outputs[GATE_OUTPUT].setChannels(n);
		for (int i = 0; i < n; i++) {
			outputs[VOCT_OUTPUT].setVoltage(root + (count ? notes[i] / 12.f : 0.f), i);
			outputs[GATE_OUTPUT].setVoltage(gated && g >= 1.f ? 10.f : 0.f, i);
		}
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		json_t* a = json_array();
		for (uint32_t s : slots) json_array_append_new(a, json_integer(s));
		json_object_set_new(r, "slots", a);
		return r;
	}
	void dataFromJson(json_t* r) override {
		json_t* a = json_object_get(r, "slots");
		for (int i = 0; a && i < 8 && i < (int)json_array_size(a); i++) slots[i] = (uint32_t)json_integer_value(json_array_get(a, i));
	}
};

struct ChordKeys : FallenDisplay {
	CHORDModule* m = nullptr;
	static constexpr float TOP = 11.f;
	int editSlot() { return m ? math::clamp(intVal(m, CHORDIds::CHORD_PARAM) - 1, 0, 7) : 0; }
	void onButton(const ButtonEvent& e) override {
		if (m && e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS && e.pos.y > TOP) {
			e.consume(this);
			float kw = (box.size.x - 4.f) / fallen::Chords::KEYS;
			int k = math::clamp((int)((e.pos.x - 2.f) / kw), 0, fallen::Chords::KEYS - 1);
			uint32_t& s = m->slots[editSlot()];
			uint32_t bit = 1u << k;
			int n = 0;
			for (int i = 0; i < fallen::Chords::KEYS; i++) n += (s >> i) & 1;
			if (s & bit) s &= ~bit;
			else if (n < fallen::Chords::MAXN) s |= bit;
			return;
		}
		FallenDisplay::onButton(e);
	}
	void drawContents(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		int slot = editSlot();
		uint32_t s = m ? m->slots[slot] : fallen::Chords::mask({0, 3, 7, 10});
		uint32_t fac[8];
		fallen::Chords::factory(fac);
		std::string name = s == fac[slot] ? fallen::Chords::FACTORY_NAMES[slot] : "user";
		text(vg, 3.f, 2.f, string::f("%d  %s", slot + 1, name.c_str()), 9.f);
		float kw = (box.size.x - 4.f) / fallen::Chords::KEYS;
		float h = box.size.y - TOP - 3.f;
		static const bool black[12] = {0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0};
		for (int k = 0; k < fallen::Chords::KEYS; k++) {
			float x = 2.f + k * kw;
			bool on = (s >> k) & 1;
			bool bk = black[k % 12];
			NVGcolor c = on ? accent(1.f) : (bk ? ink(0.25f) : ink(0.65f));
			float kh = bk ? h * 0.6f : h;
			rect(vg, x + 0.4f, TOP, kw - 0.8f, kh, c);
		}
	}
};

struct CHORDWidget : ModuleWidget {
	CHORDWidget(CHORDModule* m) {
		fallenScaffold(this, m, "CHORD");
		placeCHORD<CHORDModule>(this, m);
		auto* d = new ChordKeys;
		d->m = m;
		d->setRectMm(CHORD_KEYS_MM);
		addChild(d);
	}
	void appendContextMenu(Menu* menu) override {
		CHORDModule* m = dynamic_cast<CHORDModule*>(module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Restore factory chords", "", [=]() { fallen::Chords::factory(m->slots); }));
		menu->addChild(createMenuItem("Clear the shown slot", "", [=]() { m->slots[math::clamp(intVal(m, CHORDIds::CHORD_PARAM) - 1, 0, 7)] = 1u; }));
	}
};
Model* modelCHORD = createModel<CHORDModule, CHORDWidget>("CHORD");
