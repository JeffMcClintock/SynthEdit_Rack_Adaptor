// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2007-2026 Jeff McClintock.
//
// The STATIC-HOST half of the registration story. Compiled INSTEAD of
// RackFactory.cpp, never alongside it.
//
// RackFactory.cpp is written for a module that is its own binary: it supplies
// gmpi::RegisterPlugin, gmpi::RegisterPluginWithXml and MP_GetFactory. A host
// that links the ported modules STATICALLY (TIDE Rack) already defines all
// three - SynthEditLib's UgDatabase.cpp routes the first two straight into its
// ModuleFactory(), and the format wrapper owns the entry point - so compiling
// RackFactory.cpp into such a host is a wall of duplicate symbols.
//
// What the static host still needs from the adaptor is the ONE thing the
// shared factory existed for: the deferred XML. autoRegisterModel() runs from
// each module's own createModel() line during static init, which is BEFORE
// anything the port declares after including upstream's .cpp
// (RACK_DISPLAY_STATE is the canonical example) - so the XML cannot be built
// at registration time there any more than it could in a .gmpi. The dynamic
// build defers to the factory's getPluginInformation(); this file defers to an
// explicit call the host makes at startup instead:
//
//     rack_adaptor::registerDeferredModules();   // e.g. TideApp::InitInstance
//
// By then every static initializer in the binary has run, so the generator
// sees the whole module - the same guarantee the lazy factory gives, obtained
// from the host's timeline instead of the host's scan.
//
// The GUI half needs no deferral in either build: RackAutoRegister.h calls
// gmpi::RegisterPlugin(Editor, ...) directly at static init, and the static
// host's implementation of that (FindOrCreateModuleInfo3) tolerates the GUI
// arriving before the DSP XML.

#include <string>
#include <vector>

#include "RackFactory.h"

namespace
{

struct DeferredRegistration
{
	gmpi::api::PluginSubtype   subType;
	std::string                uniqueId;
	rack_adaptor::CreateXmlPtr createXml;
	gmpi::CreatePluginPtr      create;
};

// Function-local static, never namespace-scope: this is written during static
// init and a namespace-scope vector's own constructor is unordered against
// those writes. See DEVNOTES.md in VCV_Fundamental_gmpi, "Static init: use
// function-local statics, always".
std::vector<DeferredRegistration>& deferred()
{
	static std::vector<DeferredRegistration> list;
	return list;
}

} // anonymous namespace

namespace rack_adaptor
{

gmpi::ReturnCode registerPluginLazyXml(gmpi::api::PluginSubtype subType, const char* uniqueId,
                                       CreateXmlPtr createXml, gmpi::CreatePluginPtr create)
{
	if (!create || !createXml || !uniqueId)
		return gmpi::ReturnCode::Fail;

	deferred().push_back({ subType, uniqueId, createXml, create });
	return gmpi::ReturnCode::Ok;
}

int registerDeferredModules()
{
	// ONCE PER PROCESS, guarded here rather than in the host, because the
	// registrations land in a process-global database and a plug-in host
	// creates several instances in one process (TIDE learned this at M4:
	// an AUv3 extension process is shared across instantiations).
	static bool done = false;
	if (done)
		return 0;
	done = true;

	int count = 0;
	for (const auto& d : deferred())
	{
		const auto xml = d.createXml ? d.createXml() : std::string{};
		if (xml.empty())
			continue; // the model never registered; nothing to describe

		if (gmpi::RegisterPluginWithXml(d.subType, xml.c_str(), d.create) == gmpi::ReturnCode::Ok)
			++count;
	}

	return count;
}

} // namespace rack_adaptor
