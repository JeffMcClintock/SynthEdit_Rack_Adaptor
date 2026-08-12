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
// compile upstream's .cpp against rack/rack.hpp, and one registerModule()
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
#include <optional>
#include <string>

#include "Processor.h"
#include "rack.hpp"          // the Rack mock (same include upstream compiles against)
#include "RackPanelLayout.h"    // where the module says its controls are
#include "RackDisplayState.h"   // how a custom display gets its DSP state

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

// How many of a module's menu entries carry a parameter. Separators do not,
// so this is not menu.size(), and getting it wrong shifts every light
// parameter onto a menu option.
inline std::size_t menuOptionCount(const std::vector<MenuOption>& menu)
{
	std::size_t count = 0;
	for (const auto& m : menu)
	{
		if (m.paramIndex >= 0)
			++count;
	}
	return count;
}

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
// `pinId` is the DSP pin index — a plain 0-based index into the <Audio>
// section, exactly as the field name (patchpoint_description::dspPin) says.
// It is NOT the document/connection pin numbering that a host reports when you
// wire modules together; that one counts a module's GUI pins first, and using
// it here shifts every jack onto the wrong pin.
//
// The lookup that settles it is Module_Info::getPinDescriptionById, which
// searches `plugs` (audio only). GUI pins live in a separate `gui_plugs` map
// and are never in play. SynthEdit's own SE Patch Point out corroborates:
// <Audio> is Input, Output and its patch point is pinId="1" — the audio index.
//
// Getting this wrong is quiet and nasty rather than an error: the jacks still
// draw and still accept cables, but each one is wired to a different pin than
// its label. Inputs behave as outputs, and the last jacks land on parameter
// pins, which crashes when audio starts.
inline std::string generatePatchPoints(const PanelLayout& layout,
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

	// Matches the <Audio> section this generator emits: inputs, then outputs.
	for (const auto& c : layout.inputs)
		emit(c, static_cast<size_t>(c.id));
	for (const auto& c : layout.outputs)
		emit(c, layout.numInputs + static_cast<size_t>(c.id));

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
		const auto& q = *probe->paramQuantities[i];
		x += "    <Parameter id=\"" + std::to_string(i) + "\" name=\"";
		detail::appendEscaped(x, q.name.empty() ? ("Param " + std::to_string(i)) : q.name);
		x += "\" datatype=\"float\" default=\"" + detail::formatFloat(q.defaultValue) + "\"/>\n";
	}
	// Context-menu options become parameters too, appended after the module's
	// own. Rack keeps these as plain ints on the module and offers them through
	// appendContextMenu(); as GMPI parameters they persist with the preset and
	// show up in the host, and the editor still offers the right-click menu.
	// Read once: the menu, the jack positions and which ports are placed all
	// come from the same walk of the module widget.
	const auto layout = readPanelLayout(model);
	const auto& menuOptions = layout.menu;

	for (const auto& m : menuOptions)
	{
		if (m.paramIndex < 0)
			continue;   // separator — display only, it carries no parameter

		x += "    <Parameter id=\"" + std::to_string(probe->params.size() + m.paramIndex) + "\" name=\"";
		detail::appendEscaped(x, m.name.empty() ? ("Option " + std::to_string(m.paramIndex)) : m.name);

		if (m.kind == MenuOptionKind::BoolPtr)
		{
			// A plain on/off; SynthEdit renders it as a checkbox in the host.
			x += "\" datatype=\"bool\" default=\"" + std::to_string(m.defaultValue) + "\"/>\n";
			continue;
		}

		x += "\" datatype=\"enum\" default=\"" + std::to_string(m.defaultValue) + "\" metadata=\"";

		for (size_t l = 0; l < m.labels.size(); ++l)
		{
			if (l) x += ",";
			detail::appendEscaped(x, m.labels[l]);
		}
		x += "\"/>\n";
	}

	// Lights. Each one becomes a private, non-persistent parameter written by
	// the DSP and read by the editor — the same shape SynthEdit's own scope and
	// meters use to get data from the audio thread to the GUI. They are not
	// user parameters: `private` keeps them out of the host's automation list,
	// `ignorePatchChange` and `persistant="false"` keep a blinking LED from
	// marking the patch dirty or being saved into it.
	//
	// Only the lights the panel actually SHOWS get one. A module that declares
	// lights and never places a widget for them costs nothing.
	const std::size_t lightParamBase = probe->params.size() + menuOptionCount(menuOptions);

	for (std::size_t i = 0; i < layout.displayedLightIds.size(); ++i)
	{
		x += "    <Parameter id=\"" + std::to_string(lightParamBase + i) + "\" name=\"Light ";
		x += std::to_string(layout.displayedLightIds[i]);
		x += "\" datatype=\"float\" private=\"true\" ignorePatchChange=\"true\" persistant=\"false\"/>\n";
	}

	// The display-state blob: the channel a custom display's DSP state travels
	// on. Same shape as a light — private, not persisted — carrying bytes.
	//
	// EMITTED UNCONDITIONALLY, even for the majority of modules that will never
	// send anything on it, and that is deliberate. Whether a module HAS display
	// state is only known once RACK_DISPLAY_STATE has run, and that sits after
	// the #include of upstream's source — while this function runs from
	// createModel(), which is at the BOTTOM of that same file and therefore
	// EARLIER. Asking here would always answer "no".
	//
	// The alternative was a second macro before the include to declare intent,
	// which is an ordering rule for the next porter to get wrong. One unused
	// private parameter is the cheaper mistake: the processor never writes it,
	// the editor never reads it, and nothing shows it to a user.
	const std::size_t displayStateParamId = lightParamBase + layout.displayedLightIds.size();

	x += "    <Parameter id=\"" + std::to_string(displayStateParamId);
	x += "\" name=\"Display State\" datatype=\"blob\" private=\"true\"";
	x += " ignorePatchChange=\"true\" persistant=\"false\"/>\n";

	x += "  </Parameters>\n";

	// The editor registers separately (withId / registerEditor), but its pins
	// are declared here. One per parameter, in paramId order — RackEditor
	// constructs its Pin<float>s in the same order, and GMPI indexes editor
	// pins by construction order, so the two line up.
	x += "  <GUI graphicsApi=\"GmpiGui\">\n";
	for (size_t i = 0; i < probe->params.size(); ++i)
	{
		const auto& q = *probe->paramQuantities[i];
		x += "    <Pin name=\"";
		detail::appendEscaped(x, q.name.empty() ? ("Param " + std::to_string(i)) : q.name);
		x += "\" private=\"true\" parameterId=\"" + std::to_string(i) + "\" />\n";
	}
	for (const auto& m : menuOptions)
	{
		if (m.paramIndex < 0)
			continue;

		x += "    <Pin name=\"";
		detail::appendEscaped(x, m.name);
		x += "\" private=\"true\" parameterId=\"" + std::to_string(probe->params.size() + m.paramIndex) + "\" />\n";
	}
	for (std::size_t i = 0; i < layout.displayedLightIds.size(); ++i)
	{
		x += "    <Pin name=\"Light " + std::to_string(layout.displayedLightIds[i]);
		x += "\" datatype=\"float\" private=\"true\" parameterId=\"" + std::to_string(lightParamBase + i) + "\" />\n";
	}
	x += "    <Pin name=\"Display State\" datatype=\"blob\" private=\"true\" parameterId=\"";
	x += std::to_string(displayStateParamId) + "\" />\n";
	x += "  </GUI>\n";

	// Audio pins — inputs, then outputs, then one pin per parameter. This
	// order is mirrored exactly by RackProcessor's pin construction; the two
	// must never diverge.
	// A port that has a patch point is meant to be patched on the panel, so its
	// structure-view pin is minimised to keep the module from sprouting a row
	// of pins nobody uses — the same thing SE Patch Point out does with its own
	// output. Ports WITHOUT a patch point stay visible: a module can declare a
	// port and never place a widget for it, and minimising that one would leave
	// it with no way in at all.
	const auto hasPatchPoint = [](const std::vector<ControlLayout>& placed, size_t portId)
	{
		for (const auto& c : placed)
		{
			if (static_cast<size_t>(c.id) == portId)
				return true;
		}
		return false;
	};

	x += "  <Audio>\n";
	for (size_t i = 0; i < probe->inputs.size(); ++i)
	{
		const auto& n = probe->inputNames[i];
		x += "    <Pin name=\"";
		detail::appendEscaped(x, n.empty() ? ("In " + std::to_string(i + 1)) : n);
		x += "\" datatype=\"float\" rate=\"audio\"";
		if (opt.patchPoints && hasPatchPoint(layout.inputs, i))
			x += " isMinimised=\"true\"";
		x += " />\n";
	}
	for (size_t i = 0; i < probe->outputs.size(); ++i)
	{
		const auto& n = probe->outputNames[i];
		x += "    <Pin name=\"";
		detail::appendEscaped(x, n.empty() ? ("Out " + std::to_string(i + 1)) : n);
		x += "\" datatype=\"float\" rate=\"audio\" direction=\"out\"";
		if (opt.patchPoints && hasPatchPoint(layout.outputs, i))
			x += " isMinimised=\"true\"";
		x += " />\n";
	}
	for (size_t i = 0; i < probe->params.size(); ++i)
	{
		x += "    <Pin parameterId=\"" + std::to_string(i) + "\" />\n";
	}
	for (const auto& m : menuOptions)
	{
		if (m.paramIndex >= 0)
			x += "    <Pin parameterId=\"" + std::to_string(probe->params.size() + m.paramIndex) + "\" />\n";
	}
	// direction="out": the DSP writes these, the GUI reads them. Everything
	// else in this section is an input.
	for (std::size_t i = 0; i < layout.displayedLightIds.size(); ++i)
	{
		x += "    <Pin name=\"Light " + std::to_string(layout.displayedLightIds[i]);
		x += "\" direction=\"out\" datatype=\"float\" private=\"true\" parameterId=\"";
		x += std::to_string(lightParamBase + i) + "\" />\n";
	}
	x += "    <Pin name=\"Display State\" direction=\"out\" datatype=\"blob\" private=\"true\" parameterId=\"";
	x += std::to_string(displayStateParamId) + "\" />\n";
	x += "  </Audio>\n";

	// Jack positions, from the module's own widget. Indexed against the <Audio>
	// section above, not the host's document pin numbering — see
	// generatePatchPoints.
	if (opt.patchPoints)
		x += generatePatchPoints(layout, opt);

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

		// Context-menu options, bound to THIS module instance — the editor has
		// its own copy, and writing the chosen index has to land in the one
		// that is actually processing audio.
		layout = readPanelLayout(*model, module.get());

		// Pins must be CONSTRUCTED in declaration order, because GMPI numbers
		// them by construction order and the XML lists them that way. The two
		// kinds need different pin types, so they live in separate containers
		// but are interleaved here — MenuOption::slot says which entry of its
		// own container an option owns.
		for (const auto& m : layout.menu)
		{
			switch (m.kind)
			{
			case MenuOptionKind::IndexPtr: menuIntPins.emplace_back();  break;
			case MenuOptionKind::BoolPtr:  menuBoolPins.emplace_back(); break;
			default: break;   // separators have no pin
			}
		}

		// The display-state blob is last of all in the <Audio> section, so it
		// is constructed after the lights below. Look the codec up now, while
		// the module is to hand.
		displayState = findDisplayState(*module);

		// Light outputs, last in the <Audio> section and therefore last here.
		// These are the one thing the DSP SENDS rather than receives: the
		// module computes an LED's brightness in process() and the editor has
		// no way to know it otherwise, owning a different module instance.
		for (std::size_t i = 0; i < layout.displayedLightIds.size(); ++i)
			lightPins.emplace_back();

		// Constructed whether or not this module has state to send, because the
		// pin is in the <Audio> list either way and pins are numbered by
		// construction order.
		displayStatePin.emplace();

		if (displayState)
			displayStateBytes.resize(displayState->size);

		// Every port has a GMPI pin and every pin has a buffer, so from the
		// module's point of view everything is patched. Modules commonly
		// return early from process() when no output is connected; this is
		// what keeps them running. (See the normalling caveat on
		// Port::isConnected in plugin.hpp.)
		for (auto& p : module->inputs)  p.connected = true;
		for (auto& p : module->outputs) p.connected = true;

		// Put the module's loose state — internal DSP state, and members that
		// are not parameters — into the values it documents, the way Rack's
		// "Initialize" does. Modules are entitled to assume this has happened.
		//
		// It has to be HERE and cannot move to open() or later: the host pushes
		// initial parameter values after construction, and a reset running
		// afterwards would throw them away and snap every option back to its
		// default.
		module->onReset(rack::Module::ResetEvent{});

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

		// Menu options write straight into the module member the module itself
		// handed us in appendContextMenu().
		for (const auto& m : layout.menu)
		{
			if (m.slot < 0)
				continue;

			const size_t slot = static_cast<size_t>(m.slot);

			if (m.kind == MenuOptionKind::IndexPtr && slot < menuIntPins.size())
				m.write(menuIntPins[slot]);
			else if (m.kind == MenuOptionKind::BoolPtr && slot < menuBoolPins.size())
				m.write(menuBoolPins[slot] ? 1 : 0);
		}
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

		sendLights(sampleFrames);
		sendDisplayState(sampleFrames);
	}

	// Ship the module's display state to the editor.
	//
	// AT DISPLAY RATE, NOT BLOCK RATE. This is a picture — Scope's buffer alone
	// is 64KB — and sending it every block would be two orders of magnitude
	// more traffic than anyone can see. Roughly 30 times a second is the
	// budget, derived from the sample rate so it holds at any block size.
	//
	// The pin still compares before sending, so a module sitting idle with an
	// unchanging buffer costs a memcmp and no traffic at all.
	void sendDisplayState(int sampleFrames)
	{
		if (!displayState || !displayStatePin)
			return;

		displayStateCountdown -= sampleFrames;
		if (displayStateCountdown > 0)
			return;

		displayStateCountdown = (int)(args.sampleRate / displayStateHz);

		displayState->capture(*module, displayStateBytes.data());
		displayStatePin->setValue(displayStateBytes, getBlockPosition() + sampleFrames - 1);
	}

	// Push each displayed light's brightness to the editor.
	//
	// Once per chunk, not per sample: a light is a GUI value, and modules
	// already rate-limit their own updates with a ClockDivider. setValue only
	// sends when the value actually changes, so a steady LED costs one
	// comparison per chunk and no traffic at all.
	//
	// Quantised to 1/256 for the same reason: a VU-style light computed with a
	// smoothing filter never settles exactly, and without this every chunk
	// would send a new value forever.
	//
	// THE BLOCK POSITION IS NOT OPTIONAL HERE. An output pin may only default
	// it while the processor sits exactly on an event boundary, which is true
	// in onSetPins() and false throughout subProcess() — Processor asserts on
	// it in debug builds. getBlockPosition() is the offset of the CHUNK we were
	// handed, not of where we are inside it, so the value we just computed
	// belongs to its last sample.
	void sendLights(int sampleFrames)
	{
		if (lightPins.empty() || sampleFrames <= 0)
			return;

		const int blockPosition = getBlockPosition() + sampleFrames - 1;

		for (std::size_t i = 0; i < lightPins.size(); ++i)
		{
			const int id = layout.displayedLightIds[i];
			if (id < 0 || (std::size_t)id >= module->lights.size())
				continue;

			const float b = std::clamp(module->lights[id].getBrightness(), 0.0f, 1.0f);
			lightPins[i].setValue(std::round(b * 256.0f) / 256.0f, blockPosition);
		}
	}

private:
	std::unique_ptr<rack::Module> module;
	std::deque<gmpi::AudioInPin>  audioIns;
	std::deque<gmpi::AudioOutPin> audioOuts;
	std::deque<gmpi::FloatInPin>  paramPins;
	std::deque<gmpi::IntInPin>    menuIntPins;
	std::deque<gmpi::BoolInPin>   menuBoolPins;
	std::deque<gmpi::FloatOutPin> lightPins;   // DSP -> editor, one per displayed light

	// DSP -> editor, the state a custom display draws from. Absent unless the
	// port declared some with RACK_DISPLAY_STATE. std::optional because the
	// pin registers itself on construction, so it must not be constructed at
	// all when there is no pin for it in the XML.
	const DisplayStateCodec*        displayState{};
	std::optional<gmpi::BlobOutPin> displayStatePin;
	gmpi::Blob                      displayStateBytes;

	static constexpr float displayStateHz = 30.0f;
	int                    displayStateCountdown{};
	PanelLayout                   layout;      // menu targets bound to `module`
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
