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
