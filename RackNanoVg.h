// SPDX-License-Identifier: ISC OR GPL-3.0-or-later
// Copyright 2007-2026 Jeff McClintock.
//
// nanovg, implemented over gmpi_ui.
//
// Seven of Fundamental's modules draw part of their own panel — Scope's trace,
// VCA-1's VU meter, ADSR's envelope curve, Octave's button grid, Quantizer's
// note grid, Sum's and Viz's channel readouts. All of that is nanovg calls in
// the module's own source, which is not ours to change. This is the other end
// of them.
//
// Between the seven there are 27 distinct nvg* functions, and gmpi_ui has a
// direct equivalent for every one, so this is a translation rather than a
// reimplementation.
//
// TWO THINGS ARE NOT TRANSLATED, both stated rather than papered over:
//
//   Fonts. Rack asks for res/fonts/ShareTechMono-Regular.ttf, which is Rack's
//   file and is not shipped here. Text renders in a monospace fallback, so
//   glyph widths differ slightly from Rack's.
//
//   Gradient paints. nvgFillPaint is a no-op — nothing in this set fills with
//   a gradient, and a wrong gradient is worse than an obvious flat fill.
//
// PATHS ARE BUFFERED, not streamed. nanovg builds a path and then paints it
// with fill() or stroke(), possibly both; gmpi_ui wants the geometry closed
// before it can paint, and wants to know at the START of each figure whether
// it is destined to be filled or stroked. Buffering the points and building
// the geometry at paint time is what reconciles those, and it means rect(),
// circle() and roundedRect() can be mixed into a path with moveTo/lineTo the
// way nanovg allows.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "helpers/GmpiPluginEditor.h"
#include "rack.hpp"

namespace rack_adaptor
{

// DECODING Rack's colours is not the same as adopting Rack's blending, and the
// difference is the whole point.
//
// A Rack NVGcolor is sRGB-encoded — nvgRGB(0x19, 0x19, 0x19) means the byte
// 0x19 as it would appear in an image file. gmpi_ui's Color components are
// LINEAR. Passing the components straight through therefore does not preserve
// the colour, it reinterprets it: Scope's near-black display panel came out
// mid-grey, (88,88,88) against Rack's (25,25,25), because 0.098 treated as
// linear encodes to 0.34 on the way out.
//
// So the components are decoded here, once, on the way in. Blending stays
// linear — which is the property worth keeping and the reason not to imitate
// Rack's pipeline — while the colours are the ones the module asked for.
//
// The error is worst exactly where it is most visible: dark colours. A bright
// saturated LED shifts a few percent; a dark panel shifts by a factor of three.
inline float srgbToLinear(float c)
{
	// gmpi_ui's own conversion, which is a 256-entry table. Rack's colours all
	// originate as bytes — nvgRGB(0x19, 0x19, 0x19) — so the round-trip through
	// one is exact for every colour that actually turns up, and the few stated
	// as floats land within half a code value.
	const int byte = (int)std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f);
	return gmpi::drawing::SRGBPixelToLinear((uint8_t)byte);
}

// Alpha is a coverage fraction, not a colour, and is never gamma-encoded.
inline gmpi::drawing::Color toColor(rack::NVGcolor c)
{
	return { srgbToLinear(c.r), srgbToLinear(c.g), srgbToLinear(c.b), c.a };
}

class NanoVgGraphics final : public rack::NanoVgBackend
{
public:
	explicit NanoVgGraphics(gmpi::drawing::Graphics& graphics) : g(graphics) {}

	// What a module's draw() is handed. `this` is reachable from it through
	// the backend pointer, which is the whole mechanism.
	rack::Widget::DrawArgs args()
	{
		rack::Widget::DrawArgs a;
		a.vg = &context;
		return a;
	}

	// ----------------------------------------------------------------- paths

	void beginPath() override
	{
		subPaths.clear();
	}

	void moveTo(float x, float y) override
	{
		subPaths.push_back({});
		subPaths.back().points.push_back(transformed(x, y));
	}

	void lineTo(float x, float y) override
	{
		if (subPaths.empty())
			subPaths.push_back({});

		subPaths.back().points.push_back(transformed(x, y));
	}

	// Flattened, because nothing in this set uses curves and a subdivided
	// cubic is indistinguishable at panel scale.
	void bezierTo(float c1x, float c1y, float c2x, float c2y, float x, float y) override
	{
		if (subPaths.empty() || subPaths.back().points.empty())
		{
			moveTo(x, y);
			return;
		}

		const auto p0 = subPaths.back().points.back();
		const auto p1 = transformed(c1x, c1y);
		const auto p2 = transformed(c2x, c2y);
		const auto p3 = transformed(x, y);

		constexpr int steps = 16;
		for (int i = 1; i <= steps; ++i)
		{
			const float t = (float)i / steps;
			const float u = 1.0f - t;

			subPaths.back().points.push_back({
				u*u*u*p0.x + 3*u*u*t*p1.x + 3*u*t*t*p2.x + t*t*t*p3.x,
				u*u*u*p0.y + 3*u*u*t*p1.y + 3*u*t*t*p2.y + t*t*t*p3.y });
		}
	}

	void closePath() override
	{
		if (!subPaths.empty())
			subPaths.back().closed = true;
	}

	void rect(float x, float y, float w, float h) override
	{
		SubPath sp;
		sp.closed = true;
		sp.points = { transformed(x, y), transformed(x + w, y),
		              transformed(x + w, y + h), transformed(x, y + h) };
		subPaths.push_back(std::move(sp));
	}

	void roundedRect(float x, float y, float w, float h, float r) override
	{
		const float radius = (std::min)(r, 0.5f * (std::min)(w, h));

		SubPath sp;
		sp.closed = true;

		// Corner arcs, clockwise from the top-left, 8 segments each — enough
		// that a 2px corner radius reads as a curve rather than a chamfer.
		const struct { float cx, cy, from; } corners[]
		{
			{ x + w - radius, y + radius,     -90.0f },   // top-right
			{ x + w - radius, y + h - radius,   0.0f },   // bottom-right
			{ x + radius,     y + h - radius,  90.0f },   // bottom-left
			{ x + radius,     y + radius,     180.0f },   // top-left
		};

		for (const auto& c : corners)
		{
			constexpr int segments = 8;
			for (int i = 0; i <= segments; ++i)
			{
				const float deg = c.from + 90.0f * (float)i / segments;
				const float rad = deg * 3.14159265f / 180.0f;
				sp.points.push_back(transformed(c.cx + std::cos(rad) * radius,
				                                c.cy + std::sin(rad) * radius));
			}
		}

		subPaths.push_back(std::move(sp));
	}

	void circle(float cx, float cy, float r) override { ellipse(cx, cy, r, r); }

	void ellipse(float cx, float cy, float rx, float ry) override
	{
		SubPath sp;
		sp.closed = true;

		constexpr int segments = 48;
		for (int i = 0; i < segments; ++i)
		{
			const float rad = 2.0f * 3.14159265f * (float)i / segments;
			sp.points.push_back(transformed(cx + std::cos(rad) * rx, cy + std::sin(rad) * ry));
		}

		subPaths.push_back(std::move(sp));
	}

	void fill() override
	{
		auto geometry = buildGeometry(gmpi::drawing::FigureBegin::Filled,
		                              gmpi::drawing::FigureEnd::Closed);
		if (!geometry)
			return;

		auto brush = g.createSolidColorBrush(withAlpha(state.fill));
		g.fillGeometry(*geometry, brush);
	}

	void stroke() override
	{
		// FigureEnd::Open for an unclosed subpath, so a polyline is not
		// silently joined end-to-end — Scope's background lines are open.
		auto geometry = buildGeometry(gmpi::drawing::FigureBegin::Hollow,
		                              gmpi::drawing::FigureEnd::Open);
		if (!geometry)
			return;

		auto brush = g.createSolidColorBrush(withAlpha(state.stroke));
		g.drawGeometry(*geometry, brush, state.strokeWidth);
	}

	// ----------------------------------------------------------------- state

	void fillColor(rack::NVGcolor c) override   { state.fill = c; }
	void strokeColor(rack::NVGcolor c) override { state.stroke = c; }
	void strokeWidth(float w) override          { state.strokeWidth = w; }
	void globalAlpha(float a) override          { state.alpha = a; }
	void fontSize(float s) override             { state.fontSize = s; }
	void textAlign(int a) override              { state.textAlign = a; }
	void textLetterSpacing(float s) override    { state.letterSpacing = s; }

	void save() override { stack.push_back(state); }

	void restore() override
	{
		if (stack.empty())
			return;

		// Clips are pushed on the graphics context, so unwinding the state has
		// to unwind them too and in the right order.
		while (state.clipDepth > stack.back().clipDepth)
		{
			g.popAxisAlignedClip();
			--state.clipDepth;
		}

		state = stack.back();
		stack.pop_back();
	}

	void translate(float x, float y) override { state.offsetX += x * state.scaleX; state.offsetY += y * state.scaleY; }
	void scale(float x, float y) override     { state.scaleX *= x; state.scaleY *= y; }

	// MOCK: no module in this set rotates, and honouring it would mean
	// carrying a full matrix rather than the offset/scale pair above.
	void rotate(float) override {}

	void scissor(float x, float y, float w, float h) override
	{
		const auto tl = transformed(x, y);
		const auto br = transformed(x + w, y + h);

		g.pushAxisAlignedClip({ tl.x, tl.y, br.x, br.y });
		++state.clipDepth;
	}

	void resetScissor() override
	{
		while (state.clipDepth > 0)
		{
			g.popAxisAlignedClip();
			--state.clipDepth;
		}
	}

	// ------------------------------------------------------------------ text

	// nanovg positions text by a corner or the baseline; gmpi_ui lays it out in
	// a rectangle. The rectangle is derived from the measured extent so the two
	// agree, with the vertical treated as a baseline unless told otherwise —
	// that is nanovg's default and what Fundamental's displays assume.
	float text(float x, float y, const char* str, const char* end) override
	{
		if (!str)
			return 0.0f;

		const std::string_view s = end ? std::string_view(str, (std::size_t)(end - str))
		                               : std::string_view(str);
		if (s.empty())
			return 0.0f;

		const float size = state.fontSize * state.scaleY;
		if (size <= 0.0f)
			return 0.0f;

		// A monospace face, because every display using text here is a numeric
		// readout that Rack renders in ShareTechMono.
		const std::string_view families[]{ "Consolas", "Courier New", "Arial" };
		auto format = g.getFactory().createTextFormat(size, families);

		const auto extent = format.getTextExtentU(s);
		const auto metrics = format.getFontMetrics();

		const auto origin = transformed(x, y);

		float left = origin.x;
		if (state.textAlign & rack::NVG_ALIGN_CENTER) left -= 0.5f * extent.width;
		else if (state.textAlign & rack::NVG_ALIGN_RIGHT) left -= extent.width;

		float top = origin.y;
		if (state.textAlign & rack::NVG_ALIGN_MIDDLE)      top -= 0.5f * extent.height;
		else if (state.textAlign & rack::NVG_ALIGN_BOTTOM) top -= extent.height;
		else if (state.textAlign & rack::NVG_ALIGN_TOP)    top += 0.0f;
		else                                               top -= metrics.ascent;   // baseline, nanovg's default

		auto brush = g.createSolidColorBrush(withAlpha(state.fill));
		g.drawTextU(s, format, { left, top, left + extent.width + 1.0f, top + extent.height + 1.0f }, brush);

		return extent.width;
	}

private:
	struct SubPath
	{
		std::vector<gmpi::drawing::Point> points;
		bool closed{};
	};

	// nanovg's transform, reduced to what the modules use: translation and
	// scale. Applied as points go in, so the buffered path is already in the
	// editor's coordinates and nothing has to be re-transformed at paint time.
	gmpi::drawing::Point transformed(float x, float y) const
	{
		return { state.offsetX + x * state.scaleX, state.offsetY + y * state.scaleY };
	}

	gmpi::drawing::Color withAlpha(rack::NVGcolor c) const
	{
		auto out = toColor(c);
		out.a *= state.alpha;
		return out;
	}

	std::optional<gmpi::drawing::PathGeometry> buildGeometry(gmpi::drawing::FigureBegin begin,
	                                                         gmpi::drawing::FigureEnd end)
	{
		bool any = false;
		for (const auto& sp : subPaths)
		{
			if (sp.points.size() >= 2)
			{
				any = true;
				break;
			}
		}
		if (!any)
			return std::nullopt;

		auto geometry = g.getFactory().createPathGeometry();
		auto sink = geometry.open();

		for (const auto& sp : subPaths)
		{
			if (sp.points.size() < 2)
				continue;

			sink.beginFigure(sp.points.front(), begin);
			sink.addLines({ sp.points.data() + 1, sp.points.size() - 1 });
			sink.endFigure(sp.closed ? gmpi::drawing::FigureEnd::Closed : end);
		}

		sink.close();
		return geometry;
	}

	struct State
	{
		rack::NVGcolor fill{ 1.f, 1.f, 1.f, 1.f };
		rack::NVGcolor stroke{ 1.f, 1.f, 1.f, 1.f };
		float strokeWidth{ 1.0f };
		float alpha{ 1.0f };

		float offsetX{}, offsetY{};
		float scaleX{ 1.0f }, scaleY{ 1.0f };

		int   clipDepth{};

		float fontSize{ 12.0f };
		int   textAlign{ rack::NVG_ALIGN_LEFT };
		float letterSpacing{};
	};

	gmpi::drawing::Graphics& g;
	rack::NVGcontext context{ this };

	State state;
	std::vector<State> stack;
	std::vector<SubPath> subPaths;
};

} // namespace rack_adaptor
