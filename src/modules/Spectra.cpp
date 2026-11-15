// SPECTRA: additive voice whose partials move from harmonic to inharmonic.
#include "Common.hpp"
#include "../dsp/Additive.hpp"

struct SPECTRAModule : Module, SPECTRAIds {
	static constexpr int MAXV = 4;
	fallen::Additive voice[MAXV];
	fallen::Additive::Params pr[MAXV];
	double movePhase = 0.0;
	int div = 0;

	SPECTRAModule() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configInt(this, PARTIALS_PARAM, 1, 32, 16, "PARTIALS");
		configParam(STRETCH_PARAM, -1.f, 1.f, 0.f, "STRETCH (0 = harmonic; partial n at n^(1 + 0.5 × STRETCH))");
		configParam(TILT_PARAM, -12.f, 6.f, -6.f, "TILT", " dB/oct");
		configParam(ODDEVEN_PARAM, -1.f, 1.f, 0.f, "ODD/EVEN (-1 = odd only, +1 = even only)");
		configExp(this, RATE_PARAM, 0.01f, 2.f, 0.1f, "RATE (MOVE window speed)", " Hz");
		configParam(DEPTH_PARAM, 0.f, 1.f, 0.f, "DEPTH (flat to only the MOVE window)", "%", 0.f, 100.f);
		configParam(DETUNE_PARAM, 0.f, 20.f, 0.f, "DETUNE (spread per partial)", " cents");
		configParam(WIDTH_PARAM, 0.f, 1.f, 0.f, "WIDTH (odd partials left, even right)", "%", 0.f, 100.f);
		configParam(LEVEL_PARAM, 0.f, 2.f, 1.f, "LEVEL", " dB", -10.f, 20.f);
		configExp(this, ATTACK_PARAM, 0.001f, 10.f, 0.01f, "ATTACK", " s");
		configExp(this, RELEASE_PARAM, 0.001f, 20.f, 0.5f, "RELEASE", " s");
		configInput(STRETCH_INPUT, "STRETCH CV (±5 V = full range)");
		configInput(TILT_INPUT, "TILT CV (1 V = 3 dB/oct)");
		configInput(DEPTH_INPUT, "DEPTH CV (0 to 10 V adds 0 to 100%)");
		configInput(PARTIALS_INPUT, "PARTIALS CV (0 to 10 V over 1 to 32)");
		configInput(VOCT_INPUT, "V/OCT (up to 4 voices)");
		configInput(GATE_INPUT, "GATE (polyphonic; unpatched = drone)");
		configOutput(L_OUTPUT, "L");
		configOutput(R_OUTPUT, "R");
		onSampleRateChange();
		for (auto& v : voice) v.reset();
	}
	void onSampleRateChange() override {
		for (auto& v : voice) v.sr = APP->engine->getSampleRate();
	}
	void onReset() override {
		movePhase = 0.0;
		for (auto& v : voice) v.reset();
	}

	void process(const ProcessArgs& args) override {
		int nch = math::clamp(inputs[VOCT_INPUT].getChannels(), 1, MAXV);
		float rate = expVal(this, RATE_PARAM, 0.01f, 2.f);
		movePhase += rate * args.sampleTime;
		if (movePhase >= 1.0) movePhase -= 1.0;
		bool drone = !inputs[GATE_INPUT].isConnected();

		if (div-- <= 0) {
			div = 31;
			for (int c = 0; c < nch; c++) {
				fallen::Additive::Params& p = pr[c];
				float part = params[PARTIALS_PARAM].getValue();
				if (inputs[PARTIALS_INPUT].isConnected()) part += inputs[PARTIALS_INPUT].getPolyVoltage(c) / 10.f * 31.f;
				p.partials = math::clamp((int)std::lround(part), 1, 32);
				p.stretch = math::clamp(params[STRETCH_PARAM].getValue() + inputs[STRETCH_INPUT].getPolyVoltage(c) / 5.f, -1.f, 1.f);
				p.tilt = math::clamp(params[TILT_PARAM].getValue() + 3.f * inputs[TILT_INPUT].getPolyVoltage(c), -12.f, 6.f);
				p.oddEven = params[ODDEVEN_PARAM].getValue();
				p.rate = rate;
				p.depth = math::clamp(params[DEPTH_PARAM].getValue() + inputs[DEPTH_INPUT].getPolyVoltage(c) / 10.f, 0.f, 1.f);
				p.detune = params[DETUNE_PARAM].getValue();
				p.width = params[WIDTH_PARAM].getValue();
				p.attack = expVal(this, ATTACK_PARAM, 0.001f, 10.f);
				p.release = expVal(this, RELEASE_PARAM, 0.001f, 20.f);
				float f0 = fallen::C4 * std::pow(2.f, inputs[VOCT_INPUT].getPolyVoltage(c));
				voice[c].update(f0, p, (float)movePhase);
			}
		}
		float L = 0.f, R = 0.f;
		for (int c = 0; c < nch; c++) {
			bool gate = drone || inputs[GATE_INPUT].getPolyVoltage(c) >= 1.f;
			float l, r;
			voice[c].process(gate, pr[c], l, r);
			L += l;
			R += r;
		}
		float g = 5.f * params[LEVEL_PARAM].getValue();
		outputs[L_OUTPUT].setVoltage(L * g);
		outputs[R_OUTPUT].setVoltage(R * g);
	}
	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "movePhase", json_real(movePhase));
		return root;
	}
	void dataFromJson(json_t* root) override {
		movePhase = jsonFloat(root, "movePhase", 0.f);
	}
};

struct SPECTRAWidget : ModuleWidget {
	SPECTRAWidget(SPECTRAModule* m) {
		fallenScaffold(this, m, "SPECTRA");
		placeSPECTRA<SPECTRAModule>(this, m);
	}
};

Model* modelSPECTRA = createModel<SPECTRAModule, SPECTRAWidget>("SPECTRA");
