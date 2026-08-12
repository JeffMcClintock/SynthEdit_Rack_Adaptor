// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2007-2026 Jeff McClintock.
//
// A MOCK of VCV Rack's plugin.hpp.
//
// PURPOSE: let VCV Rack modules' own .cpp files compile, unmodified, inside a
// GMPI plugin. Nothing here is a working implementation of Rack — it is the
// smallest set of declarations that satisfies the compiler, so that porting
// becomes "replace one stub at a time with a GMPI-backed implementation"
// rather than "rewrite the module".
//
// WHAT IS REAL vs WHAT IS A STUB
//   Real  — simd::float_4 arithmetic, clamp, sqrt, Param/Port value storage,
//           Port::isConnected(), and the config*() metadata capture that
//           RackAdaptor.h reads to generate GMPI XML. These are small and
//           exact; faking them would be no less code and would have to be
//           undone later.
//   Stub  — everything host-side: widgets, panels, menus, json, asset paths.
//           They compile and do nothing. Each is marked MOCK.
//
// Scope: driven by what the ported Fundamental modules actually reference.
// Other modules will need more; add it here as they are ported.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// ---------------------------------------------------------------------------
// jansson — Rack persists module state through it. Opaque and inert here, so
// dataToJson()/dataFromJson() compile but store nothing. In C, hence global
// namespace and C-style names.
// ---------------------------------------------------------------------------
struct json_t;

inline json_t* json_object() { return nullptr; }                            // MOCK
inline json_t* json_integer(long long) { return nullptr; }                  // MOCK
inline long long json_integer_value(json_t*) { return 0; }                  // MOCK
inline json_t* json_object_get(json_t*, const char*) { return nullptr; }    // MOCK
inline void json_object_set_new(json_t*, const char*, json_t*) {}           // MOCK

namespace rack {

// ---------------------------------------------------------------------------
// math
// ---------------------------------------------------------------------------
struct Vec
{
	float x{}, y{};
	Vec() = default;
	Vec(float x, float y) : x(x), y(y) {}
};

struct Rect
{
	Vec pos, size;
};

inline float clamp(float x, float lo, float hi)
{
	return std::min(std::max(x, lo), hi);
}

// ---------------------------------------------------------------------------
// simd — Rack's 4-wide float. REAL: a plain array with the operators the
// Fundamental modules need. Scalar, not vectorised; correctness first, and
// the compiler auto-vectorises this shape well anyway.
// ---------------------------------------------------------------------------
namespace simd {

struct float_4
{
	float v[4]{};

	float_4() = default;
	float_4(float a) { v[0] = v[1] = v[2] = v[3] = a; }          // implicit: modules assign a float to a float_4
	float_4(float a, float b, float c, float d) { v[0] = a; v[1] = b; v[2] = c; v[3] = d; }

	float&       operator[](int i)       { return v[i]; }
	const float& operator[](int i) const { return v[i]; }
};

#define RACK_FLOAT4_BINOP(op)                                                       \
	inline float_4 operator op (float_4 a, float_4 b)                                   \
	{ return { a[0] op b[0], a[1] op b[1], a[2] op b[2], a[3] op b[3] }; }

RACK_FLOAT4_BINOP(+)
RACK_FLOAT4_BINOP(-)
RACK_FLOAT4_BINOP(*)
RACK_FLOAT4_BINOP(/)
#undef RACK_FLOAT4_BINOP

inline float_4& operator+=(float_4& a, float_4 b) { a = a + b; return a; }
inline float_4& operator-=(float_4& a, float_4 b) { a = a - b; return a; }
inline float_4& operator*=(float_4& a, float_4 b) { a = a * b; return a; }

inline float_4 sqrt(float_4 a)
{
	return { std::sqrt(a[0]), std::sqrt(a[1]), std::sqrt(a[2]), std::sqrt(a[3]) };
}

// Found by ADL from a module's unqualified clamp() on a float_4.
inline float_4 clamp(float_4 x, float lo, float hi)
{
	return { rack::clamp(x[0], lo, hi), rack::clamp(x[1], lo, hi),
	         rack::clamp(x[2], lo, hi), rack::clamp(x[3], lo, hi) };
}

} // namespace simd

// ---------------------------------------------------------------------------
// engine
// ---------------------------------------------------------------------------
struct Param
{
	float value{};
	float getValue() const   { return value; }
	void  setValue(float v)  { value = v; }
};

struct Light
{
	float value{};
};

struct Port
{
	static constexpr int PORT_MAX_CHANNELS = 16;

	float voltages[PORT_MAX_CHANNELS]{};
	int   channels{ 1 };
	bool  connected{ false };

	// Real, and load-bearing: modules commonly return from process()
	// immediately unless an output is connected. The adaptor marks the ports
	// it actually drives — see RackProcessor's constructor.
	//
	// CAVEAT: the adaptor drives every port, so a module never sees an
	// unconnected input. A module that normalises an unconnected port to a
	// fallback value (getNormalVoltage) will instead read our 0V. Revisit
	// per-module if that distinction starts to matter.
	bool isConnected() const { return connected; }

	int  getChannels() const     { return channels; }
	void setChannels(int n)      { channels = n; }

	float getVoltage(int channel = 0) const        { return voltages[channel]; }
	void  setVoltage(float v, int channel = 0)     { voltages[channel] = v; }

	// Rack's poly semantics: a monophonic cable broadcasts channel 0 to all.
	float getPolyVoltage(int channel) const
	{
		return channels == 1 ? getVoltage(0) : getVoltage(channel);
	}

	template<typename T>
	T getPolyVoltageSimd(int firstChannel) const
	{
		T out{};
		for (int i = 0; i < 4; ++i)
			out[i] = getPolyVoltage(std::min(firstChannel + i, PORT_MAX_CHANNELS - 1));
		return out;
	}

	template<typename T>
	void setVoltageSimd(T value, int firstChannel)
	{
		for (int i = 0; i < 4; ++i)
		{
			const int c = firstChannel + i;
			if (c < PORT_MAX_CHANNELS)
				voltages[c] = value[i];
		}
	}
};

using Input  = Port;
using Output = Port;

// REAL, not just accepted-and-dropped: RackAdaptor.h reads this back to
// generate the GMPI <Parameters> section, so the ranges, names and defaults
// the module declares in configParam() become the plugin's metadata.
struct ParamQuantity
{
	float minValue{}, maxValue{}, defaultValue{};
	std::string name, unit;
};

struct Module
{
	struct ProcessArgs
	{
		float   sampleRate{ 44100.0f };
		float   sampleTime{ 1.0f / 44100.0f };
		int64_t frame{};
	};

	struct ResetEvent {};
	struct RandomizeEvent {};

	std::vector<Param>  params;
	std::vector<Input>  inputs;
	std::vector<Output> outputs;
	std::vector<Light>  lights;

	std::vector<ParamQuantity> paramQuantities;

	// Captured so the adaptor can name the generated GMPI pins the way the
	// module names its ports.
	std::vector<std::string> inputNames;
	std::vector<std::string> outputNames;

	virtual ~Module() = default;

	void config(int numParams, int numInputs, int numOutputs, int numLights = 0)
	{
		params.resize(numParams);
		inputs.resize(numInputs);
		outputs.resize(numOutputs);
		lights.resize(numLights);
		paramQuantities.resize(numParams);
		inputNames.resize(numInputs);
		outputNames.resize(numOutputs);
	}

	// Sets the default so getValue() is at least plausible before anything
	// drives the param; the display arguments are dropped.
	ParamQuantity* configParam(int paramId, float minValue, float maxValue, float defaultValue,
	                           std::string name = "", std::string unit = "",
	                           float /*displayBase*/ = 0.f, float /*displayMultiplier*/ = 1.f,
	                           float /*displayOffset*/ = 0.f)
	{
		if (paramId < (int)params.size())
		{
			params[paramId].value = defaultValue;
			paramQuantities[paramId] = { minValue, maxValue, defaultValue, std::move(name), std::move(unit) };
			return &paramQuantities[paramId];
		}
		return nullptr;
	}

	void configInput(int portId, std::string name = "")
	{
		if (portId < (int)inputNames.size())
			inputNames[portId] = std::move(name);
	}

	void configOutput(int portId, std::string name = "")
	{
		if (portId < (int)outputNames.size())
			outputNames[portId] = std::move(name);
	}

	void configBypass(int, int) {}                     // MOCK

	virtual void onReset(const ResetEvent&) {}
	virtual void process(const ProcessArgs&) {}

	virtual json_t* dataToJson() { return nullptr; }
	virtual void    dataFromJson(json_t*) {}
};

// ---------------------------------------------------------------------------
// widgets — all MOCK. They build a tree that is constructed and then dropped.
// Panel dimensions in Rack are 1px = 1/75in; a HP is 5.08mm.
// ---------------------------------------------------------------------------
static constexpr float RACK_GRID_WIDTH  = 15.0f;
static constexpr float RACK_GRID_HEIGHT = 380.0f;

inline float mm2px(float mm)  { return mm * (75.0f / 25.4f); }
inline Vec   mm2px(Vec v)     { return { mm2px(v.x), mm2px(v.y) }; }

struct Widget
{
	Rect box;
	std::vector<Widget*> children;

	virtual ~Widget() = default;
	void addChild(Widget* w) { children.push_back(w); }
};

// Panel art, keyed by the path the module names in createPanel(). A plugin's
// build stages its res/*.svg into here (see the generated panel-resources
// header), so nothing has to hand the art to the editor by hand.
//
// Lookup is deliberately LAZY: createPanel() records the path and the editor
// resolves it at construction, long after static init. Resolving eagerly
// would make this race the panel registrations across translation units,
// where inline-variable initialization order is unspecified.
class PanelRegistry
{
public:
	static PanelRegistry& instance()
	{
		static PanelRegistry singleton;
		return singleton;
	}

	// Returns bool so a plugin can register with an inline variable.
	bool add(std::string path, const char* svg)
	{
		panels_.push_back({ std::move(path), svg });
		return true;
	}

	const char* find(std::string_view path) const
	{
		for (const auto& p : panels_)
		{
			if (p.path == path)
				return p.svg;
		}
		return nullptr;
	}

private:
	struct Entry { std::string path; const char* svg; };
	std::vector<Entry> panels_;
};

struct SvgPanel : Widget
{
	std::string path;   // resolved against PanelRegistry when it is needed
};
struct ParamWidget : Widget { Module* module{}; int paramId{}; };
struct PortWidget  : Widget { Module* module{}; int portId{}; };

// The concrete Fundamental control set.
//
// SIZES ARE REAL and load-bearing: generatePluginXml() turns a port widget's
// size into its patch-point hit radius. Rack takes these from each component's
// SVG, which the mock has no reader for, so the panel dimensions are stated
// here instead. A PJ301M is an 8.7mm panel jack; the knobs are sized for
// completeness and nothing reads them yet.
struct ThemedScrew      : Widget {};
struct RoundBlackKnob   : ParamWidget { RoundBlackKnob()   { box.size = mm2px(Vec(9.5f, 9.5f)); } };
struct Trimpot          : ParamWidget { Trimpot()          { box.size = mm2px(Vec(7.0f, 7.0f)); } };

// 23.7px is the literal size of Rack's res/ComponentLibrary/PJ301M.svg, so
// the jack the editor draws is the size the real artwork would have been.
struct ThemedPJ301MPort : PortWidget  { ThemedPJ301MPort() { box.size = Vec(23.7f, 23.7f); } };

// NOTE ON box.pos: for widgets made by the *Centered helpers below, the mock
// leaves box.pos holding the CENTRE. Rack converts it to a top-left; we do
// not, because the centre is exactly what a patch point needs and nothing
// here lays anything out. Read box.pos as "where createXxxCentered put it".
template<class T> T* createWidget(Vec pos)
{
	T* w = new T;
	w->box.pos = pos;
	return w;
}

template<class T> T* createParamCentered(Vec centre, Module* module, int paramId)
{
	T* w = new T;
	w->box.pos = centre;
	w->module = module;
	w->paramId = paramId;
	return w;
}

template<class T> T* createInputCentered(Vec centre, Module* module, int portId)
{
	T* w = new T;
	w->box.pos = centre;
	w->module = module;
	w->portId = portId;
	return w;
}

template<class T> T* createOutputCentered(Vec centre, Module* module, int portId)
{
	T* w = new T;
	w->box.pos = centre;
	w->module = module;
	w->portId = portId;
	return w;
}

// The light panel's path is kept — that is how the editor finds the art
// without anyone naming it twice. The dark variant is ignored for now.
inline SvgPanel* createPanel(std::string lightSvg, std::string /*darkSvg*/ = "")
{
	auto* p = new SvgPanel;
	p->path = std::move(lightSvg);
	return p;
}

// ---------------------------------------------------------------------------
// menus — MOCK
// ---------------------------------------------------------------------------
struct MenuItem : Widget {};
struct MenuSeparator : Widget {};

struct Menu : Widget {};

template<typename T>
MenuItem* createIndexPtrSubmenuItem(std::string /*name*/, std::vector<std::string> /*labels*/, T* /*ptr*/)
{
	return new MenuItem;   // MOCK
}

// ---------------------------------------------------------------------------
// module widget
// ---------------------------------------------------------------------------
struct ModuleWidget : Widget
{
	Module* module{};

	// Kept, not just parented: this is where the module states each jack's
	// position and which port it belongs to, and generatePluginXml() reads it
	// back to place the patch points. Rack exposes the same thing through
	// getInput()/getOutput().
	std::vector<ParamWidget*> paramWidgets;
	std::vector<PortWidget*>  inputWidgets;
	std::vector<PortWidget*>  outputWidgets;

	// Whatever the module passed to setPanel(createPanel("res/Foo.svg")).
	std::string panelPath;

	void setModule(Module* m) { module = m; }

	void setPanel(Widget* p)
	{
		if (auto* svg = dynamic_cast<SvgPanel*>(p))
			panelPath = svg->path;

		addChild(p);
	}

	void addParam(ParamWidget* w) { paramWidgets.push_back(w);  addChild(w); }
	void addInput(PortWidget* w)  { inputWidgets.push_back(w);  addChild(w); }
	void addOutput(PortWidget* w) { outputWidgets.push_back(w); addChild(w); }

	template<class T> T* getModule() { return dynamic_cast<T*>(module); }

	virtual void appendContextMenu(Menu*) {}
};

// ---------------------------------------------------------------------------
// plugin / model registration
// ---------------------------------------------------------------------------
struct Plugin {};

inline Plugin* pluginInstance = nullptr;

namespace asset {
inline std::string plugin(Plugin*, std::string filename) { return filename; }   // MOCK
}

struct Model
{
	std::string slug;

	virtual ~Model() = default;
	virtual Module*       createModule() { return nullptr; }
	virtual ModuleWidget* createModuleWidget(Module*) { return nullptr; }
};

} // namespace rack

// Declared, not defined: RackAutoRegister.h supplies the body, and createModel
// below calls it so that a module registers itself with GMPI simply by being
// compiled. The call is dependent on createModel's template arguments, so it
// is looked up when createModel is instantiated — by which point the
// definition is visible, because the module's .cpp is #included AFTER
// RackModule.h. Include only plugin.hpp and you get a link error naming this,
// which is the intended signpost.
//
// Define RACK_NO_AUTO_REGISTER before the module's .cpp to opt out and call
// rack_adaptor::registerModule() yourself.
namespace rack_adaptor
{
template<class TModule, class TModuleWidget>
void autoRegisterModel(rack::Model& model);
}

namespace rack {

// Every Model that createModel() builds, findable by slug.
//
// Rack registers models in the plugin's own init(): `p->addModel(modelFade)`.
// We have no init() — but upstream's registration line, e.g.
//
//     Model* modelFade = createModel<Fade, FadeWidget>("Fade");
//
// is a namespace-scope variable whose initializer runs at static-init time,
// which is the same hook GMPI uses with `auto r = Register<T>::withXml(...)`
// (there too the value is discarded; the initializer's side effect is the
// point). So createModel() adds each Model here on the way past, and the
// adaptor can then reach a module by slug without naming its C++ type.
class ModelRegistry
{
public:
	// Function-local static, NOT a namespace-scope global. The registrations
	// happen during static init, and a global registry might not be
	// constructed yet when the first one runs — it would then be overwritten
	// by its own constructor afterwards, dropping whatever had registered.
	// Constructing on first use removes the ordering question entirely, which
	// is why SynthEdit reaches its factory through ModuleFactory() rather than
	// a bare global too.
	static ModelRegistry& instance()
	{
		static ModelRegistry singleton;
		return singleton;
	}

	void add(Model* model) { models_.push_back(model); }

	Model* find(std::string_view slug) const
	{
		for (auto* m : models_)
		{
			if (m->slug == slug)
				return m;
		}
		return nullptr;
	}

	const std::vector<Model*>& all() const { return models_; }

private:
	std::vector<Model*> models_;
};

// Mirrors Rack's own createModel, and the mirroring matters: the overrides
// below are what odr-use TModule's and TModuleWidget's constructors. Without
// them the compiler parses the module but emits nothing, and a mock missing
// something those constructors need would still "build" — the false pass this
// layer must never give.
template<class TModule, class TModuleWidget>
Model* createModel(std::string slug)
{
	struct TModel : Model
	{
		Module* createModule() override { return new TModule; }

		ModuleWidget* createModuleWidget(Module* m) override
		{
			return new TModuleWidget(m ? dynamic_cast<TModule*>(m) : nullptr);
		}
	};

	TModel* model = new TModel;   // owned by the registry, never freed.
	model->slug = std::move(slug);

	ModelRegistry::instance().add(model);

#ifndef RACK_NO_AUTO_REGISTER
	// This is the whole point: compiling a Rack module's .cpp is all it takes
	// to get a GMPI plugin out of it. No per-module registration to write, and
	// none to forget.
	rack_adaptor::autoRegisterModel<TModule, TModuleWidget>(*model);
#endif

	return model;
}

} // namespace rack

// Rack's real plugin.hpp does exactly this, which is why Fundamental's
// sources refer to Module, Vec, simd::float_4 etc. unqualified.
using namespace rack;
