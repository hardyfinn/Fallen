// Helpers shared by the Rack wrappers.
#pragma once
#include "../plugin.hpp"
#include "../generated/Layout.hpp"
#include "../dsp/Common.hpp"
#include "../time/Clock.hpp"

// Exponential knob: stored value 0..1, shown as lo*(hi/lo)^v in `unit`.
inline ParamQuantity* configExp(Module* m, int id, float lo, float hi, float def, const std::string& name, const std::string& unit, float mult = 1.f) {
	return m->configParam(id, 0.f, 1.f, fallen::expNorm(def, lo, hi), name, unit, hi / lo, lo * mult);
}
inline float expVal(Module* m, int id, float lo, float hi) { return fallen::expRange(m->params[id].getValue(), lo, hi); }

inline ParamQuantity* configInt(Module* m, int id, int lo, int hi, int def, const std::string& name, const std::string& unit = "", float displayOffset = 0.f) {
	ParamQuantity* q = m->configParam(id, (float)lo, (float)hi, (float)def, name, unit, 0.f, 1.f, displayOffset);
	q->snapEnabled = true;
	return q;
}
inline int intVal(Module* m, int id) { return (int)std::lround(m->params[id].getValue()); }

inline float inV(Module* m, int id, int ch = 0) { return m->inputs[id].isConnected() ? m->inputs[id].getVoltage(ch) : 0.f; }

// json helpers
inline float jsonFloat(json_t* root, const char* key, float def) {
	json_t* j = json_object_get(root, key);
	return j ? (float)json_number_value(j) : def;
}
inline int jsonInt(json_t* root, const char* key, int def) {
	json_t* j = json_object_get(root, key);
	return j ? (int)json_integer_value(j) : def;
}
inline bool jsonBool(json_t* root, const char* key, bool def) {
	json_t* j = json_object_get(root, key);
	return j ? json_is_true(j) : def;
}
inline json_t* jsonFloats(const float* v, int n) {
	json_t* a = json_array();
	for (int i = 0; i < n; i++) json_array_append_new(a, json_real(v[i]));
	return a;
}
inline void readFloats(json_t* root, const char* key, float* v, int n) {
	json_t* a = json_object_get(root, key);
	if (!a || !json_is_array(a)) return;
	for (int i = 0; i < n && i < (int)json_array_size(a); i++) v[i] = (float)json_number_value(json_array_get(a, i));
}

// Light that shows a short flash.
struct Flash {
	float t = 0.f;
	void fire(float len = 0.05f) { t = len; }
	float process(float dt) { if (t > 0.f) { t -= dt; return 1.f; } return 0.f; }
};

// File dialogs (synchronous; called from menu actions on the UI thread). Empty string = cancelled.
#include <osdialog.h>
enum class FileDialog { OPEN, SAVE, FOLDER };
inline std::string fileDialog(FileDialog kind, const std::string& dir, const std::string& name, const char* filters) {
	osdialog_filters* f = filters ? osdialog_filters_parse(filters) : nullptr;
	osdialog_file_action a = kind == FileDialog::SAVE ? OSDIALOG_SAVE : kind == FileDialog::FOLDER ? OSDIALOG_OPEN_DIR : OSDIALOG_OPEN;
	char* p = osdialog_file(a, dir.empty() ? nullptr : dir.c_str(), name.empty() ? nullptr : name.c_str(), f);
	if (f) osdialog_filters_free(f);
	if (!p) return "";
	std::string s(p);
	std::free(p);
	return s;
}
