// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2007-2026 Jeff McClintock.
//
// RackEditor — the GUI counterpart of RackProcessor.
//
// Draws a Rack module's panel SVG and makes its knobs work, generically. Like
// RackProcessor it names no module type: the panel art comes in as a string,
// and every control's position, size and range comes from the module's own
// ModuleWidget via readPanelLayout().
//
// Usage (in the plugin's GUI .cpp):
//
//     #include "RackEditor.h"
//     #include "FadePanelSvg.h"        // panel art as a string literal
//
//     namespace {
//     auto r = rack_adaptor::registerEditor("VCV: Fade",
//         [] { return rack_adaptor::createEditor("Fade", kFadePanelSvg); });
//     }
//
// The upstream module's .cpp must already have been compiled into the plugin
// (the DSP .cpp includes it), so its Model is in the registry by the time this
// runs.
//
// WHAT THIS DOES NOT DO: draw the knob caps, jacks or screws. Fundamental's
// panel SVGs already carry that artwork, so the editor draws only the moving
// part — an indicator line per knob. Modules whose panels do NOT include the
// component art will look bare until someone teaches this to render the
// component types themselves.

#pragma once

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <deque>
#include <string>

#include "helpers/GmpiPluginEditor.h"
#include "helpers/SvgParser.h"

#include "RackPanelLayout.h"

namespace rack_adaptor
{

// Rack knobs sweep 300 degrees, centred on 12 o'clock.
inline constexpr float knobSweepDegrees = 150.0f;

// A full sweep takes this many pixels of vertical drag.
inline constexpr float knobDragPixels = 200.0f;

class RackEditor : public gmpi::editor::PluginEditor
{
public:
	// panelSvg is optional: pass nullptr and the panel named by the module's
	// own createPanel() call is looked up in rack::PanelRegistry, which is
	// where the build stages res/*.svg. Resolving here rather than at
	// static-init time is deliberate — see PanelRegistry.
	explicit RackEditor(const char* slug, const char* panelSvg = nullptr)
	{
		auto* model = rack::ModelRegistry::instance().find(slug);
		assert(model && "VCV model not registered - is the module's .cpp compiled into this plugin?");
		if (model)
			layout = readPanelLayout(*model);

		if (!panelSvg && !layout.panelPath.empty())
			panelSvg = rack::PanelRegistry::instance().find(layout.panelPath);

		assert(panelSvg && "no panel art - is the generated panel-resources header compiled in?");
		if (panelSvg)
			svg = panelSvg;

		panelSize = intrinsicSize(svg.c_str());

		// One pin per parameter, in paramId order, matching the <GUI> pin
		// list generatePluginXml() emits. std::deque because the pins
		// self-register by address and must never be relocated.
		for (std::size_t i = 0; i < layout.numParams; ++i)
			paramPins.emplace_back();

		for (auto& pin : paramPins)
		{
			pin.onUpdate = [this](gmpi::editor::PinBase*)
			{
				if (drawingHost)
					drawingHost->invalidateRect(nullptr);
			};
		}
	}

	// A FIXED-SIZE editor: the same answer whatever is offered, availableSize
	// ignored. That is the SDK's convention rather than a shortcut — the VST3
	// wrapper decides a client is resizable by measuring against 0x0 and
	// 10000x10000 and seeing whether the answers differ, so a constant is
	// precisely how a plugin declares it is not resizable.
	//
	// A Eurorack panel is fixed by definition: it is an exact number of HP at
	// 1px = 1/75in, and stretching it would misplace every jack — including
	// the patch points, whose coordinates are baked into the plugin XML.
	gmpi::ReturnCode measure(const gmpi::drawing::Size* /*availableSize*/,
	                         gmpi::drawing::Size* returnDesiredSize) override
	{
		*returnDesiredSize = panelSize;
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode render(gmpi::drawing::api::IDeviceContext* drawingContext) override
	{
		gmpi::drawing::Graphics g(drawingContext);
		gmpi::drawing::ClipDrawingToBounds _(g, bounds);

		// The panel does not necessarily cover the whole surface, and a host
		// is entitled to hand us a dirty one.
		g.clear(gmpi::drawing::Colors::Black);

		const auto drawn = SvgParser::drawFromMemory(g, svg);
		if (drawn.width > 0.0f && drawn.height > 0.0f)
			panelSize = drawn;

		drawKnobIndicators(g);

		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode onPointerDown(gmpi::drawing::Point point, int32_t /*flags*/) override
	{
		dragging = hitTest(point);
		lastMouse = point;

		if (dragging < 0)
			return gmpi::ReturnCode::Unhandled;   // let the host have it

		return inputHost ? inputHost->setCapture() : gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode onPointerMove(gmpi::drawing::Point point, int32_t /*flags*/) override
	{
		if (dragging >= 0)
		{
			const auto& c = layout.params[dragging];

			// Up is louder. Scale by the declared range so a full sweep is
			// knobDragPixels whatever units the module chose.
			const float delta = (lastMouse.y - point.y) / knobDragPixels
			                  * (c.maxValue - c.minValue);

			const float updated = std::clamp(value(c.id) + delta,
			                                 (std::min)(c.minValue, c.maxValue),
			                                 (std::max)(c.minValue, c.maxValue));

			paramPins[c.id] = updated;   // operator= writes through to the host

			if (drawingHost)
				drawingHost->invalidateRect(nullptr);
		}

		lastMouse = point;
		return gmpi::ReturnCode::Ok;
	}

	gmpi::ReturnCode onPointerUp(gmpi::drawing::Point /*point*/, int32_t /*flags*/) override
	{
		const bool wasDragging = dragging >= 0;
		dragging = -1;

		if (!wasDragging)
			return gmpi::ReturnCode::Unhandled;

		return inputHost ? inputHost->releaseCapture() : gmpi::ReturnCode::Ok;
	}

private:
	float value(int paramId) const
	{
		return (std::size_t)paramId < paramPins.size() ? paramPins[paramId].value : 0.0f;
	}

	// Index into layout.params, or -1.
	int hitTest(gmpi::drawing::Point p) const
	{
		int best = -1;
		float bestDistSq = 0.0f;

		for (std::size_t i = 0; i < layout.params.size(); ++i)
		{
			const auto& c = layout.params[i];
			const float dx = p.x - c.x;
			const float dy = p.y - c.y;
			const float distSq = dx * dx + dy * dy;
			const float r = c.radius();

			if (r > 0.0f && distSq <= r * r && (best < 0 || distSq < bestDistSq))
			{
				best = (int)i;
				bestDistSq = distSq;
			}
		}

		return best;
	}

	// Only the moving part — the panel art already has the knob caps.
	void drawKnobIndicators(gmpi::drawing::Graphics& g)
	{
		if (layout.params.empty())
			return;

		auto brush = g.createSolidColorBrush(gmpi::drawing::Colors::Black);

		for (const auto& c : layout.params)
		{
			const float r = c.radius();
			if (r <= 0.0f)
				continue;

			const float t = std::clamp(c.normalise(value(c.id)), 0.0f, 1.0f);
			const float degrees = -knobSweepDegrees + t * 2.0f * knobSweepDegrees;
			const float radians = degrees * 3.14159265f / 180.0f;

			// 0 degrees is 12 o'clock, positive clockwise; y grows downward.
			const float tip = r * 0.75f;
			const gmpi::drawing::Point centre{ c.x, c.y };
			const gmpi::drawing::Point end{ c.x + std::sin(radians) * tip,
			                                c.y - std::cos(radians) * tip };

			g.drawLine(centre, end, brush, (std::max)(1.0f, r * 0.12f));
		}
	}

	std::string   svg;
	PanelLayout   layout;
	gmpi::drawing::Size panelSize{ 100.0f, 100.0f };

	std::deque<gmpi::editor::Pin<float>> paramPins;

	int  dragging{ -1 };
	gmpi::drawing::Point lastMouse{};

	// The panel's declared size, needed by measure() before anything has been
	// drawn. SvgParser only reports it as a side effect of drawing, so read
	// the two attributes directly.
	static gmpi::drawing::Size intrinsicSize(const char* xml)
	{
		tinyxml2::XMLDocument doc;
		if (doc.Parse(xml) != tinyxml2::XML_SUCCESS)
			return { 100.0f, 100.0f };

		auto* svgElement = doc.FirstChildElement("svg");
		if (!svgElement)
			return { 100.0f, 100.0f };

		const gmpi::drawing::Size s{ svgElement->FloatAttribute("width"),
		                             svgElement->FloatAttribute("height") };

		return (s.width > 0.0f && s.height > 0.0f) ? s : gmpi::drawing::Size{ 100.0f, 100.0f };
	}
};

// See createProcessor: the capture-less lambda GMPI wants can still pass
// literals through to here.
inline gmpi::api::IUnknown* createEditor(const char* slug, const char* panelSvg = nullptr)
{
	auto* e = new RackEditor(slug, panelSvg);

	// Clear the thread_local the pins registered through, as
	// Register<>::withId does.
	e->constructingInstance = {};

	return static_cast<gmpi::api::IUnknown*>(static_cast<gmpi::api::IEditor*>(e));
}

inline bool registerEditor(const char* id, gmpi::CreatePluginPtr create)
{
	gmpi::RegisterPlugin(gmpi::api::PluginSubtype::Editor, id, create);
	return false; // value unused; a namespace-scope `auto r =` needs one.
}

} // namespace rack_adaptor
