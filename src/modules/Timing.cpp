// GROUP, PRIME and HOLD.
#include "Common.hpp"
#include "../time/Group.hpp"
#include "../time/Prime.hpp"
#include "../time/Hold.hpp"

// ============================================================================ GROUP
struct GROUPModule : Module, GROUPIds {
	fallen::Group engine;
	fallen::Group::Params pr;
	fallen::TrigOut trigOut, firstOut, lastOut;
	fallen::GateOut gateOut;
	dsp::SchmittTrigger trigIn, fireBtn, resetIn;
	Flash fireFlash;
	float indexCv = 0.f;

	GROUPModule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configInt(this, COUNT_PARAM, 1, 16, 5, "COUNT (events per group)");
		configExp(this, SPACE_PARAM, 1.f, 1000.f, 60.f, "SPACE (first gap in the group)", " ms");
		configParam(CURVE_PARAM, -1.f, 1.f, -0.5f, "CURVE (− compresses, + expands the gaps)");
		configExp(this, PERIOD_PARAM, 20.f, 10000.f, 1000.f, "PERIOD (group start to group start)", " ms");
		configParam(LENGTH_PARAM, 1.f, 99.f, 50.f, "LENGTH (gate, % of the gap)", "%");
		configButton(FIRE_PARAM, "FIRE (start a group now)");
		configParam(RATE_PARAM, 0.f, 0.25f, 0.f, "RATE (drift per group)", " cycle");
		configParam(DEPTH_PARAM, 0.f, 2.f, 0.f, "DEPTH (drift of SPACE)", " oct");
		configInput(TRIG_INPUT, "TRIG IN (each edge fires a group; turns off PERIOD)");
		configInput(RESET_INPUT, "RESET");
		configInput(COUNT_INPUT, "COUNT CV (0 to 10 V over 1 to 16)");
		configInput(SPACE_INPUT, "SPACE CV (1 V per doubling)");
		configInput(CURVE_INPUT, "CURVE CV (±5 V = full range)");
		configInput(PERIOD_INPUT, "PERIOD CV (1 V per doubling)");
		configOutput(TRIG_OUTPUT, "TRIG");
		configOutput(GATE_OUTPUT, "GATE");
		configOutput(FIRST_OUTPUT, "FIRST (first event of a group)");
		configOutput(LAST_OUTPUT, "LAST (last event of a group)");
		configOutput(INDEX_OUTPUT, "INDEX (position in the group)");
		configOutput(BUSY_OUTPUT, "BUSY (high while a group runs)");
		configLight(FIRE_LIGHT, "Fire");
		engine.reset();
	}
	void onReset(const ResetEvent& e) override { Module::onReset(e); engine.reset(); }

	void process(const ProcessArgs& args) override {
		pr.count = math::clamp(intVal(this, COUNT_PARAM) + (int)std::lround(inV(this, COUNT_INPUT) * 1.5f), 1, 16);
		pr.spaceMs = math::clamp(expVal(this, SPACE_PARAM, 1.f, 1000.f) * std::pow(2.f, inV(this, SPACE_INPUT)), 1.f, 10000.f);
		pr.curve = math::clamp(params[CURVE_PARAM].getValue() + inV(this, CURVE_INPUT) / 5.f, -1.f, 1.f);
		pr.periodMs = math::clamp(expVal(this, PERIOD_PARAM, 20.f, 10000.f) * std::pow(2.f, inV(this, PERIOD_INPUT)), 1.f, 60000.f);
		pr.length = params[LENGTH_PARAM].getValue();
		pr.rate = params[RATE_PARAM].getValue();
		pr.depth = params[DEPTH_PARAM].getValue();
		pr.external = inputs[TRIG_INPUT].isConnected();
		if (resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) { engine.reset(); gateOut.reset(); }
		bool fire = trigIn.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f) | fireBtn.process(params[FIRE_PARAM].getValue());
		fallen::Group::Out o = engine.process(args.sampleTime, pr, fire);
		if (o.event) {
			trigOut.fire();
			gateOut.open(o.gate);
			if (o.first) { firstOut.fire(); fireFlash.fire(0.08f); }
			if (o.last) lastOut.fire();
			indexCv = o.index * 10.f / std::max(o.count, 1);
		}
		outputs[TRIG_OUTPUT].setVoltage(trigOut.process(args.sampleTime));
		outputs[GATE_OUTPUT].setVoltage(gateOut.process(args.sampleTime));
		outputs[FIRST_OUTPUT].setVoltage(firstOut.process(args.sampleTime));
		outputs[LAST_OUTPUT].setVoltage(lastOut.process(args.sampleTime));
		outputs[INDEX_OUTPUT].setVoltage(indexCv);
		outputs[BUSY_OUTPUT].setVoltage(o.busy ? 10.f : 0.f);
		lights[FIRE_LIGHT].setBrightnessSmooth(fireFlash.process(args.sampleTime), args.sampleTime);
	}
};

struct GROUPWidget : ModuleWidget {
	GROUPWidget(GROUPModule* m) {
		fallenScaffold(this, m, "GROUP");
		placeGROUP<GROUPModule>(this, m);
	}
};
Model* modelGROUP = createModel<GROUPModule, GROUPWidget>("GROUP");

// ============================================================================ PRIME
struct PrimeLenQuantity : ParamQuantity {
	std::string getDisplayValueString() override {
		return std::to_string(fallen::Prime::PRIMES[math::clamp((int)std::lround(getValue()), 0, fallen::Prime::NP - 1)]);
	}
};

struct PRIMEModule : Module, PRIMEIds {
	fallen::Prime engine;
	fallen::Prime::Row rows[4];
	fallen::TrigOut trigs[4], anyOut, realignOut;
	fallen::GateOut gates[4];
	fallen::ClockMeter clock;
	dsp::SchmittTrigger resetIn, resetBtn;
	Flash flash[4];
	double untilPulse = 0.0;
	int lightDiv = 0;

	PRIMEModule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		const int defLen[4] = {1, 2, 3, 4};
		for (int r = 0; r < 4; r++) {
			char L = "ABCD"[r];
			configParam<PrimeLenQuantity>(LENA_PARAM + r, 0.f, fallen::Prime::NP - 1, defLen[r], string::f("Row %c length (prime)", L))->snapEnabled = true;
			configInt(this, HITSA_PARAM + r, 1, 31, 2, string::f("Row %c hits", L));
			configInt(this, ROTA_PARAM + r, 0, 30, 0, string::f("Row %c rotation", L));
			configLight(ROWA_LIGHT + r, string::f("Row %c", L));
			configOutput(TRIGA_OUTPUT + r, string::f("TRIG %c", L));
			configOutput(GATEA_OUTPUT + r, string::f("GATE %c", L));
		}
		configSwitch(SOURCEB_PARAM, 0.f, 1.f, 0.f, "Row B source", {"PULSE (every pulse)", "NEST (each event of row A)"});
		configSwitch(SOURCEC_PARAM, 0.f, 1.f, 0.f, "Row C source", {"PULSE (every pulse)", "NEST (each event of row B)"});
		configSwitch(SOURCED_PARAM, 0.f, 1.f, 0.f, "Row D source", {"PULSE (every pulse)", "NEST (each event of row C)"});
		configExp(this, RATE_PARAM, 5.f, 2000.f, 120.f, "RATE (time per pulse)", " ms");
		configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "RUN", {"Stopped", "Running"});
		configButton(RESET_PARAM, "RESET");
		configInput(CLOCK_INPUT, "CLOCK (each edge is one pulse)");
		configInput(RESET_INPUT, "RESET");
		configInput(RUN_INPUT, "RUN (gate)");
		configInput(RATE_INPUT, "RATE CV (1 V per doubling of the pulse time)");
		configOutput(ANY_OUTPUT, "ANY (any row's event)");
		configOutput(REALIGN_OUTPUT, "REALIGN (all rows back at step 1)");
		configLight(RUN_LIGHT, "Run");
		configLight(RESET_LIGHT, "Reset");
	}
	void onReset(const ResetEvent& e) override { Module::onReset(e); engine.reset(); untilPulse = 0.0; }

	int paramIdx(int r, int k) { return (k == 0 ? LENA_PARAM : k == 1 ? HITSA_PARAM : ROTA_PARAM) + r; }

	void process(const ProcessArgs& args) override {
		for (int r = 0; r < 4; r++) {
			rows[r].len = fallen::Prime::PRIMES[math::clamp(intVal(this, paramIdx(r, 0)), 0, fallen::Prime::NP - 1)];
			rows[r].hits = intVal(this, paramIdx(r, 1));
			rows[r].rot = intVal(this, paramIdx(r, 2));
			rows[r].nest = r > 0 && params[SOURCEB_PARAM + r - 1].getValue() > 0.5f;
		}
		engine.setRows(rows);
		if (resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f) | resetBtn.process(params[RESET_PARAM].getValue())) {
			engine.reset();
			untilPulse = 0.0;
		}
		bool run = inputs[RUN_INPUT].isConnected() ? inputs[RUN_INPUT].getVoltage() >= 1.f : params[RUN_PARAM].getValue() > 0.5f;
		double period = expVal(this, RATE_PARAM, 5.f, 2000.f) * 0.001 * std::pow(2.0, (double)inV(this, RATE_INPUT));
		period = fallen::clampd(period, 0.001, 60.0);
		bool pulse = false;
		bool edge = clock.process(inputs[CLOCK_INPUT].getVoltage(), args.sampleTime);
		if (inputs[CLOCK_INPUT].isConnected()) {
			pulse = edge && run;
			if (clock.valid()) period = clock.period();
		} else if (run) {
			if (untilPulse <= 0.0) { pulse = true; untilPulse += period; }
			untilPulse -= args.sampleTime;
		}
		if (pulse) {
			bool ev[4];
			bool re = engine.pulse(ev);
			bool any = false;
			for (int r = 0; r < 4; r++) if (ev[r]) { trigs[r].fire(); gates[r].open(period * 0.5); flash[r].fire(0.06f); any = true; }
			if (any) anyOut.fire();
			if (re) realignOut.fire();
		}
		for (int r = 0; r < 4; r++) {
			outputs[TRIGA_OUTPUT + r].setVoltage(trigs[r].process(args.sampleTime));
			outputs[GATEA_OUTPUT + r].setVoltage(gates[r].process(args.sampleTime));
		}
		outputs[ANY_OUTPUT].setVoltage(anyOut.process(args.sampleTime));
		outputs[REALIGN_OUTPUT].setVoltage(realignOut.process(args.sampleTime));
		float fl[4];
		for (int r = 0; r < 4; r++) fl[r] = flash[r].process(args.sampleTime);
		if (++lightDiv >= 64) {
			lightDiv = 0;
			for (int r = 0; r < 4; r++) lights[ROWA_LIGHT + r].setBrightnessSmooth(fl[r], args.sampleTime * 64);
			lights[RUN_LIGHT].setBrightness(run ? 1.f : 0.f);
			lights[RESET_LIGHT].setBrightnessSmooth(params[RESET_PARAM].getValue(), args.sampleTime * 64);
		}
	}
};

struct PrimeDots : FallenScreen {
	PRIMEModule* m = nullptr;
	int realign = 0;
	float timer = 0.f;
	void step() override {
		FallenScreen::step();
		timer -= APP->window->getLastFrameDuration();
		if (m && timer <= 0.f) {
			timer = 0.1f;
			fallen::Prime copy = m->engine;
			realign = copy.pulsesToRealign();
		}
	}
	void drawContents(const DrawArgs& args) override {
		NVGcontext* vg = args.vg;
		float W = box.size.x, Hh = box.size.y;
		float rowH = (Hh - 11.f) / 4.f;
		for (int r = 0; r < 4; r++) {
			int len = m ? m->engine.rows[r].len : 3 + 2 * r;
			int pos = m ? m->engine.pos[r] : 0;
			float y = 4.f + rowH * (r + 0.5f);
			text(vg, 3.f, y - 4.5f, std::string(1, "ABCD"[r]), 9.f);
			float x0 = 14.f, dx = std::min(7.f, (W - x0 - 4.f) / std::max(len, 1));
			for (int i = 0; i < len; i++) {
				bool hit = m ? m->engine.pat[r][i] : (i % 2 == 0);
				int played = (pos - 1 + len) % len;
				NVGcolor c = (m && i == played) ? accent(1.f) : ink(hit ? 0.8f : 0.2f);
				nvgBeginPath(vg);
				nvgCircle(vg, x0 + dx * (i + 0.5f), y, hit ? 2.2f : 1.2f);
				nvgFillColor(vg, c);
				nvgFill(vg);
			}
		}
		std::string s = realign < 0 ? "REALIGN IN >100k" : string::f("REALIGN IN %d", realign);
		text(vg, 3.f, Hh - 10.f, s, 9.f);
	}
};

struct PRIMEWidget : ModuleWidget {
	PRIMEWidget(PRIMEModule* m) {
		fallenScaffold(this, m, "PRIME");
		placePRIME<PRIMEModule>(this, m);
		auto* d = new PrimeDots;
		d->m = m;
		d->setRectMm(PRIME_DOTS_MM);
		addChild(d);
	}
};
Model* modelPRIME = createModel<PRIMEModule, PRIMEWidget>("PRIME");

// ============================================================================ HOLD
struct HOLDModule : Module, HOLDIds {
	fallen::Hold engine;
	fallen::Hold::Params pr;
	dsp::SchmittTrigger trig;
	float level = 0.f;

	HOLDModule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(LENGTH_PARAM, 1.f, 200.f, 50.f, "LENGTH (percentage of the gap)", "%");
		configParam(ATTACK_PARAM, 0.f, 1.f, fallen::expNorm(1.f, 0.1f, 200.f), "ATTACK", " ms", 2000.f, 0.1f);
		configParam(SHAPE_PARAM, 0.f, 1.f, 0.f, "SHAPE (hard gate to exponential decay)", "%", 0.f, 100.f);
		configInput(TRIG_INPUT, "TRIG");
		configInput(INTERVAL_INPUT, "INTERVAL (from DURA: the time to the next event)");
		configInput(LENGTH_INPUT, "LENGTH CV (±5 V = ±100%)");
		configInput(IN_INPUT, "IN (to the VCA, polyphonic)");
		configOutput(GATE_OUTPUT, "GATE");
		configOutput(ENV_OUTPUT, "ENV (0 to 10 V)");
		configOutput(OUT_OUTPUT, "OUT (IN through the VCA)");
		configBypass(IN_INPUT, OUT_OUTPUT);
	}
	void onReset(const ResetEvent& e) override { Module::onReset(e); engine.reset(); }

	void process(const ProcessArgs& args) override {
		pr.length = math::clamp(params[LENGTH_PARAM].getValue() + inV(this, LENGTH_INPUT) * 20.f, 1.f, 200.f);
		pr.attack = fallen::expRange(params[ATTACK_PARAM].getValue(), 0.1f, 200.f) * 0.001f;
		pr.shape = params[SHAPE_PARAM].getValue();
		if (trig.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f)) {
			double iv = inputs[INTERVAL_INPUT].isConnected() ? 0.1 * std::pow(2.0, (double)inputs[INTERVAL_INPUT].getVoltage()) : -1.0;
			engine.trigger(pr, iv);
		}
		float target = engine.process(args.sampleTime, pr);
		level += (target - level) * std::min(1.f, args.sampleTime / 0.0005f);
		outputs[GATE_OUTPUT].setVoltage(engine.gate() ? 10.f : 0.f);
		outputs[ENV_OUTPUT].setVoltage(10.f * target);
		int ch = std::max(1, inputs[IN_INPUT].getChannels());
		outputs[OUT_OUTPUT].setChannels(ch);
		for (int c = 0; c < ch; c++) outputs[OUT_OUTPUT].setVoltage(inputs[IN_INPUT].getVoltage(c) * level, c);
	}
};

struct HOLDWidget : ModuleWidget {
	HOLDWidget(HOLDModule* m) {
		fallenScaffold(this, m, "HOLD");
		placeHOLD<HOLDModule>(this, m);
	}
};
Model* modelHOLD = createModel<HOLDModule, HOLDWidget>("HOLD");
