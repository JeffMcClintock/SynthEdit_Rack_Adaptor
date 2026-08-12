// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2007-2026 Jeff McClintock.
//
// See RackFactory.h for why this exists at all.
//
// Compiled into every plugin that links SynthEditRackAdaptor, which also
// defines GMPI_DISABLE_FACTORY so GMPI's own factory is compiled out of
// Core/Common.cpp. The two would otherwise collide on MP_GetFactory.

#include <cassert>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "RackFactory.h"
#include "RefCountMacros.h"

using namespace gmpi;
using namespace gmpi::api;

namespace
{

class RackFactory final : public IPluginFactory
{
public:
	ReturnCode createInstance(const char* uniqueId, PluginSubtype subType, void** returnInterface) override;
	ReturnCode getPluginInformation(int32_t index, IString* returnXml) override;

	ReturnCode registerPlugin(PluginSubtype subType, const char* uniqueId, CreatePluginPtr create);
	ReturnCode registerPluginWithXml(PluginSubtype subType, const char* xml, CreatePluginPtr create);
	ReturnCode registerPluginLazyXml(PluginSubtype subType, const char* uniqueId,
	                                 rack_adaptor::CreateXmlPtr createXml, CreatePluginPtr create);

	GMPI_QUERYINTERFACE_METHOD(IPluginFactory);
	GMPI_REFCOUNT_NO_DELETE

private:
	std::map<std::pair<PluginSubtype, std::string>, CreatePluginPtr> pluginMap;

	// One entry per plugin that has XML, in registration order, because that
	// is the order the host enumerates them in. `xml` is either supplied up
	// front or built by `createXml` the first time it is asked for.
	struct PluginXml
	{
		std::string                 xml;
		rack_adaptor::CreateXmlPtr  createXml{};
		bool                        built{};
	};
	std::vector<PluginXml> xmls;
};

RackFactory& factory()
{
	static RackFactory theFactory;
	return theFactory;
}

// Pull the plugin id out of an XML literal, the way GMPI's factory does, so a
// module registered the eager way still keys correctly.
bool idFromXml(const std::string& xml, std::string& returnId)
{
	std::size_t p{};
	for (auto s : { "<Plugin", " id", "\"" })
	{
		const auto found = xml.find(s, p);
		if (found == std::string::npos)
			return false;

		p = found + std::strlen(s);
	}

	const auto p2 = xml.find("\"", p);
	if (p2 == std::string::npos)
		return false;

	returnId = xml.substr(p, p2 - p);
	return true;
}

ReturnCode RackFactory::registerPlugin(PluginSubtype subType, const char* uniqueId, CreatePluginPtr create)
{
	if (!create || !uniqueId)
		return ReturnCode::Fail;

	// A plugin registered twice can only mean a duplicate include.
	assert(pluginMap.find({ subType, uniqueId }) == pluginMap.end());

	pluginMap[{ subType, uniqueId }] = create;
	return ReturnCode::Ok;
}

ReturnCode RackFactory::registerPluginWithXml(PluginSubtype subType, const char* xml, CreatePluginPtr create)
{
	if (!create || !xml)
		return ReturnCode::Fail;

	std::string xmlstr{ xml };

	std::string uniqueId;
	if (!idFromXml(xmlstr, uniqueId))
		return ReturnCode::Fail;

	assert(pluginMap.find({ subType, uniqueId }) == pluginMap.end());

	pluginMap[{ subType, uniqueId }] = create;
	xmls.push_back({ std::move(xmlstr), nullptr, true });

	return ReturnCode::Ok;
}

ReturnCode RackFactory::registerPluginLazyXml(PluginSubtype subType, const char* uniqueId,
                                              rack_adaptor::CreateXmlPtr createXml, CreatePluginPtr create)
{
	if (!create || !createXml || !uniqueId)
		return ReturnCode::Fail;

	assert(pluginMap.find({ subType, uniqueId }) == pluginMap.end());

	pluginMap[{ subType, uniqueId }] = create;
	xmls.push_back({ {}, createXml, false });

	return ReturnCode::Ok;
}

ReturnCode RackFactory::createInstance(const char* uniqueId, PluginSubtype subType, void** returnInterface)
{
	if (!returnInterface || !uniqueId)
		return ReturnCode::Fail;

	*returnInterface = nullptr;

	const auto it = pluginMap.find({ subType, uniqueId });
	if (it == pluginMap.end() || !it->second)
		return ReturnCode::NoSupport;

	try
	{
		auto* m = it->second();
		*returnInterface = m;

#ifdef _DEBUG
		{
			m->addRef();
			const auto refcount = m->release();
			assert(refcount == 1);
		}
#endif
	}
	catch (...)
	{
		// new throws std::bad_alloc; a constructor throws when the host does
		// not supply an interface the plugin requires.
		return ReturnCode::Fail;
	}

	return ReturnCode::Ok;
}

ReturnCode RackFactory::getPluginInformation(int32_t index, IString* returnXml)
{
	if (!returnXml)
		return ReturnCode::Fail;

	if (index < 0 || static_cast<std::size_t>(index) >= xmls.size())
	{
		returnXml->setData(nullptr, 0);
		return ReturnCode::Fail;
	}

	auto& entry = xmls[static_cast<std::size_t>(index)];

	// THIS is the reason the adaptor has a factory of its own. A deferred
	// registration builds its XML here, once, and by now the DLL's static
	// initialization has long finished - so the generator can rely on
	// everything the plugin registered, including declarations that follow a
	// module's own createModel() line.
	if (!entry.built)
	{
		entry.xml = entry.createXml ? entry.createXml() : std::string{};
		entry.built = true;
	}

	returnXml->setData(entry.xml.data(), static_cast<int32_t>(entry.xml.size()));
	return ReturnCode::Ok;
}

} // anonymous namespace

// GMPI's own definitions of these are compiled out by GMPI_DISABLE_FACTORY.
// The signatures have to match Core/Common.h exactly: gmpi_ui's helpers and
// the adaptor's own editor registration both call them.
namespace gmpi
{

ReturnCode RegisterPlugin(api::PluginSubtype subType, const char* uniqueId, CreatePluginPtr create)
{
	return factory().registerPlugin(subType, uniqueId, create);
}

ReturnCode RegisterPluginWithXml(api::PluginSubtype subType, const char* xml, CreatePluginPtr create)
{
	return factory().registerPluginWithXml(subType, xml, create);
}

} // namespace gmpi

namespace rack_adaptor
{

ReturnCode registerPluginLazyXml(api::PluginSubtype subType, const char* uniqueId,
                                 CreateXmlPtr createXml, CreatePluginPtr create)
{
	return factory().registerPluginLazyXml(subType, uniqueId, createXml, create);
}

} // namespace rack_adaptor

// The DLL's entry point. One per binary, which is why GMPI's has to be
// compiled out rather than merely unused.
extern "C"

#ifdef _WIN32
	__declspec(dllexport)
#elif defined(__GNUC__)
	__attribute__((visibility("default")))
#endif

ReturnCode MP_GetFactory(void** returnInterface)
{
	if (!returnInterface)
		return ReturnCode::Fail;

	// queryInterface rather than a cast, to keep refcounting in step.
	return factory().queryInterface(&IUnknown::guid, returnInterface);
}
