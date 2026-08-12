// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2007-2026 Jeff McClintock.
//
// Read a VCV module's panel layout out of its own ModuleWidget.
//
// A Rack module states where every control goes in its widget's constructor:
//
//     addParam(createParamCentered<RoundBlackKnob>(
//                  mm2px(Vec(7.62, 24.723)), module, Fade::CROSSFADE_PARAM));
//     addInput(createInputCentered<ThemedPJ301MPort>(
//                  mm2px(Vec(7.62, 67.53)), module, Fade::IN1_INPUT));
//
// — the position, the size (from the component type) and the param/port id,
// all together, in the module's own source. This header turns that into plain
// data, which both the XML generator (patch points) and the generic editor
// (knob hit-testing and drawing) consume.
//
// Better source than the panel SVG: Fundamental's SVGs carry a display:none
// "components" layer marking the same positions, but that is a convention
// those files happen to follow, whereas this is the code Rack itself lays the
// panel out from. It cannot drift from what the module really has.
//
// Coordinates are Rack panel pixels — mm2px, 1px = 1/75in — which is the same
// space the panel SVG uses.

#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "plugin.hpp"

namespace rack_adaptor
{

// One control's placement. `id` is a paramId for params, a portId for ports.
struct ControlLayout
{
	int   id{};
	float x{}, y{};            // centre
	float width{}, height{};

	// Params only, from configParam().
	float minValue{}, maxValue{ 1.0f }, defaultValue{};
	std::string name;

	float radius() const { return 0.5f * (std::min)(width, height); }

	// Where `value` sits in 0..1 across the declared range.
	float normalise(float value) const
	{
		const float span = maxValue - minValue;
		return span > 0.0f ? (value - minValue) / span : 0.0f;
	}

	float denormalise(float t) const
	{
		return minValue + t * (maxValue - minValue);
	}
};

struct PanelLayout
{
	std::vector<ControlLayout> params;
	std::vector<ControlLayout> inputs;
	std::vector<ControlLayout> outputs;

	// Whatever the module passed to createPanel(), e.g. "res/Fade.svg".
	// Resolve against rack::PanelRegistry to get the bytes.
	std::string panelPath;

	// Table sizes as the module declared them in config(), which is not the
	// same as the vector sizes above — a module may leave a port unplaced.
	std::size_t numParams{}, numInputs{}, numOutputs{};
};

// Construct the module and its widget purely to read the layout back, then
// throw both away. Child widgets leak; this runs once per registration (and
// once per editor), so a tree walk to free them is not worth the code.
inline PanelLayout readPanelLayout(rack::Model& model)
{
	PanelLayout layout;

	std::unique_ptr<rack::Module> probe(model.createModule());
	if (!probe)
		return layout;

	layout.numParams  = probe->params.size();
	layout.numInputs  = probe->inputs.size();
	layout.numOutputs = probe->outputs.size();

	std::unique_ptr<rack::ModuleWidget> widget(model.createModuleWidget(probe.get()));
	if (!widget)
		return layout;

	layout.panelPath = widget->panelPath;

	for (auto* w : widget->paramWidgets)
	{
		if (!w || w->paramId < 0 || (std::size_t)w->paramId >= layout.numParams)
			continue;

		const auto& q = probe->paramQuantities[w->paramId];

		layout.params.push_back({
			w->paramId,
			w->box.pos.x, w->box.pos.y,
			w->box.size.x, w->box.size.y,
			q.minValue, q.maxValue, q.defaultValue,
			q.name });
	}

	auto collectPorts = [](const std::vector<rack::PortWidget*>& src,
	                       std::size_t count,
	                       const std::vector<std::string>& names,
	                       std::vector<ControlLayout>& dest)
	{
		for (auto* w : src)
		{
			if (!w || w->portId < 0 || (std::size_t)w->portId >= count)
				continue;

			ControlLayout c;
			c.id = w->portId;
			c.x = w->box.pos.x;
			c.y = w->box.pos.y;
			c.width = w->box.size.x;
			c.height = w->box.size.y;
			c.name = names[w->portId];
			dest.push_back(c);
		}
	};

	collectPorts(widget->inputWidgets,  layout.numInputs,  probe->inputNames,  layout.inputs);
	collectPorts(widget->outputWidgets, layout.numOutputs, probe->outputNames, layout.outputs);

	return layout;
}

} // namespace rack_adaptor
