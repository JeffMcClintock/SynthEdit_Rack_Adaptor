# SPDX-License-Identifier: GPL-3.0-or-later
#
# SynthEdit Rack Adaptor.
#
# Consumers link the INTERFACE target:
#     target_link_libraries(MyModule PRIVATE SynthEditRackAdaptor)
#
# GMPI's Core headers (Processor.h) and, for the editor, gmpi_ui's helpers are
# expected on the consumer's include path already — gmpi_plugin() provides
# both.
#
# NOTE ON LICENSING: this library is GPL-3.0-or-later, and so is anything you
# build with it. See LICENSE and README.md.

cmake_minimum_required(VERSION 3.20)

project(SynthEdit_Rack_Adaptor LANGUAGES CXX)

add_library(SynthEditRackAdaptor INTERFACE)

# rack/ is on the path so a module's own `#include "plugin.hpp"` resolves to
# the mock without the module being edited.
# compat/ supplies the third-party headers Rack modules include directly and
# that this project does not otherwise depend on — <samplerate.h> (Delay) and
# <osdialog.h>/"dr_wav.h" (WTVCO, WTLFO). See the comment at the top of each.
target_include_directories(SynthEditRackAdaptor INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/rack
    ${CMAKE_CURRENT_SOURCE_DIR}/compat
)

target_compile_features(SynthEditRackAdaptor INTERFACE cxx_std_20)

# The adaptor brings its own plugin factory (RackFactory.cpp), because a ported
# module's XML is GENERATED and has to be built when the host asks for it
# rather than during static init - see RackFactory.h. GMPI_DISABLE_FACTORY
# compiles GMPI's factory out of Core/Common.cpp, which gmpi_plugin() adds
# directly to the plugin target, so this INTERFACE define reaches it.
target_compile_definitions(SynthEditRackAdaptor INTERFACE GMPI_DISABLE_FACTORY)

# INTERFACE sources are compiled into the consumer, which is what puts exactly
# one MP_GetFactory in each plugin.
target_sources(SynthEditRackAdaptor INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/RackFactory.cpp
)

# Shown in IDEs even though nothing compiles here.
target_sources(SynthEditRackAdaptor INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/RackModule.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RackAdaptor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RackEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RackPanelLayout.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RackAutoRegister.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RackDisplayState.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RackNanoVg.h
    ${CMAKE_CURRENT_SOURCE_DIR}/RackFactory.h
    ${CMAKE_CURRENT_SOURCE_DIR}/rack/rack.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/compat/samplerate.h
    ${CMAKE_CURRENT_SOURCE_DIR}/compat/osdialog.h
    ${CMAKE_CURRENT_SOURCE_DIR}/compat/dr_wav.h
)

# ---------------------------------------------------------------------------
# STATIC HOST SUPPORT (RACK_ADAPTOR_STATIC_HOST=ON; default OFF).
#
# For a SynthEdit-based host that links the ported modules INTO its own binary
# (TIDE Rack) instead of loading them as .gmpi bundles. Such a host already
# defines gmpi::RegisterPlugin / gmpi::RegisterPluginWithXml (SynthEditLib's
# UgDatabase.cpp routes both into ModuleFactory()) and owns its entry point,
# so RackFactory.cpp's definitions would all collide - the static flavour
# compiles RackFactoryStatic.cpp instead, which only queues the deferred-XML
# registrations for the host to flush with registerDeferredModules().
#
# Nothing here is created by default, so the ordinary .gmpi build is
# byte-identical with this block present.
#
# The consumer provides:
#   GMPI_SDK          path of the GMPI repo (Core headers)
#   and does NOT link SynthEditRackAdaptor - the two targets are exclusive
#     per binary, because each supplies rack_adaptor::registerPluginLazyXml.
#
# LICENSING IS UNCHANGED BY LINKAGE SHAPE: this library is GPL-3.0-or-later,
# and a host that links it statically is too. See LICENSE and README.md.
option(RACK_ADAPTOR_STATIC_HOST "Provide targets for hosts that link the ported modules statically (TIDE Rack)" OFF)

if(RACK_ADAPTOR_STATIC_HOST)
    # Usage requirements only - the include dirs and language level, same as
    # SynthEditRackAdaptor, minus its INTERFACE sources: a source on this
    # interface would be compiled once per consumer, and the static build has
    # MANY consumers (one OBJECT library per module) in ONE binary.
    add_library(SynthEditRackAdaptorStatic INTERFACE)
    target_include_directories(SynthEditRackAdaptorStatic INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/rack
        ${CMAKE_CURRENT_SOURCE_DIR}/compat
    )
    target_compile_features(SynthEditRackAdaptorStatic INTERFACE cxx_std_20)

    # The one copy of the deferred-registration queue. An OBJECT library so
    # its static state cannot be dead-stripped from an archive, and so exactly
    # one copy exists however many module object libraries join the link.
    add_library(RackAdaptorStaticRegistration OBJECT RackFactoryStatic.cpp)
    target_include_directories(RackAdaptorStaticRegistration PRIVATE
        ${GMPI_SDK}
        ${GMPI_SDK}/Core
    )
    target_link_libraries(RackAdaptorStaticRegistration PRIVATE SynthEditRackAdaptorStatic)
    set_target_properties(RackAdaptorStaticRegistration PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        POSITION_INDEPENDENT_CODE ON)
endif()
