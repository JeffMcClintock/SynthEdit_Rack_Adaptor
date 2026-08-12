// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2007-2026 Jeff McClintock.
//
// Automatic registration: compiling a Rack module's .cpp is all it takes.
//
// rack::createModel() calls autoRegisterModel(), declared in plugin.hpp and
// defined here, so a module's own registration line —
//
//     Model* modelFade = createModel<Fade, FadeWidget>("Fade");
//
// — is what puts a GMPI plugin in the factory. There is no per-module
// registration call to write, and none to forget.
//
// Include RackModule.h rather than this header directly; it pulls in the
// pieces in the order the templates need.
//
// DEFAULTS come from macros so a plugin sets them once (in CMake) instead of
// once per module:
//
//     RACK_MODULE_ID_PREFIX   "VCV: "          plugin id is prefix + slug
//     RACK_MODULE_CATEGORY    "VCV"
//     RACK_MODULE_VENDOR      "VCV (ported)"
//
// OPT OUT per module by defining RACK_NO_AUTO_REGISTER before including its
// .cpp, then calling rack_adaptor::registerModule() yourself with whatever options you
// need. Opt out of the editor for the whole plugin with RACK_ADAPTOR_NO_GUI.

#pragma once

#include <string>

#include "RackAdaptor.h"

#ifndef RACK_ADAPTOR_NO_GUI
	#include "RackEditor.h"
#endif

#ifndef RACK_MODULE_ID_PREFIX
	#define RACK_MODULE_ID_PREFIX "VCV: "
#endif

#ifndef RACK_MODULE_CATEGORY
	#define RACK_MODULE_CATEGORY "VCV"
#endif

#ifndef RACK_MODULE_VENDOR
	#define RACK_MODULE_VENDOR "VCV (ported)"
#endif

namespace rack_adaptor
{

// Per-instantiation storage, so the capture-less function pointers GMPI wants
// can still find their module. One pair per createModel<M, W> instantiation,
// which is one per ported module.
//
// FUNCTION-LOCAL statics, not `inline static` members, and the difference is
// not cosmetic. These are written during static init (from the module's own
// createModel line) and read later at runtime. An inline static std::string
// has its own dynamic initialization, unordered against that call — so the
// assignment lands first and the string's constructor then wipes it, leaving
// an empty slug and an editor that cannot find its model. Constructing on
// first use removes the ordering question, exactly as ModelRegistry does.
template<class TModule, class TModuleWidget>
std::string& moduleSlug()
{
	static std::string slug;
	return slug;
}

template<class TModule, class TModuleWidget>
std::string& modulePluginId()
{
	static std::string id;
	return id;
}

template<class TModule, class TModuleWidget>
void autoRegisterModel(rack::Model& model)
{
	auto& slug = moduleSlug<TModule, TModuleWidget>();
	auto& id   = modulePluginId<TModule, TModuleWidget>();

	// A module registered twice would assert inside GMPI's factory; a second
	// createModel with the same types can only mean a duplicate include.
	if (!slug.empty())
		return;

	slug = model.slug;
	id   = std::string(RACK_MODULE_ID_PREFIX) + model.slug;

	// Plain assignment rather than designated initializers: the values are
	// macros, and a macro that arrives without its quotes (a real risk when
	// CMake passes a string containing a space) produces a baffling syntax
	// error inside a braced initializer. This way it fails on the assignment,
	// naming the macro.
	RegistrationOptions opt;
	opt.id       = id.c_str();
	opt.name     = slug.c_str();
	opt.category = RACK_MODULE_CATEGORY;
	opt.vendor   = RACK_MODULE_VENDOR;

	const auto xml = generatePluginXml(model, opt);

	gmpi::RegisterPluginWithXml(
		gmpi::api::PluginSubtype::Audio,
		xml.c_str(),                       // the factory copies it
		[]() -> gmpi::api::IUnknown*
		{
			return createProcessor(moduleSlug<TModule, TModuleWidget>().c_str());
		});

#ifndef RACK_ADAPTOR_NO_GUI
	// The editor finds its panel art through the module's own createPanel()
	// path, so nothing needs naming here either.
	gmpi::RegisterPlugin(
		gmpi::api::PluginSubtype::Editor,
		id.c_str(),
		[]() -> gmpi::api::IUnknown*
		{
			return createEditor(moduleSlug<TModule, TModuleWidget>().c_str());
		});
#endif
}

} // namespace rack_adaptor
