// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2007-2026 Jeff McClintock.
//
// The adaptor's own plugin factory, replacing GMPI's.
//
// WHY REPLACE IT. A ported module's XML is GENERATED, not written out, and the
// generator has to see the whole plugin. Registration happens in a static
// initializer driven by the module's own createModel() line, which sits at the
// bottom of upstream's .cpp and therefore runs BEFORE anything the port
// declares after including it. RACK_DISPLAY_STATE is exactly that. Generate
// the XML at registration time and it can never observe one.
//
// The fix is to build the XML when the host asks for it. That is a niche need
// - GMPI's own plugins write their XML as a literal and have no such problem -
// so this lives here rather than in the SDK. GMPI already anticipates it:
// defining GMPI_DISABLE_FACTORY empties Core/Common.cpp, leaving this file to
// supply the factory, the two registration entry points and MP_GetFactory.
// The consuming CMake gets that define from the SynthEditRackAdaptor target,
// and Common.cpp is compiled into the plugin, so it picks it up.
//
// This is the same shape as the VST3 wrapper's MyVstPluginFactory: a factory
// belonging to the thing with the unusual requirement, not a special case
// pushed down into the SDK.
//
// Everything else behaves exactly as GMPI's factory does, including the
// duplicate-registration assert and enumeration order.

#pragma once

#include <string>

#include "Common.h"
#include "GmpiApiCommon.h"

namespace rack_adaptor
{

// Builds a plugin's XML on demand. Capture-less, like gmpi::CreatePluginPtr,
// so a per-module lambda has to reach its module through a template parameter
// - see autoRegisterModel.
typedef std::string (*CreateXmlPtr)();

// Register a plugin whose XML is built when the host first asks for it.
//
// The id cannot be deferred with it: the factory keys on the id at
// registration time, so it is passed separately rather than parsed back out of
// the XML the way gmpi::RegisterPluginWithXml does.
//
// TWO IMPLEMENTATIONS, one per build shape - a consumer compiles exactly one:
//
//   RackFactory.cpp        the module is its own binary (.gmpi). Registers
//                          into the adaptor's factory; the XML is built when
//                          the host's scan asks getPluginInformation().
//   RackFactoryStatic.cpp  the modules are linked INTO a SynthEdit-based host
//                          (TIDE Rack). Queues the registration; the host
//                          calls registerDeferredModules() at startup.
gmpi::ReturnCode registerPluginLazyXml(
	gmpi::api::PluginSubtype subType,
	const char* uniqueId,
	CreateXmlPtr createXml,
	gmpi::CreatePluginPtr create);

// STATIC HOSTS ONLY (RackFactoryStatic.cpp): build each queued module's XML
// and register it through gmpi::RegisterPluginWithXml - which a static host
// implements itself (SynthEditLib routes it into ModuleFactory()). Call once
// at startup, AFTER static initialization, which is what makes the deferred
// XML generation safe. Idempotent; returns how many modules registered.
int registerDeferredModules();

} // namespace rack_adaptor
