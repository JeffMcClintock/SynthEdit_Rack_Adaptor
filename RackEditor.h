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
#include <cstdlib>
#include <deque>
#include <string>

#include "helpers/GmpiPluginEditor.h"
#include "helpers/SvgParser.h"
#include "helpers/ContextMenuHelper.h"

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

		metrics = panelMetrics(svg.c_str());
		panelSize = metrics.size;

		// One pin per parameter, in paramId order, matching the <GUI> pin
		// list generatePluginXml() emits. std::deque because the pins
		// self-register by address and must never be relocated.
		for (std::size_t i = 0; i < layout.numParams; ++i)
			paramPins.emplace_back();

		// Menu options come after the parameters, matching the <GUI> pin list.
		// Constructed in declaration order because GMPI numbers pins that way;
		// the two kinds need different types, so they are interleaved across
		// two containers. Separators are display only and get no pin.
		for (const auto& m : layout.menu)
		{
			switch (m.kind)
			{
			case MenuOptionKind::IndexPtr: menuIntPins.emplace_back();  break;
			case MenuOptionKind::BoolPtr:  menuBoolPins.emplace_back(); break;
			default: break;
			}
		}

		auto redraw = [this](gmpi::editor::PinBase*)
		{
			if (drawingHost)
				drawingHost->invalidateRect(nullptr);
		};

		for (auto& pin : paramPins)    pin.onUpdate = redraw;
		for (auto& pin : menuIntPins)  pin.onUpdate = redraw;
		for (auto& pin : menuBoolPins) pin.onUpdate = redraw;

		// Lights last, matching the <GUI> pin list. These are driven by the
		// PROCESSOR, not by anything here — see RackProcessor::sendLights.
		// Their redraw is narrowed to the light's own rectangle: a blinking
		// LED that invalidated the whole panel would repaint the SVG, the
		// jacks and every knob several times a second.
		for (std::size_t i = 0; i < layout.displayedLightIds.size(); ++i)
			lightPins.emplace_back();

		for (std::size_t i = 0; i < lightPins.size(); ++i)
		{
			lightPins[i].onUpdate = [this, i](gmpi::editor::PinBase*)
			{
				if (drawingHost)
				{
					const auto r = lightBounds((int)i);
					drawingHost->invalidateRect(&r);
				}
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

		// Only the panel art is scaled onto Rack's grid. The jacks and knobs
		// below are already in Rack pixels — they come from the module's own
		// widget — so they are drawn after the transform is restored.
		if (metrics.drawScale != 1.0f)
		{
			const auto base = g.getTransform();
			g.setTransform(gmpi::drawing::makeScale(metrics.drawScale, metrics.drawScale) * base);
			SvgParser::drawFromMemory(g, svg);
			g.setTransform(base);
		}
		else
		{
			SvgParser::drawFromMemory(g, svg);
		}

		drawJacks(g);
		drawKnobIndicators(g);
		drawLights(g);

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

	// Right-click menu, built from what the module declared in
	// appendContextMenu(). Each option is a submenu of its labels, and picking
	// one writes the index to that option's parameter pin — which is what
	// carries it to the processor's own module instance.
	gmpi::ReturnCode populateContextMenu(gmpi::drawing::Point /*point*/,
	                                     gmpi::api::IUnknown* contextMenuItemsSink) override
	{
		if (layout.menu.empty())
			return gmpi::ReturnCode::Unhandled;

		gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
		unknown = contextMenuItemsSink;
		auto sink = unknown.as<gmpi::api::IContextItemSink>();
		if (!sink)
			return gmpi::ReturnCode::Fail;

		ContextMenuHelper menu(sink.get());

		// A separator is only meaningful BETWEEN our own entries. The host has
		// already put its items above ours and separated them itself, so a
		// leading one lands against the host's own divider and reads as
		// belonging to the item above it. Held back and emitted only once
		// something of ours follows, which also collapses runs and drops a
		// trailing one.
		bool pendingSeparator = false;
		bool emittedAny = false;

		for (const auto& option : layout.menu)
		{
			if (option.kind == MenuOptionKind::Separator)
			{
				pendingSeparator = emittedAny;
				continue;
			}

			if (pendingSeparator)
			{
				menu.addSeparator();
				pendingSeparator = false;
			}

			const std::size_t slot = static_cast<std::size_t>((std::max)(option.slot, 0));

			if (option.kind == MenuOptionKind::BoolPtr)
			{
				// One entry that ticks on and off, as Rack shows it.
				const bool on = (slot < menuBoolPins.size())
					? menuBoolPins[slot].value : (option.defaultValue != 0);

				menu.addItem(option.name.c_str(),
					[this, slot, on](int32_t)
					{
						if (slot < menuBoolPins.size())
							menuBoolPins[slot] = !on;
					},
					on ? (int32_t)gmpi::api::PopupMenuFlags::Ticked : 0);

				emittedAny = true;
				continue;
			}

			menu.beginSubMenu(option.name.c_str());

			const int current = (slot < menuIntPins.size())
				? menuIntPins[slot].value : option.defaultValue;

			for (std::size_t i = 0; i < option.labels.size(); ++i)
			{
				// Ticked to show the current choice, exactly as Rack does.
				const int32_t flags = (static_cast<int>(i) == current)
					? (int32_t)gmpi::api::PopupMenuFlags::Ticked : 0;

				menu.addItem(option.labels[i].c_str(),
					[this, slot, i](int32_t)
					{
						if (slot < menuIntPins.size())
							menuIntPins[slot] = static_cast<int32_t>(i);
					},
					flags);
			}

			menu.endSubMenu();
			emittedAny = true;
		}

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

	// The jacks.
	//
	// A Rack panel SVG does NOT contain its jack artwork — Fundamental's put
	// the positions in a display:none "components" layer and Rack composites
	// the real graphic on top at runtime. SynthEdit draws nothing at a
	// <PatchPoint> either (they are hit targets and cable anchors only), so
	// without this the sockets are simply invisible.
	//
	// Proportions are taken from Rack's own res/ComponentLibrary/PJ301M.svg,
	// which is 23.7px across (radius 11.85) and built from concentric circles.
	// Expressed as a fraction of the outer radius so the jack scales with
	// whatever size the widget declares:
	//
	//     0.937  rim            0.881  light collar
	//     0.689  dark ring      0.574  light ring
	//     0.411  hole
	//
	// The original interleaves thin radial-gradient rings between these flat
	// fills; flat fills alone reproduce the look at this size, and the panel
	// is 45px wide.
	void drawJacks(gmpi::drawing::Graphics& g)
	{
		if (layout.inputs.empty() && layout.outputs.empty())
			return;

		using namespace gmpi::drawing;

		auto rim   = g.createSolidColorBrush(colorFromHex(0x9A9A9A));
		auto light = g.createSolidColorBrush(colorFromHex(0xE0E0E0));
		auto dark  = g.createSolidColorBrush(colorFromHex(0x1F1F1F));

		auto drawJack = [&](const ControlLayout& c)
		{
			const float r = c.radius();
			if (r <= 0.0f)
				return;

			const Point centre{ c.x, c.y };

			// Largest first; each ring paints over the one before.
			auto ring = [&](float scale, IHasBrush& brush)
			{
				g.fillEllipse(Ellipse{ centre, r * scale, r * scale }, brush);
			};

			ring(0.937f, rim);
			ring(0.881f, light);
			ring(0.689f, dark);
			ring(0.574f, light);
			ring(0.411f, dark);
		};

		for (const auto& c : layout.inputs)  drawJack(c);
		for (const auto& c : layout.outputs) drawJack(c);
	}

	// The LEDs.
	//
	// Rack's own light rendering, in the parts that show: an unlit lens is
	// always drawn (a half-grey disc at a third opacity, faintly outlined),
	// the lit colour is painted over it at an alpha equal to the brightness,
	// and a halo spreads to four times the radius.
	//
	// A bi-colour LED holds two light ids and two base colours. Rack combines
	// them with a screen blend and takes the strongest brightness as the alpha,
	// which is what makes Compare's yellow/blue indicator read as one lamp
	// changing colour rather than two lamps fighting.
	//
	// COLOUR SPACE, and this is deliberate rather than overlooked: Rack's
	// SCHEME_* constants are sRGB-encoded, and gmpi_ui's Color components are
	// linear. The components are passed straight through, so an LED renders a
	// little brighter and less saturated than the same one in Rack —
	// SCHEME_YELLOW's 0.797 green lands at 231/255 rather than 203/255.
	// Blending stays correct, which is the property worth keeping; matching
	// Rack exactly would mean adopting its sRGB blending, which is not.
	void drawLights(gmpi::drawing::Graphics& g)
	{
		using namespace gmpi::drawing;

		if (layout.lights.empty())
			return;

		for (const auto& l : layout.lights)
		{
			const float r = l.radius();
			if (r <= 0.0f)
				continue;

			const Point centre{ l.x, l.y };
			const Ellipse lens{ centre, r, r };

			// The unlit lens, always.
			auto bg = g.createSolidColorBrush(Color{ 0.5f, 0.5f, 0.5f, 0.33f });
			g.fillEllipse(lens, bg);

			auto border = g.createSolidColorBrush(Color{ 0.0f, 0.0f, 0.0f, 0.15f });
			g.drawEllipse(lens, border, (std::max)(0.5f, r * 0.1f));

			const Color lit = mixLight(l);
            if (lit.a <= 0.0f)
                continue;

			// The halo: constant out to the lens edge, fading to nothing at
			// four radii. Drawn BEFORE the lit lens so the lens stays crisp.
			const float outer = r * 4.0f;
			const Color haloColor{ lit.r, lit.g, lit.b, lit.a * 0.25f };
			const Color clear{ lit.r, lit.g, lit.b, 0.0f };

			const Gradientstop stops[]
			{
				{ 0.00f, haloColor },
				{ 0.25f, haloColor },
				{ 1.00f, clear },
			};

			auto halo = g.createRadialGradientBrush(stops, centre, outer);
			g.fillEllipse(Ellipse{ centre, outer, outer }, halo);

			auto core = g.createSolidColorBrush(lit);
			g.fillEllipse(lens, core);
		}
	}

	// Rack's MultiLightWidget::setBrightnesses, which screens each base colour
	// scaled by its own light's brightness. The alpha is the strongest of them,
	// so a light that is off everywhere disappears entirely rather than tinting
	// the lens.
	gmpi::drawing::Color mixLight(const LightLayout& l) const
	{
		gmpi::drawing::Color mixed{ 0.0f, 0.0f, 0.0f, 0.0f };

		for (std::size_t i = 0; i < l.colors.size(); ++i)
		{
			const float b = brightness(l.firstLightId + (int)i);
			if (b <= 0.0f)
				continue;

			const auto& c = l.colors[i];

			if (mixed.a <= 0.0f)
			{
				mixed = { c.r, c.g, c.b, b };
				continue;
			}

			mixed.r = 1.0f - (1.0f - mixed.r) * (1.0f - c.r);
			mixed.g = 1.0f - (1.0f - mixed.g) * (1.0f - c.g);
			mixed.b = 1.0f - (1.0f - mixed.b) * (1.0f - c.b);
			mixed.a = (std::max)(mixed.a, b);
		}

		return mixed;
	}

	float brightness(int lightId) const
	{
		const int slot = layout.lightSlot(lightId);
		if (slot < 0 || (std::size_t)slot >= lightPins.size())
			return 0.0f;

		return std::clamp(lightPins[slot].value, 0.0f, 1.0f);
	}

	// The area a light can affect, halo included — what its pin invalidates.
	gmpi::drawing::Rect lightBounds(int pinSlot) const
	{
		gmpi::drawing::Rect r{ 0.0f, 0.0f, 0.0f, 0.0f };

		if (pinSlot < 0 || (std::size_t)pinSlot >= layout.displayedLightIds.size())
			return r;

		const int lightId = layout.displayedLightIds[pinSlot];
		bool found = false;

		// One light id can appear in more than one widget; union them.
		for (const auto& l : layout.lights)
		{
			if (lightId < l.firstLightId || lightId >= l.firstLightId + (int)l.colors.size())
				continue;

			const float reach = l.radius() * 4.0f;
			const gmpi::drawing::Rect b{ l.x - reach, l.y - reach, l.x + reach, l.y + reach };

			r = found ? gmpi::drawing::Rect{ (std::min)(r.left,   b.left),
			                                 (std::min)(r.top,    b.top),
			                                 (std::max)(r.right,  b.right),
			                                 (std::max)(r.bottom, b.bottom) }
			          : b;
			found = true;
		}

		return r;
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

	// TWO DPIs, and mixing them up puts every control in the wrong place.
	//
	// Rack's panel grid is 1px = 1/75in — that is what mm2px() produces, so
	// every jack and knob position from the module's widget is in 75-dpi
	// pixels. SVG follows the CSS convention of 96dpi, and SvgParser converts
	// physical units that way (mm -> n * 96/25.4), correctly.
	//
	// A panel authored in millimetres therefore draws 96/75 = 28% too large
	// for the coordinates its own module places controls at. Most Fundamental
	// panels are authored in bare user units that already ARE Rack pixels, so
	// they need no correction and this is a no-op for them — but Inkscape
	// defaults to mm, so it turns up.
	//
	// panelMetrics() answers both questions: the size to report to the host,
	// in Rack pixels, and the scale to apply before letting SvgParser draw.
	static constexpr float rackDpi = 75.0f;
	static constexpr float cssDpi  = 96.0f;

	struct PanelMetrics
	{
		gmpi::drawing::Size size{ 100.0f, 100.0f };   // Rack pixels
		float drawScale{ 1.0f };                      // Rack px per CSS px
	};

	std::string   svg;
	PanelLayout   layout;
	gmpi::drawing::Size panelSize{ 100.0f, 100.0f };
	PanelMetrics        metrics;

	std::deque<gmpi::editor::Pin<float>>   paramPins;
	std::deque<gmpi::editor::Pin<int32_t>> menuIntPins;
	std::deque<gmpi::editor::Pin<bool>>    menuBoolPins;
	std::deque<gmpi::editor::Pin<float>>   lightPins;

	int  dragging{ -1 };
	gmpi::drawing::Point lastMouse{};


	// Returns inches for a physical unit, or a negative value when the length
	// is in bare user units / px (already Rack pixels by convention).
	static float lengthInInches(const std::string& text, float& userUnits)
	{
		userUnits = static_cast<float>(std::atof(text.c_str()));

		const auto firstAlpha = text.find_first_of("abcdefghijklmnopqrstuvwxyz%");
		if (firstAlpha == std::string::npos)
			return -1.0f;

		const std::string unit = text.substr(firstAlpha);

		if (unit == "mm") return userUnits / 25.4f;
		if (unit == "cm") return userUnits / 2.54f;
		if (unit == "in") return userUnits;
		if (unit == "pt") return userUnits / 72.0f;
		if (unit == "pc") return userUnits / 6.0f;

		return -1.0f;   // px, %, or something unrecognised — treat as user units
	}

	static PanelMetrics panelMetrics(const char* xml)
	{
		PanelMetrics m;

		tinyxml2::XMLDocument doc;
		if (doc.Parse(xml) != tinyxml2::XML_SUCCESS)
			return m;

		auto* svgElement = doc.FirstChildElement("svg");
		if (!svgElement)
			return m;

		const char* w = svgElement->Attribute("width");
		const char* h = svgElement->Attribute("height");
		if (!w || !h)
			return m;

		float wUnits = 0.0f, hUnits = 0.0f;
		const float wInches = lengthInInches(w, wUnits);
		const float hInches = lengthInInches(h, hUnits);

		if (wInches > 0.0f && hInches > 0.0f)
		{
			// Physical units: land it on Rack's grid, and tell the caller how
			// much to shrink SvgParser's 96-dpi rendering to match.
			m.size = { wInches * rackDpi, hInches * rackDpi };
			m.drawScale = rackDpi / cssDpi;
		}
		else if (wUnits > 0.0f && hUnits > 0.0f)
		{
			m.size = { wUnits, hUnits };
			m.drawScale = 1.0f;
		}

		return m;
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
