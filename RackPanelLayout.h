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
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "rack.hpp"

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

// One entry from the module's appendContextMenu(), kept in declaration order so
// the menu reads the way the module wrote it — separators included.
//
// For an option, `target` points into the module instance that declared it, so
// whoever built the layout can write the chosen index straight through, and
// `paramIndex` is its position among the options only (separators do not get a
// parameter, so they do not consume an index).
enum class MenuOptionKind
{
	Separator,   // display only, no parameter
	IndexPtr,    // submenu of labels selecting an int   (createIndexPtrSubmenuItem)
	BoolPtr,     // single entry that ticks on and off   (createBoolPtrMenuItem)
};

struct MenuOption
{
	MenuOptionKind kind{ MenuOptionKind::Separator };

	std::string name;
	std::string rightText;
	std::vector<std::string> labels;   // IndexPtr only

	int*  target{};       // IndexPtr — into the module that declared it
	bool* boolTarget{};   // BoolPtr  — likewise
	int   defaultValue{};

	// The lambda form of the same two. A module that has to DO something when
	// the value changes (Merge resizes its channel count) states its menu with
	// accessors rather than a raw pointer, so both shapes have to be carried.
	// Whichever is set is the one to write through; the setter wins when both
	// are, because it is the one that runs the module's own side effects.
	std::function<void(int)>  setIndex;
	std::function<void(bool)> setBool;

	void write(int value) const
	{
		if (kind == MenuOptionKind::BoolPtr)
		{
			if (setBool)          setBool(value != 0);
			else if (boolTarget) *boolTarget = (value != 0);
		}
		else
		{
			if (setIndex)     setIndex(value);
			else if (target) *target = value;
		}
	}

	// Position among the options, skipping separators. Offsets the GMPI
	// parameter id, so a separator never consumes one.
	int paramIndex{ -1 };

	// Position within this option's own pin container. Kinds use different pin
	// types (int vs bool), so they are counted separately even though the pins
	// are CONSTRUCTED in declaration order — see RackProcessor.
	int slot{ -1 };
};

struct PanelLayout
{
	std::vector<ControlLayout> params;
	std::vector<ControlLayout> inputs;
	std::vector<ControlLayout> outputs;
	std::vector<MenuOption>    menu;

	// Whatever the module passed to createPanel(), e.g. "res/Fade.svg".
	// Resolve against rack::PanelRegistry to get the bytes.
	std::string panelPath;

	// Table sizes as the module declared them in config(), which is not the
	// same as the vector sizes above — a module may leave a port unplaced.
	std::size_t numParams{}, numInputs{}, numOutputs{};
};

// Record what appendContextMenu() declared, in order. Separators are kept so
// the menu reads as the module wrote it, but they carry no parameter — only
// options are numbered, which is what paramIndex tracks.
//
// bindTargets is false when the module is a throwaway probe: a target pointing
// into a module that is about to be destroyed is worse than no target at all.
inline void collectMenu(rack::Menu& menu, PanelLayout& layout, bool bindTargets)
{
	int paramIndex = 0;
	int intSlot = 0;
	int boolSlot = 0;

	for (auto* item : menu.items)
	{
		if (!item)
			continue;

		MenuOption option;
		option.name = item->name;
		option.rightText = item->rightText;

		if (item->isSeparator)
		{
			option.kind = MenuOptionKind::Separator;
		}
		else if (item->isIndexPtr())
		{
			option.kind = MenuOptionKind::IndexPtr;
			option.labels = item->labels;
			option.target   = bindTargets ? item->target     : nullptr;
			option.setIndex = bindTargets ? item->setIndexFn : nullptr;
			option.defaultValue = item->readIndex();
			option.paramIndex = paramIndex++;
			option.slot = intSlot++;
		}
		else if (item->isBoolPtr())
		{
			option.kind = MenuOptionKind::BoolPtr;
			option.boolTarget = bindTargets ? item->boolTarget : nullptr;
			option.setBool    = bindTargets ? item->setBoolFn  : nullptr;
			option.defaultValue = item->readBool() ? 1 : 0;
			option.paramIndex = paramIndex++;
			option.slot = boolSlot++;
		}
		else
		{
			continue;   // a menu item shape this adaptor does not model yet
		}

		layout.menu.push_back(std::move(option));
	}
}

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

		const auto& q = *probe->paramQuantities[w->paramId];

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

	// Context menu, e.g. Fade's "Pan law". Names, labels and current value only
	// — `target` is deliberately left null, because it would point into
	// `probe`, which is destroyed on the way out. A caller that needs to WRITE
	// the chosen index uses the two-argument overload below and binds the menu
	// to its own live module.
	{
		rack::Menu menu;
		widget->appendContextMenu(&menu);

		collectMenu(menu, layout, /*bindTargets*/ false);
	}

	return layout;
}

// As above, but binding the menu targets to a module the CALLER owns.
//
// readPanelLayout() builds and destroys its own probe, which would leave every
// MenuOption::target dangling. The editor and the processor each hold their own
// live module, and each needs the menu bound to that one — writing a chosen
// index has to land in the instance that is actually running.
inline PanelLayout readPanelLayout(rack::Model& model, rack::Module* liveModule)
{
	PanelLayout layout = readPanelLayout(model);

	layout.menu.clear();

	if (!liveModule)
		return layout;

	std::unique_ptr<rack::ModuleWidget> widget(model.createModuleWidget(liveModule));
	if (!widget)
		return layout;

	rack::Menu menu;
	widget->appendContextMenu(&menu);

	collectMenu(menu, layout, /*bindTargets*/ true);

	return layout;
}

} // namespace rack_adaptor
