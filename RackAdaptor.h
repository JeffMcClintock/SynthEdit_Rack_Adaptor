// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2007-2026 Jeff McClintock.
//
// SynthEdit Rack Adaptor — run a VCV Rack module inside a GMPI plugin, generically.
//
// The idea: a Rack module already declares its whole configuration in its own
// constructor — config() sizes the param/input/output tables, configParam()
// carries names, ranges and defaults, configInput()/configOutput() carry port
// names. This adaptor constructs one instance, reads that configuration back,
// and generates the GMPI plugin XML to suit. Per-module code shrinks to:
// compile upstream's .cpp against rack/plugin.hpp, and one registerModule()
// call.
//
// Usage (in the plugin's .cpp):
//
//     #include "RackAdaptor.h"
//     #include "vcv/Fade.cpp"   // upstream, verbatim — see ORDERING below
//
//     namespace {
//     auto r = rack_adaptor::registerModule("Fade",
//         { .id = "VCV: Fade", .category = "VCV Fundamental", .vendor = "VCV (ported)" },
//         [] { return rack_adaptor::createProcessor("Fade"); });
//     }
//
// ORDERING: registerModule() looks the slug up in ModelRegistry at static-init
// time, so upstream's `Model* modelX = createModel<...>(slug);` must have run
// first. Within one translation unit dynamic initialization runs top to
// bottom, so #include the upstream .cpp ABOVE the registerModule() call and
// the order is guaranteed. Across translation units it is not — keep the pair
// together.
//
// Generated pin order (this is the contract patch-point XML and hosts see):
//     [0 .. nIn)                every VCV input port, in VCV's order
//     [nIn .. nIn+nOut)         every VCV output port
//     [nIn+nOut .. +nParams)    one parameter pin per VCV param
//
// VOLTS: Rack modules think in volts (audio ±5V, CV 0-10V or ±5V) while GMPI
// signals are nominally 1.0 = 10V. The bridge multiplies by 10 on the way in
// and divides on the way out, so a GMPI signal of 0.5 reads as 5V inside the
// module and upstream idioms like `cv / 10.f` see the units they expect.
// Parameters are NOT scaled: a param pin carries the raw value the module
// declared its range in (a 0..1 crossfade is 0..1 on the pin).

#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>

#include "Processor.h"
#include "plugin.hpp"          // the Rack mock (same include upstream compiles against)
#include "RackPanelLayout.h"    // where the module says its controls are

namespace rack_adaptor
{

// GMPI/SynthEdit convention: a float signal of 1.0 represents 10 Volts.
inline constexpr float voltsPerUnit = 10.0f;

struct RegistrationOptions
{
	const char* id = nullptr;     // GMPI plugin id, e.g. "VCV: Fade"
	const char* name = nullptr;   // display name; defaults to the slug
	const char* category = "VCV";
	const char* vendor = "VCV (ported)";

	// Patch points are generated from the module's own widget — see
	// generatePatchPoints(). Set false to omit them.
	bool patchPoints = true;

	// Hit radius in pixels. 0 = derive from each jack's own size, which is
	// what the component types in plugin.hpp declare.
	int patchPointRadius = 0;

	// Raw XML appended inside <Plugin>. NO XML comments in here; SynthEdit's
	// parser rejects them (learned the hard way).
	const char* extraXml = nullptr;
};

namespace detail
{
	inline void appendEscaped(std::string& out, const std::string& s)
	{
		for (char c : s)
		{
			switch (c)
			{
			case '&':  out += "&amp;";  break;
			case '<':  out += "&lt;";   break;
			case '>':  out += "&gt;";   break;
			case '"':  out += "&quot;"; break;
			default:   out += c;        break;
			}
		}
	}

	inline std::string formatFloat(float v)
	{
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%g", v);
		return buf;
	}
} // namespace detail

// Generate <PatchPoints> from the panel layout the module declared.
//
// Positions are Rack panel pixels, the same space the panel SVG uses and
// therefore the space SynthEdit wants. They are rounded because SynthEdit
// reads `center` with StringToInt.
//
// pinBase IS NOT ZERO, and getting it wrong points every jack at the wrong
// pin. `pinId` indexes SynthEdit's DOCUMENT pin list, which is not the
// <Audio> list: a module's GUI pins are numbered ahead of its audio pins. So
// with N parameters (hence N <GUI> pins), audio pin k is document pin N + k.
//
// Determined empirically, and worth re-checking if SynthEdit changes: connect
// something to the module and see which pin the host says you hit. For Fade
// (2 params, 3 inputs) In1 is portId 1 and lands on document pin 3.
inline std::string generatePatchPoints(const PanelLayout& layout,
                                       std::size_t pinBase,
                                       const RegistrationOptions& opt)
{
	std::string pts;

	auto emit = [&](const ControlLayout& c, size_t pinIndex)
	{
		int radius = opt.patchPointRadius;
		if (radius <= 0)
			radius = static_cast<int>(std::lround(c.radius()));

		if (radius <= 0)
			return;   // no declared size and no override — skip rather than guess

		pts += "    <PatchPoint pinId=\"" + std::to_string(pinIndex)
		     + "\" center=\"" + std::to_string(static_cast<int>(std::lround(c.x)))
		     + ","            + std::to_string(static_cast<int>(std::lround(c.y)))
		     + "\" radius=\"" + std::to_string(radius) + "\" />\n";
	};

	// Audio pin order is inputs then outputs, shifted past the GUI pins.
	for (const auto& c : layout.inputs)
		emit(c, pinBase + static_cast<size_t>(c.id));
	for (const auto& c : layout.outputs)
		emit(c, pinBase + layout.numInputs + static_cast<size_t>(c.id));

	if (pts.empty())
		return {};

	return "  <PatchPoints>\n" + pts + "  </PatchPoints>\n";
}

// Build the GMPI plugin XML from what the module's own constructor declared.
// Constructs a throwaway instance purely to read the configuration.
inline std::string generatePluginXml(rack::Model& model, const RegistrationOptions& opt)
{
	std::unique_ptr<rack::Module> probe(model.createModule());
	assert(probe && "Model::createModule returned null");

	std::string x;
	x += "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n";

	x += "<Plugin id=\"";
	detail::appendEscaped(x, opt.id);
	x += "\" name=\"";
	detail::appendEscaped(x, opt.name ? opt.name : model.slug.c_str());
	x += "\" category=\"";
	detail::appendEscaped(x, opt.category);
	x += "\" vendor=\"";
	detail::appendEscaped(x, opt.vendor);
	x += "\">\n";

	// Parameters, from configParam(). Defaults carry through; ranges live in
	// paramQuantities if a host-side mapping ever wants them.
	x += "  <Parameters>\n";
	for (size_t i = 0; i < probe->params.size(); ++i)
	{
		const auto& q = probe->paramQuantities[i];
		x += "    <Parameter id=\"" + std::to_string(i) + "\" name=\"";
		detail::appendEscaped(x, q.name.empty() ? ("Param " + std::to_string(i)) : q.name);
		x += "\" datatype=\"float\" default=\"" + detail::formatFloat(q.defaultValue) + "\"/>\n";
	}
	x += "  </Parameters>\n";

	// The editor registers separately (withId / registerEditor), but its pins
	// are declared here. One per parameter, in paramId order — RackEditor
	// constructs its Pin<float>s in the same order, and GMPI indexes editor
	// pins by construction order, so the two line up.
	x += "  <GUI graphicsApi=\"GmpiGui\">\n";
	for (size_t i = 0; i < probe->params.size(); ++i)
	{
		const auto& q = probe->paramQuantities[i];
		x += "    <Pin name=\"";
		detail::appendEscaped(x, q.name.empty() ? ("Param " + std::to_string(i)) : q.name);
		x += "\" private=\"true\" parameterId=\"" + std::to_string(i) + "\" />\n";
	}
	x += "  </GUI>\n";

	// Audio pins — inputs, then outputs, then one pin per parameter. This
	// order is mirrored exactly by RackProcessor's pin construction; the two
	// must never diverge.
	x += "  <Audio>\n";
	for (size_t i = 0; i < probe->inputs.size(); ++i)
	{
		const auto& n = probe->inputNames[i];
		x += "    <Pin name=\"";
		detail::appendEscaped(x, n.empty() ? ("In " + std::to_string(i + 1)) : n);
		x += "\" datatype=\"float\" rate=\"audio\" />\n";
	}
	for (size_t i = 0; i < probe->outputs.size(); ++i)
	{
		const auto& n = probe->outputNames[i];
		x += "    <Pin name=\"";
		detail::appendEscaped(x, n.empty() ? ("Out " + std::to_string(i + 1)) : n);
		x += "\" datatype=\"float\" rate=\"audio\" direction=\"out\" />\n";
	}
	for (size_t i = 0; i < probe->params.size(); ++i)
	{
		x += "    <Pin parameterId=\"" + std::to_string(i) + "\" />\n";
	}
	x += "  </Audio>\n";

	// Jack positions, from the module's own widget. The GUI pins emitted above
	// are numbered ahead of the audio pins in SynthEdit's document pin list,
	// so they offset every patch point — see generatePatchPoints.
	if (opt.patchPoints)
		x += generatePatchPoints(readPanelLayout(model), probe->params.size(), opt);

	if (opt.extraXml)
		x += opt.extraXml;

	x += "</Plugin>\n";
	return x;
}

// The generic bridge: owns a module built from its Model and drives it with
// GMPI audio. One instance per plugin instance.
class RackProcessor : public gmpi::Processor
{
public:
	explicit RackProcessor(const char* slug)
	{
		auto* model = rack::ModelRegistry::instance().find(slug);
		assert(model && "VCV model not registered - was the upstream .cpp included?");

		module.reset(model->createModule());
		assert(module && "Model::createModule returned null");

		// GMPI pins self-register with the Processor under construction, in
		// construction order (Processor::constructingInstance), so emplacing
		// them here — inputs, outputs, then parameter pins — reproduces
		// generatePluginXml()'s pin order exactly. std::deque, not vector:
		// deque never relocates existing elements, and the framework holds
		// pointers to each registered pin.
		for (size_t i = 0; i < module->inputs.size(); ++i)
			audioIns.emplace_back();
		for (size_t i = 0; i < module->outputs.size(); ++i)
			audioOuts.emplace_back();
		for (size_t i = 0; i < module->params.size(); ++i)
			paramPins.emplace_back();

		// Every port has a GMPI pin and every pin has a buffer, so from the
		// module's point of view everything is patched. Modules commonly
		// return early from process() when no output is connected; this is
		// what keeps them running. (See the normalling caveat on
		// Port::isConnected in plugin.hpp.)
		for (auto& p : module->inputs)  p.connected = true;
		for (auto& p : module->outputs) p.connected = true;

		setSubProcess(&RackProcessor::subProcess);
	}

	gmpi::ReturnCode open(gmpi::api::IUnknown* phost) override
	{
		const auto rc = gmpi::Processor::open(phost);

		if (host)
		{
			args.sampleRate = host->getSampleRate();
			args.sampleTime = 1.0f / args.sampleRate;
		}

		return rc;
	}

	void onSetPins() override
	{
		// Parameter pins carry the module's raw values — no scaling.
		for (size_t i = 0; i < paramPins.size(); ++i)
			module->params[i].setValue(paramPins[i]);
	}

	void subProcess(int sampleFrames)
	{
		// One process() call per sample: Rack's contract is one invocation
		// per frame with state kept between calls. A virtual call plus a
		// 4-wide SIMD path per mono sample is the honest price of running
		// upstream's code unmodified; optimise later, if ever.
		for (int i = 0; i < sampleFrames; ++i)
		{
			for (size_t p = 0; p < audioIns.size(); ++p)
				module->inputs[p].setVoltage(getBuffer(audioIns[p])[i] * voltsPerUnit);

			module->process(args);
			++args.frame;

			for (size_t p = 0; p < audioOuts.size(); ++p)
				getBuffer(audioOuts[p])[i] = module->outputs[p].getVoltage() / voltsPerUnit;
		}
	}

private:
	std::unique_ptr<rack::Module> module;
	std::deque<gmpi::AudioInPin>  audioIns;
	std::deque<gmpi::AudioOutPin> audioOuts;
	std::deque<gmpi::FloatInPin>  paramPins;
	rack::Module::ProcessArgs     args{};
};

// For the create callback passed to registerModule(). GMPI's CreatePluginPtr
// is a plain function pointer, so the callback must be capture-less — it can
// still hand a string literal to this function:
//     [] { return rack_adaptor::createProcessor("Fade"); }
inline gmpi::api::IUnknown* createProcessor(const char* slug)
{
	auto* p = new RackProcessor(slug);

	// Clear the thread_local the pins registered through, exactly as
	// Register<>::withXml does. Leaving it dangling would let a later
	// self-registering pin attach itself to a destroyed processor.
	p->constructingInstance = {};

	return static_cast<gmpi::api::IUnknown*>(static_cast<gmpi::api::IProcessor*>(p));
}

// Generate the XML from the registered Model and register the processor.
// Safe to pass the generated string's c_str(): the GMPI factory copies it
// (MpFactory::RegisterPluginWithXml does `std::string xmlstr{xml}`).
inline bool registerModule(const char* slug, const RegistrationOptions& opt, gmpi::CreatePluginPtr create)
{
	auto* model = rack::ModelRegistry::instance().find(slug);
	assert(model && "VCV model not registered - #include the upstream .cpp ABOVE registerModule() in the same file");
	if (!model)
		return false;

	const auto xml = generatePluginXml(*model, opt);
	gmpi::RegisterPluginWithXml(gmpi::api::PluginSubtype::Audio, xml.c_str(), create);

	return false; // value unused; a namespace-scope `auto r =` needs one.
}

} // namespace rack_adaptor
