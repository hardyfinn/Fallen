// DURA: duration sequencer.
#include "Common.hpp"
#include "../time/Dura.hpp"

using fallen::Dura;

static constexpr float MS_LO = 1.f, MS_HI = 2000.f;

struct DURAModule;

// LEN knob: its meaning follows the MODE switch.
struct LenQuantity : ParamQuantity {
	std::string getDisplayValueString() override;
	void setDisplayValueString(std::string s) override;
};

struct DURAModule : Module, DURAIds {
	Dura engine;
	Dura::Params pr;
	fallen::TrigOut trigOut, eocOut;
	fallen::GateOut gateOut;
	fallen::ClockMeter clock;
	dsp::SchmittTrigger resetIn, resetBtn;
	Flash stepFlash;
	float lenStore[3][16];
	int curMode = 1;
	bool resetOnFirstClock = true;
	bool waitClock = false;
	float stepCv = 0.f, intervalCv = 0.f, repCv = 0.f;
	int lightDiv = 0;

	DURAModule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int i = 0; i < 16; i++) {
			configParam<LenQuantity>(LEN1_PARAM + i, 0.f, 1.f, 0.f, string::f("Step %d length", i + 1));
			configInt(this, REP1_PARAM + i, 1, 16, 1, string::f("Step %d repeats", i + 1));
			configSwitch(ON1_PARAM + i, 0.f, 1.f, 1.f, string::f("Step %d", i + 1), {"Rest", "On"});
			configLight(ON1_LIGHT + i, string::f("Step %d", i + 1));
			lenStore[0][i] = fallen::expNorm(125.f, MS_LO, MS_HI);
			lenStore[1][i] = 0.f;
			lenStore[2][i] = 5.f / 13.f;
		}
		configSwitch(MODE_PARAM, 0.f, 2.f, 1.f, "Mode", {"MS (milliseconds)", "UNITS (multiples of UNIT)", "RATIO (ratios of BASE)"});
		configExp(this, UNIT_PARAM, 5.f, 500.f, 40.f, "UNIT (length of one unit)", " ms");
		configExp(this, BASE_PARAM, 20.f, 4000.f, 500.f, "BASE (length of ratio 1)", " ms");
		configParam(STRETCH_PARAM, -2.f, 2.f, 0.f, "STRETCH (scales every step)", "×", 2.f);
		configInt(this, STEPS_PARAM, 1, 16, 8, "STEPS (loop length)");
		configParam(NUDGE_PARAM, -50.f, 50.f, 0.f, "NUDGE (lengthens or shortens the last interval)", "%");
		configParam(LENGTH_PARAM, 1.f, 99.f, 50.f, "LENGTH (gate as a percentage of the interval)", "%");
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.f, "SPREAD (varies each gate length, repeatably)", "%", 0.f, 100.f);
		configInt(this, SEED_PARAM, 0, 99, 0, "SEED (which repeatable spread)");
		configSwitch(EVOLVE_PARAM, 0.f, 1.f, 0.f, "VARY", {"LOOP (same each loop)", "EVOLVE (differs each loop)"});
		configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "RUN", {"Stopped", "Running"});
		configButton(RESET_PARAM, "RESET");
		configInput(CLOCK_INPUT, "CLOCK (sets UNIT, BASE, or scales MS steps by period/100 ms)");
		configInput(RUN_INPUT, "RUN (gate: high runs)");
		configInput(RESET_INPUT, "RESET");
		configInput(STRETCH_INPUT, "STRETCH CV (1 V doubles every step)");
		configInput(LENGTH_INPUT, "LENGTH CV (±5 V = full range)");
		configInput(SPREAD_INPUT, "SPREAD CV (±5 V = full range)");
		configInput(STEPS_INPUT, "STEPS CV (0 to 10 V over 1 to 16)");
		configOutput(TRIG_OUTPUT, "TRIG");
		configOutput(GATE_OUTPUT, "GATE");
		configOutput(STEP_OUTPUT, "STEP (0 V at step 1, rising 10 V/steps)");
		configOutput(INTERVAL_OUTPUT, "INTERVAL (time to the next event: 0 V = 100 ms, +1 V per doubling)");
		configOutput(REPN_OUTPUT, "REP (repeat number within the step)");
		configOutput(EOC_OUTPUT, "EOC (end of loop)");
		configLight(RUN_LIGHT, "Run");
		configLight(RESET_LIGHT, "Reset");
		engine.reset();
	}

	void onReset(const ResetEvent& e) override {
		Module::onReset(e);
		for (int i = 0; i < 16; i++) {
			lenStore[0][i] = fallen::expNorm(125.f, MS_LO, MS_HI);
			lenStore[1][i] = 0.f;
			lenStore[2][i] = 5.f / 13.f;
		}
		curMode = 1;
		doReset();
	}

	void doReset() {
		engine.reset();
		gateOut.reset();
		waitClock = resetOnFirstClock && inputs[CLOCK_INPUT].isConnected();
	}

	// Map a normalised LEN value to the step data for a mode.
	static float lenMs(float v) { return fallen::expRange(v, MS_LO, MS_HI); }
	static int lenUnits(float v) { return 1 + (int)std::lround(math::clamp(v, 0.f, 1.f) * 15.f); }
	static int lenRatio(float v) { return (int)std::lround(math::clamp(v, 0.f, 1.f) * 13.f); }

	void process(const ProcessArgs& args) override {
		int mode = math::clamp(intVal(this, MODE_PARAM), 0, 2);
		if (mode != curMode) {
			// keep one LEN pattern per mode, so switching does not scramble the steps
			for (int i = 0; i < 16; i++) {
				lenStore[curMode][i] = params[LEN1_PARAM + i].getValue();
				params[LEN1_PARAM + i].setValue(lenStore[mode][i]);
			}
			curMode = mode;
		}
		pr.mode = mode;
		pr.unitMs = expVal(this, UNIT_PARAM, 5.f, 500.f);
		pr.baseMs = expVal(this, BASE_PARAM, 20.f, 4000.f);
		pr.stretch = math::clamp(std::pow(2.f, params[STRETCH_PARAM].getValue() + inV(this, STRETCH_INPUT)), 1.f / 16.f, 16.f);
		pr.steps = math::clamp(intVal(this, STEPS_PARAM) + (int)std::lround(inV(this, STEPS_INPUT) * 1.5f), 1, 16);
		pr.nudge = params[NUDGE_PARAM].getValue() * 0.01f;
		pr.length = math::clamp(params[LENGTH_PARAM].getValue() + inV(this, LENGTH_INPUT) / 5.f * 98.f, 1.f, 99.f);
		pr.spread = math::clamp(params[SPREAD_PARAM].getValue() + inV(this, SPREAD_INPUT) / 5.f, 0.f, 1.f);
		pr.seed = intVal(this, SEED_PARAM);
		pr.evolve = params[EVOLVE_PARAM].getValue() > 0.5f;
		for (int i = 0; i < 16; i++) {
			float v = params[LEN1_PARAM + i].getValue();
			pr.lenMs[i] = lenMs(v);
			pr.units[i] = lenUnits(v);
			pr.ratioIdx[i] = lenRatio(v);
			pr.rep[i] = intVal(this, REP1_PARAM + i);
			pr.on[i] = params[ON1_PARAM + i].getValue() > 0.5f;
		}
		bool edge = clock.process(inputs[CLOCK_INPUT].getVoltage(), args.sampleTime);
		pr.extPeriod = (inputs[CLOCK_INPUT].isConnected() && clock.valid()) ? clock.period() : 0.0;
		if (!inputs[CLOCK_INPUT].isConnected()) waitClock = false;
		if (edge) waitClock = false;
		if (resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f) | resetBtn.process(params[RESET_PARAM].getValue())) doReset();
		bool run = inputs[RUN_INPUT].isConnected() ? inputs[RUN_INPUT].getVoltage() >= 1.f : params[RUN_PARAM].getValue() > 0.5f;

		if (run && !waitClock) {
			Dura::Out o = engine.process(args.sampleTime, pr);
			if (o.event) {
				if (o.trig) { trigOut.fire(); gateOut.open(o.gate); stepFlash.fire(0.06f); }
				if (o.eoc) eocOut.fire();
				stepCv = o.step * 10.f / pr.steps;
				repCv = o.rep * 10.f / Dura::reps(pr, o.step);
				intervalCv = math::clamp((float)std::log2(o.interval / 0.1), -10.f, 10.f);
			}
		}
		outputs[TRIG_OUTPUT].setVoltage(trigOut.process(args.sampleTime));
		outputs[GATE_OUTPUT].setVoltage(run ? gateOut.process(args.sampleTime) : 0.f);
		outputs[EOC_OUTPUT].setVoltage(eocOut.process(args.sampleTime));
		outputs[STEP_OUTPUT].setVoltage(stepCv);
		outputs[INTERVAL_OUTPUT].setVoltage(intervalCv);
		outputs[REPN_OUTPUT].setVoltage(repCv);

		float fl = stepFlash.process(args.sampleTime);
		if (++lightDiv >= 64) {
			lightDiv = 0;
			float dt = args.sampleTime * 64;
			for (int i = 0; i < 16; i++) {
				float b = pr.on[i] ? 0.25f : 0.f;
				if (i == engine.curStep && run) b = std::max(b, 0.5f + 0.5f * fl);
				if (i >= pr.steps) b *= 0.3f;
				lights[ON1_LIGHT + i].setBrightnessSmooth(b, dt);
			}
			lights[RUN_LIGHT].setBrightness(run ? 1.f : 0.f);
			lights[RESET_LIGHT].setBrightnessSmooth(params[RESET_PARAM].getValue() > 0.5f ? 1.f : 0.f, dt);
		}
	}

	json_t* dataToJson() override {
		json_t* r = json_object();
		for (int i = 0; i < 16; i++) lenStore[curMode][i] = params[LEN1_PARAM + i].getValue();
		json_t* a = json_array();
		for (int m = 0; m < 3; m++) json_array_append_new(a, jsonFloats(lenStore[m], 16));
		json_object_set_new(r, "lenStore", a);
		json_object_set_new(r, "mode", json_integer(curMode));
		json_object_set_new(r, "resetOnFirstClock", json_boolean(resetOnFirstClock));
		return r;
	}
	void dataFromJson(json_t* r) override {
		json_t* a = json_object_get(r, "lenStore");
		if (a && json_is_array(a)) {
			for (int m = 0; m < 3 && m < (int)json_array_size(a); m++) {
				json_t* row = json_array_get(a, m);
				for (int i = 0; i < 16 && i < (int)json_array_size(row); i++) lenStore[m][i] = (float)json_number_value(json_array_get(row, i));
			}
		}
		curMode = jsonInt(r, "mode", intVal(this, MODE_PARAM));
		resetOnFirstClock = jsonBool(r, "resetOnFirstClock", true);
	}

	// ----- context-menu actions (UI thread)
	void fillFromTable() {
		params[MODE_PARAM].setValue(1.f);
		int seed = intVal(this, SEED_PARAM);
		for (int i = 0; i < 16; i++) {
			int u = 1 + (int)(fallen::hash01((uint32_t)seed * 31u + 5u, (uint32_t)i) * 10.f);  // 1..10, the Multistability table
			params[LEN1_PARAM + i].setValue((u - 1) / 15.f);
		}
	}
	void reverseSteps() {
		int n = intVal(this, STEPS_PARAM);
		for (int i = 0; i < n / 2; i++) swapSteps(i, n - 1 - i);
	}
	void rotate(int dir) {
		int n = intVal(this, STEPS_PARAM);
		if (n < 2) return;
		if (dir > 0) for (int i = n - 1; i > 0; i--) swapSteps(i, i - 1);
		else for (int i = 0; i < n - 1; i++) swapSteps(i, i + 1);
	}
	void swapSteps(int a, int b) {
		for (int base : {(int)LEN1_PARAM, (int)REP1_PARAM, (int)ON1_PARAM}) {
			float t = params[base + a].getValue();
			params[base + a].setValue(params[base + b].getValue());
			params[base + b].setValue(t);
		}
	}
};

std::string LenQuantity::getDisplayValueString() {
	DURAModule* m = dynamic_cast<DURAModule*>(module);
	float v = getValue();
	int mode = m ? math::clamp(intVal(m, DURAIds::MODE_PARAM), 0, 2) : 1;
	if (mode == 0) return string::f("%.1f ms", DURAModule::lenMs(v));
	if (mode == 2) return std::string("× ") + Dura::RATIO_NAMES[DURAModule::lenRatio(v)] + " of BASE";
	return string::f("× %d UNIT", DURAModule::lenUnits(v));
}
void LenQuantity::setDisplayValueString(std::string s) {
	DURAModule* m = dynamic_cast<DURAModule*>(module);
	int mode = m ? math::clamp(intVal(m, DURAIds::MODE_PARAM), 0, 2) : 1;
	float x = std::atof(s.c_str());
	if (mode == 0) setValue(fallen::expNorm(math::clamp(x, MS_LO, MS_HI), MS_LO, MS_HI));
	else if (mode == 1) setValue(math::clamp((x - 1.f) / 15.f, 0.f, 1.f));
	else {
		for (int i = 0; i < Dura::NRATIOS; i++) if (s == Dura::RATIO_NAMES[i]) { setValue(i / 13.f); return; }
	}
}

// Proportional bars: each step's width is its duration.
struct DuraBars : FallenScreen {
	DURAModule* m = nullptr;
	void drawContents(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		float W = box.size.x, Hh = box.size.y;
		Dura::Params p;
		int cur = 0;
		if (m) {
			p = m->pr;
			cur = m->engine.curStep;
		} else {
			p.steps = 8;
			for (int i = 0; i < 16; i++) p.units[i] = 1 + (i * 7) % 4;
		}
		int n = math::clamp(p.steps, 1, 16);
		double total = Dura::loopLength(p);
		if (total <= 0) return;
		float x0 = 3.f, w = W - 6.f, y0 = Hh * 0.18f, h = Hh * 0.42f;
		double acc = 0;
		for (int i = 0; i < n; i++) {
			double I = Dura::stepInterval(p, i);
			int R = Dura::reps(p, i);
			double d = I * R + (i == n - 1 ? I * p.nudge : 0.0);
			float xa = x0 + (float)(acc / total) * w, xb = x0 + (float)((acc + d) / total) * w;
			NVGcolor c = i == cur ? accent(0.95f) : ink(p.on[i] ? 0.75f : 0.18f);
			rect(vg, xa + 0.6f, y0, std::max(xb - xa - 1.2f, 0.6f), h, c);
			for (int r = 1; r < R; r++) {
				float xr = xa + (float)(I * r / total) * w;
				rect(vg, xr - 0.3f, y0, 0.6f, h, nvgRGB(0x0d, 0x0d, 0x0d));
			}
			acc += d;
		}
		std::string mode = p.mode == 0 ? "MS" : (p.mode == 1 ? "UNITS" : "RATIO");
		std::string s = string::f("%s   LOOP %.0f ms", mode.c_str(), total * 1000.0);
		if (m) s += string::f("   NOW %.1f ms x%d", m->engine.curInterval * 1000.0, Dura::reps(p, cur));
		text(vg, 4.f, Hh * 0.66f, s, 9.5f);
	}
};

struct DURAWidget : ModuleWidget {
	DURAWidget(DURAModule* m) {
		fallenScaffold(this, m, "DURA");
		placeDURA<DURAModule>(this, m);
		auto* d = new DuraBars;
		d->m = m;
		d->setRectMm(DURA_BARS_MM);
		addChild(d);
	}
	void appendContextMenu(Menu* menu) override {
		DURAModule* m = dynamic_cast<DURAModule*>(module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Fill from table (UNITS 1 to 10, from SEED)", "", [=]() { m->fillFromTable(); }));
		menu->addChild(createMenuItem("Reverse steps", "", [=]() { m->reverseSteps(); }));
		menu->addChild(createMenuItem("Rotate right", "", [=]() { m->rotate(1); }));
		menu->addChild(createMenuItem("Rotate left", "", [=]() { m->rotate(-1); }));
		menu->addChild(createBoolPtrMenuItem("Wait for the first clock edge after reset", "", &m->resetOnFirstClock));
	}
};

Model* modelDURA = createModel<DURAModule, DURAWidget>("DURA");
