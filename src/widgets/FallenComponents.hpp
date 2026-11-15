// Knobs, jacks, toggles, buttons and the display base. SVGs come from scripts/make_panels.py.
#pragma once
#include "../plugin.hpp"

inline std::shared_ptr<window::Svg> fallenSvg(const std::string& name) {
	return window::Svg::load(asset::plugin(pluginInstance, "res/components/" + name + ".svg"));
}

struct FallenKnobBase : app::SvgKnob {
	FallenKnobBase() {
		minAngle = -0.75f * M_PI;
		maxAngle = 0.75f * M_PI;
		shadow->opacity = 0.15f;
	}
};
struct FallenKnob : FallenKnobBase { FallenKnob() { setSvg(fallenSvg("FallenKnob")); } };
struct FallenKnobSmall : FallenKnobBase { FallenKnobSmall() { setSvg(fallenSvg("FallenKnobSmall")); } };
struct FallenKnobTiny : FallenKnobBase { FallenKnobTiny() { setSvg(fallenSvg("FallenKnobTiny")); shadow->opacity = 0.1f; } };

struct FallenJack : app::SvgPort { FallenJack() { setSvg(fallenSvg("FallenJack")); } };

// Value 0 is the TOP position, which is the first printed label.
struct FallenToggle2 : app::SvgSwitch {
	FallenToggle2() {
		addFrame(fallenSvg("Toggle2_0"));
		addFrame(fallenSvg("Toggle2_1"));
		shadow->opacity = 0.f;
	}
};
struct FallenToggle3 : app::SvgSwitch {
	FallenToggle3() {
		addFrame(fallenSvg("Toggle3_0"));
		addFrame(fallenSvg("Toggle3_1"));
		addFrame(fallenSvg("Toggle3_2"));
		shadow->opacity = 0.f;
	}
};

// The accent colour (#e04a1f) for LEDs.
template <typename TBase = app::ModuleLightWidget>
struct TFallenLight : TBase {
	TFallenLight() {
		this->bgColor = nvgRGB(0x2a, 0x2a, 0x2a);
		this->borderColor = nvgRGBA(0, 0, 0, 0x60);
		this->addBaseColor(nvgRGB(0xff, 0x5a, 0x2a));
	}
};
using FallenLight = TFallenLight<>;

struct FallenButtonBase : app::SvgSwitch {
	FallenButtonBase() {
		momentary = true;
		addFrame(fallenSvg("FallenButton_0"));
		addFrame(fallenSvg("FallenButton_1"));
		shadow->opacity = 0.f;
	}
};
struct FallenButton : LightButton<FallenButtonBase, MediumSimpleLight<FallenLight>> {};
struct FallenLatch : LightButton<FallenButtonBase, MediumSimpleLight<FallenLight>> {
	FallenLatch() { momentary = false; latch = true; }
};

// Panel and screws. Placement of controls comes from generated/Layout.hpp.
template <class W>
inline void fallenScaffold(W* w, Module* m, const std::string& slug) {
	w->setModule(m);
	w->setPanel(createPanel(asset::plugin(pluginInstance, "res/" + slug + ".svg")));
	float width = w->box.size.x;
	w->addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, 0)));
	w->addChild(createWidget<ScrewBlack>(Vec(width - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	if (width > 6 * RACK_GRID_WIDTH) {
		w->addChild(createWidget<ScrewBlack>(Vec(width - 2 * RACK_GRID_WIDTH, 0)));
		w->addChild(createWidget<ScrewBlack>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	}
}

// Display: black ground, contents drawn in layer 1 so they stay lit when the room lights dim.
struct FallenDisplay : widget::OpaqueWidget {
	void setRectMm(const float* mm) {
		box.pos = mm2px(Vec(mm[0], mm[1]));
		box.size = mm2px(Vec(mm[2], mm[3]));
	}
	static NVGcolor ink(float a = 1.f) { return nvgRGBA(0xf1, 0xf1, 0xee, (unsigned char)(255 * math::clamp(a, 0.f, 1.f))); }
	static NVGcolor accent(float a = 1.f) { return nvgRGBA(0xff, 0x5a, 0x2a, (unsigned char)(255 * math::clamp(a, 0.f, 1.f))); }
	void draw(const DrawArgs& args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 1.5f);
		nvgFillColor(args.vg, nvgRGB(0x0d, 0x0d, 0x0d));
		nvgFill(args.vg);
		Widget::draw(args);
	}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			nvgSave(args.vg);
			nvgScissor(args.vg, 0, 0, box.size.x, box.size.y);
			drawContents(args);
			nvgRestore(args.vg);
		}
		Widget::drawLayer(args, layer);
	}
	virtual void drawContents(const DrawArgs& args) {}
	bool setFont(NVGcontext* vg, float size) {
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (!font || font->handle < 0) return false;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, size);
		return true;
	}
	void text(NVGcontext* vg, float x, float y, const std::string& s, float size, int align = NVG_ALIGN_LEFT | NVG_ALIGN_TOP, NVGcolor col = ink()) {
		if (!setFont(vg, size)) return;
		nvgFillColor(vg, col);
		nvgTextAlign(vg, align);
		nvgText(vg, x, y, s.c_str(), NULL);
	}
	void rect(NVGcontext* vg, float x, float y, float w, float h, NVGcolor c) {
		nvgBeginPath(vg);
		nvgRect(vg, x, y, w, h);
		nvgFillColor(vg, c);
		nvgFill(vg);
	}
};

// A display that is only drawn (passes mouse events through).
struct FallenScreen : FallenDisplay {
	void onButton(const ButtonEvent& e) override { Widget::onButton(e); }
};
